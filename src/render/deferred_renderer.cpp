#include "fuhglgraphics/render/deferred_renderer.hpp"

#include <fuhglgraphics/particles/particle_system.hpp>
#include <fuhglgraphics/probes/light_probes.hpp>
#include <fuhglgraphics/water/water_surface.hpp>

#include "deferred_glsl.hpp"

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render/lod.hpp>
#include <fuhgl/render/shadow_glsl.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fuhglgraphics::render {
namespace {

void makeTarget(fuhgl::Texture& tex, fuhgl::gl::GLenum internal, int w, int h, int msaa) {
    if (msaa > 0) {
        tex = fuhgl::Texture(fuhgl::Texture::Target::Tex2DMultisample);
        tex.bind();
        tex.storageMultisample2D(msaa, internal, w, h);
    } else {
        tex = fuhgl::Texture(fuhgl::Texture::Target::Tex2D);
        tex.bind();
        tex.texStorage2D(1, internal, w, h);
        tex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
        tex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
        tex.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
        tex.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    }
}

} // namespace

struct DeferredRenderer::Impl {
    fuhgl::Framebuffer gbuffer, resolve, output;
    fuhgl::Renderbuffer depthMsaa;
    fuhgl::Texture texAlbedo{fuhgl::Texture::Target::Tex2D}, texNormal{fuhgl::Texture::Target::Tex2D},
        texPbr{fuhgl::Texture::Target::Tex2D}, texEmissive{fuhgl::Texture::Target::Tex2D},
        texMotion{fuhgl::Texture::Target::Tex2D}, texMatId{fuhgl::Texture::Target::Tex2D},
        texDepth{fuhgl::Texture::Target::Tex2D};
    fuhgl::Texture rAlbedo{fuhgl::Texture::Target::Tex2D}, rNormal{fuhgl::Texture::Target::Tex2D},
        rPbr{fuhgl::Texture::Target::Tex2D}, rEmissive{fuhgl::Texture::Target::Tex2D},
        rMotion{fuhgl::Texture::Target::Tex2D}, rMatId{fuhgl::Texture::Target::Tex2D};
    fuhgl::Texture texOutput{fuhgl::Texture::Target::Tex2D};

    fuhgl::render::Material gbufferMat, lightingMat, taaMat, upscaleMat, tonemapMat, copyMat;
    // G13 water: planar-reflection target + cheap forward materials + the water surface pass
    fuhgl::Framebuffer reflFbo;
    fuhgl::Texture reflColor{fuhgl::Texture::Target::Tex2D};
    fuhgl::Texture reflDepth{fuhgl::Texture::Target::Tex2D};
    int reflW = 0, reflH = 0;
    fuhgl::render::Material reflSkyMat, reflMat, waterMat;
    fuhgl::UniformBuffer lightsUbo;
    fuhgl::VertexArray fsVao;
    fuhgl::render::RenderGraph post;
    fuhgl::render::ShadowMap shadowMap;
    fuhgl::render::CascadeShadowMap csm;
    fuhgl::Texture dummyShadowTex{fuhgl::Texture::Target::Tex2D};
    fuhgl::Texture dummyShadowArrayTex{fuhgl::Texture::Target::Tex2DArray};
    fuhgl::Texture dummyCube{fuhgl::Texture::Target::Cube};
    fuhgl::Texture dummy2D{fuhgl::Texture::Target::Tex2D};
    // F46 shadow-kit dummies (keep the lighting pass complete when a sampler is unused)
    fuhgl::Texture dummyMomentsTex{fuhgl::Texture::Target::Tex2D};        // RGBA32F 2D (unit 11)
    fuhgl::Texture dummyMomentsArrayTex{fuhgl::Texture::Target::Tex2DArray}; // RGBA32F array (unit 12)
    fuhgl::Texture dummyShadowCubeTex{fuhgl::Texture::Target::Cube};       // depth cube (unit 13)
    fuhgl::Texture dummyCubeMomentsTex{fuhgl::Texture::Target::Cube};      // RGBA32F cube (unit 14)
    fuhgl::Texture dummy3D{fuhgl::Texture::Target::Tex3D};                 // G7 probe dummy (units 17..23)
};

DeferredRenderer::DeferredRenderer() : impl_(std::make_unique<Impl>()) {}
DeferredRenderer::~DeferredRenderer() = default;
DeferredRenderer::DeferredRenderer(DeferredRenderer&&) noexcept = default;
DeferredRenderer& DeferredRenderer::operator=(DeferredRenderer&&) noexcept = default;

bool DeferredRenderer::create(int width, int height, int msaaSamples) {
    width_ = width;
    height_ = height;
    msaa_ = msaaSamples > 0 ? msaaSamples : 0;
    const fuhgl::gl::Version v = fuhgl::gl::version();
    glMajor_ = v.major;
    glMinor_ = v.minor;
    frameIndex_ = 0;
    prevViewProj_ = fuhgl::math::Mat4{};
    return createInternal();
}

void DeferredRenderer::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    frameIndex_ = 0;
    prevViewProj_ = fuhgl::math::Mat4{};
    createInternal();
}

void DeferredRenderer::setRenderScale(float scale) {
    const float s = clampedRenderScale(scale);
    if (s == renderScale_ || width_ <= 0) {
        renderScale_ = s;
        return;
    }
    renderScale_ = s;
    frameIndex_ = 0;
    prevViewProj_ = fuhgl::math::Mat4{};
    createInternal();
}

bool DeferredRenderer::createInternal() {
    impl_ = std::make_unique<Impl>();
    Impl& im = *impl_;
    internalSize(width_, height_, renderScale_, internalWidth_, internalHeight_);
    if (internalWidth_ <= 0 || internalHeight_ <= 0 || width_ <= 0 || height_ <= 0) return false;
    const int iw = internalWidth_, ih = internalHeight_;

    // --- G-buffer targets (multisampled when requested) ---
    makeTarget(im.texAlbedo, fuhgl::gl::GL_RGBA8, iw, ih, msaa_);
    makeTarget(im.texNormal, fuhgl::gl::GL_RGBA8, iw, ih, msaa_);
    makeTarget(im.texPbr, fuhgl::gl::GL_RGBA8, iw, ih, msaa_);
    makeTarget(im.texEmissive, fuhgl::gl::GL_RGBA16F, iw, ih, msaa_);
    makeTarget(im.texMotion, fuhgl::gl::GL_RG16F, iw, ih, msaa_);
    makeTarget(im.texMatId, fuhgl::gl::GL_RGBA8, iw, ih, msaa_);
    im.texDepth = fuhgl::Texture(fuhgl::Texture::Target::Tex2D);
    im.texDepth.bind();
    im.texDepth.texStorage2D(1, fuhgl::gl::GL_DEPTH_COMPONENT24, iw, ih);
    im.texDepth.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
    im.texDepth.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
    im.texDepth.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    im.texDepth.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);

    im.gbuffer.create();
    im.gbuffer.setSize(iw, ih);
    im.gbuffer.bind();
    im.gbuffer.attachTexture2D(0, im.texAlbedo);
    im.gbuffer.attachTexture2D(1, im.texNormal);
    im.gbuffer.attachTexture2D(2, im.texPbr);
    im.gbuffer.attachTexture2D(3, im.texEmissive);
    im.gbuffer.attachTexture2D(4, im.texMotion);
    im.gbuffer.attachTexture2D(5, im.texMatId);
    if (msaa_ > 0) {
        im.depthMsaa.create();
        im.depthMsaa.bind();
        im.depthMsaa.storageMultisample(msaa_, fuhgl::gl::GL_DEPTH24_STENCIL8, iw, ih);
        im.gbuffer.attachRenderbuffer(fuhgl::Framebuffer::Attachment::Depth, im.depthMsaa);
    } else {
        im.gbuffer.attachTexture2D(fuhgl::Framebuffer::Attachment::Depth, im.texDepth);
    }
    if (!im.gbuffer.isComplete()) {
        std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: G-buffer incomplete (0x%x)\n", im.gbuffer.status());
        return false;
    }

    // --- G13: planar-reflection target (internal size * waterReflectionScale) ---
    im.reflW = std::max(1, static_cast<int>(static_cast<float>(iw) * waterReflectionScale));
    im.reflH = std::max(1, static_cast<int>(static_cast<float>(ih) * waterReflectionScale));
    makeTarget(im.reflColor, fuhgl::gl::GL_RGBA16F, im.reflW, im.reflH, 0);
    im.reflColor.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    im.reflColor.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    im.reflDepth.bind();
    im.reflDepth.texStorage2D(1, fuhgl::gl::GL_DEPTH_COMPONENT24, im.reflW, im.reflH);
    im.reflDepth.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
    im.reflDepth.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
    im.reflDepth.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    im.reflDepth.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    im.reflFbo.create();
    im.reflFbo.setSize(im.reflW, im.reflH);
    im.reflFbo.bind();
    im.reflFbo.attachTexture2D(0, im.reflColor);
    im.reflFbo.attachTexture2D(fuhgl::Framebuffer::Attachment::Depth, im.reflDepth);
    if (!im.reflFbo.isComplete()) {
        std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: reflection FBO incomplete (0x%x)\n", im.reflFbo.status());
        return false;
    }

    // --- resolve target (MSAA only) ---
    if (msaa_ > 0) {
        makeTarget(im.rAlbedo, fuhgl::gl::GL_RGBA8, iw, ih, 0);
        makeTarget(im.rNormal, fuhgl::gl::GL_RGBA8, iw, ih, 0);
        makeTarget(im.rPbr, fuhgl::gl::GL_RGBA8, iw, ih, 0);
        makeTarget(im.rEmissive, fuhgl::gl::GL_RGBA16F, iw, ih, 0);
        makeTarget(im.rMotion, fuhgl::gl::GL_RG16F, iw, ih, 0);
        makeTarget(im.rMatId, fuhgl::gl::GL_RGBA8, iw, ih, 0);
        im.resolve.create();
        im.resolve.setSize(iw, ih);
        im.resolve.bind();
        im.resolve.attachTexture2D(0, im.rAlbedo);
        im.resolve.attachTexture2D(1, im.rNormal);
        im.resolve.attachTexture2D(2, im.rPbr);
        im.resolve.attachTexture2D(3, im.rEmissive);
        im.resolve.attachTexture2D(4, im.rMotion);
        im.resolve.attachTexture2D(5, im.rMatId);
        im.resolve.attachTexture2D(fuhgl::Framebuffer::Attachment::Depth, im.texDepth);
        if (!im.resolve.isComplete()) {
            std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: resolve FBO incomplete (0x%x)\n", im.resolve.status());
            return false;
        }
    }

    // --- LDR output target ---
    makeTarget(im.texOutput, fuhgl::gl::GL_RGBA8, width_, height_, 0);
    im.output.create();
    im.output.setSize(width_, height_);
    im.output.bind();
    im.output.attachTexture2D(0, im.texOutput);
    if (!im.output.isComplete()) {
        std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: output FBO incomplete (0x%x)\n", im.output.status());
        return false;
    }
    fuhgl::Framebuffer::unbind();

    // --- shaders ---
    if (!im.gbufferMat.create()) return false;
    {
        fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
        if (!vs.compile(kGBufferVert)) {
            std::fprintf(stderr, "DeferredRenderer: gbuffer VS: %s\n", vs.infoLog().c_str());
            return false;
        }
        if (!fs.compile(kGBufferFrag)) {
            std::fprintf(stderr, "DeferredRenderer: gbuffer FS: %s\n", fs.infoLog().c_str());
            return false;
        }
        im.gbufferMat.program().attach(vs);
        im.gbufferMat.program().attach(fs);
        if (!im.gbufferMat.program().link()) {
            std::fprintf(stderr, "DeferredRenderer: gbuffer link: %s\n", im.gbufferMat.program().infoLog().c_str());
            return false;
        }
    }
    const std::string lightFrag = std::string(kLightFragHead) + kLightFragKit +
                                  fuhgl::render::kSkFuncs + kLightFragProbes + kLightFragPbr + kLightFragTail;
    if (!compilePostFs(im.lightingMat, lightFrag, "lighting") ||
        !compilePostFs(im.taaMat, kTaaFrag, "taa") ||
        !compilePostFs(im.upscaleMat, kUpscaleFrag, "upscale") ||
        !compilePostFs(im.tonemapMat, kTonemapFrag, "tonemap") ||
        !compilePostFs(im.copyMat, kCopyFrag, "copy")) {
        return false;
    }
    // G13 water shaders: reflection background/geometry (fullscreen + mesh) + water surface
    {
        fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
        if (!im.reflMat.create()) return false;
        if (!vs.compile(kReflVert)) {
            std::fprintf(stderr, "DeferredRenderer: reflection VS: %s\n", vs.infoLog().c_str());
            return false;
        }
        if (!fs.compile(kReflFrag)) {
            std::fprintf(stderr, "DeferredRenderer: reflection FS: %s\n", fs.infoLog().c_str());
            return false;
        }
        im.reflMat.program().attach(vs);
        im.reflMat.program().attach(fs);
        if (!im.reflMat.program().link()) {
            std::fprintf(stderr, "DeferredRenderer: reflection link: %s\n", im.reflMat.program().infoLog().c_str());
            return false;
        }
    }
    if (!compilePostFs(im.reflSkyMat, kReflSkyFrag, "reflection sky") ||
        !compileWaterMat(im.waterMat)) {
        return false;
    }
    im.lightingMat.program().bindUniformBlock("LightBlock", 0);

    if (!im.lightsUbo.create()) return false;
    im.lightsUbo.bindBase(0);
    if (!im.fsVao.create()) return false;
    im.fsVao.bind();

    // --- recreate shadow maps when they were enabled before a (re)create ---
    // (F46: VSM additionally renders RGBA32F moments into a second attachment)
    const bool moments = shadowFilter == fuhgl::render::ShadowFilter::Vsm;
    if (shadows_ && shadowSize_ > 0) impl_->shadowMap.create(shadowSize_, moments);
    if (csmEnabled_ && csmSize_ > 0) impl_->csm.create(csmSize_, csmCascades_, moments);
    if (pointShadows_ && pointShadowSize_ > 0) pointShadowMap_.create(pointShadowSize_, moments);

    // --- dummy samplers keep the lighting pass complete when shadows/IBL are off ---
    im.dummyShadowTex.bind();
    im.dummyShadowTex.texStorage2D(1, fuhgl::gl::GL_DEPTH_COMPONENT24, 1, 1);
    im.dummyShadowTex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummyShadowTex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummyShadowArrayTex.bind();
    im.dummyShadowArrayTex.texStorage3D(1, fuhgl::gl::GL_DEPTH_COMPONENT24, 1, 1, 1);
    im.dummyShadowArrayTex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummyShadowArrayTex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
    const fuhgl::Texture::CubeFace cubeFaces[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    im.dummyCube.bind();
    for (int face = 0; face < 6; ++face)
        im.dummyCube.setImageCubeFace(cubeFaces[face], 0, fuhgl::gl::GL_RGBA8, 1,
                                      fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, nullptr);
    im.dummyCube.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummyCube.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummy2D.bind();
    im.dummy2D.texStorage2D(1, fuhgl::gl::GL_RG16F, 1, 1);
    im.dummy2D.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummy2D.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
    // F46 shadow-kit dummies: VSM moments 2D/array (RGBA32F), point cube depth + moments
    im.dummyMomentsTex.bind();
    im.dummyMomentsTex.texStorage2D(1, fuhgl::gl::GL_RGBA32F, 1, 1);
    im.dummyMomentsTex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    im.dummyMomentsTex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    im.dummyMomentsArrayTex.bind();
    im.dummyMomentsArrayTex.texStorage3D(1, fuhgl::gl::GL_RGBA32F, 1, 1, 1);
    im.dummyMomentsArrayTex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    im.dummyMomentsArrayTex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    im.dummyShadowCubeTex.bind();
    for (int face = 0; face < 6; ++face)
        im.dummyShadowCubeTex.setImageCubeFace(cubeFaces[face], 0,
                                               fuhgl::gl::GL_DEPTH_COMPONENT24, 1, fuhgl::gl::GL_DEPTH_COMPONENT,
                                               fuhgl::gl::GL_FLOAT, nullptr);
    im.dummyShadowCubeTex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummyShadowCubeTex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_NEAREST);
    im.dummyCubeMomentsTex.bind();
    for (int face = 0; face < 6; ++face)
        im.dummyCubeMomentsTex.setImageCubeFace(cubeFaces[face], 0,
                                                fuhgl::gl::GL_RGBA32F, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_FLOAT,
                                                nullptr);
    im.dummyCubeMomentsTex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    im.dummyCubeMomentsTex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    // G7: dummy 3D texture keeps the lighting pass complete when probes are off
    im.dummy3D.bind();
    im.dummy3D.setImage3D(0, fuhgl::gl::GL_RGBA16F, 1, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_HALF_FLOAT, nullptr);
    im.dummy3D.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    im.dummy3D.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);

    // --- post graph: lighting -> TAA -> upscale -> tonemap ---
    const fuhgl::Texture& gAlbedo = msaa_ > 0 ? im.rAlbedo : im.texAlbedo;
    const fuhgl::Texture& gNormal = msaa_ > 0 ? im.rNormal : im.texNormal;
    const fuhgl::Texture& gPbr = msaa_ > 0 ? im.rPbr : im.texPbr;
    const fuhgl::Texture& gEmissive = msaa_ > 0 ? im.rEmissive : im.texEmissive;
    const fuhgl::Texture& gMotion = msaa_ > 0 ? im.rMotion : im.texMotion;
    im.post.addExternal("gAlbedo", &gAlbedo, iw, ih);
    im.post.addExternal("gNormal", &gNormal, iw, ih);
    im.post.addExternal("gPbr", &gPbr, iw, ih);
    im.post.addExternal("gEmissive", &gEmissive, iw, ih);
    im.post.addExternal("gMotion", &gMotion, iw, ih);
    im.post.addExternal("gMatId", &(msaa_ > 0 ? im.rMatId : im.texMatId), iw, ih);
    im.post.addExternal("gDepth", &im.texDepth, iw, ih);
    im.post.addResource("hdr", iw, ih, fuhgl::gl::GL_RGBA16F);
    im.post.addResource("hdrW", iw, ih, fuhgl::gl::GL_RGBA16F); // hdr + G13 water
    im.post.addResource("hdrP", iw, ih, fuhgl::gl::GL_RGBA16F); // hdrW + G5 particles
    im.post.addResource("taa", iw, ih, fuhgl::gl::GL_RGBA16F);
    im.post.addResource("up", width_, height_, fuhgl::gl::GL_RGBA16F);
    im.post.addPass("lighting", {"gAlbedo", "gNormal", "gPbr", "gEmissive", "gDepth"}, {"hdr"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawLighting(ctx); });
    im.post.addPass("water", {"hdr", "gDepth"}, {"hdrW"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawWater(ctx); });
    im.post.addPass("particles", {"hdrW", "gDepth"}, {"hdrP"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawParticles(ctx); });
    im.post.addPass("taa", {"hdrP", "gMotion", "taa"}, {"taa"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawTaa(ctx); }, /*feedback=*/true);
    im.post.addPass("upscale", {"taa"}, {"up"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawUpscale(ctx); });
    im.post.addPass("tonemap", {"up", "gDepth"}, {},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawTonemap(ctx); });
    if (!im.post.compile()) {
        std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: post graph compile failed\n");
        return false;
    }
    return true;
}

void DeferredRenderer::renderGBuffer(const fuhgl::render::SceneView& scene,
                                     const std::vector<std::uint32_t>* materialIds) {
    Impl& im = *impl_;
    auto& f = fuhgl::gl::functions();
    fuhgl::gl::enable(fuhgl::gl::Cap::DepthTest);
    fuhgl::gl::depthFunc(fuhgl::gl::CompareFunc::Less);
    fuhgl::gl::depthMask(true);
    fuhgl::gl::enable(fuhgl::gl::Cap::CullFace);
    fuhgl::gl::cullFace(fuhgl::gl::Face::Back);
    fuhgl::gl::frontFace(true);
    fuhgl::gl::disable(fuhgl::gl::Cap::Blend);

    fuhgl::render::LightBlock block{};
    packLightBlock(scene.lights, block);
    im.lightsUbo.upload(block);
    im.lightsUbo.bindBase(0);

    im.gbuffer.bind();
    const fuhgl::gl::GLenum drawBufs[6] = {fuhgl::gl::GL_COLOR_ATTACHMENT0, fuhgl::gl::GL_COLOR_ATTACHMENT1,
                                            fuhgl::gl::GL_COLOR_ATTACHMENT2, fuhgl::gl::GL_COLOR_ATTACHMENT3,
                                            fuhgl::gl::GL_COLOR_ATTACHMENT4, fuhgl::gl::GL_COLOR_ATTACHMENT5};
    fuhgl::gl::drawBuffers(6, drawBufs);
    fuhgl::gl::viewport(0, 0, internalWidth_, internalHeight_);
    const float clearZero[4] = {0.f, 0.f, 0.f, 0.f};
    const float clearAlbedo[4] = {scene.clearColor.x, scene.clearColor.y, scene.clearColor.z, 1.f};
    fuhgl::gl::clearBufferfv(fuhgl::gl::GL_COLOR, 0, clearAlbedo);
    for (int i = 1; i < 6; ++i) fuhgl::gl::clearBufferfv(fuhgl::gl::GL_COLOR, i, clearZero);
    fuhgl::gl::clear(fuhgl::gl::ClearBit::Depth);

    const fuhgl::math::Mat4 viewProj =
        fuhgl::math::jitterViewProj(scene.camera->viewProj(scene.aspect), scene.jitter);
    const fuhgl::math::Mat4 prevViewProj = prevViewProj_;

    fuhgl::Program& prog = im.gbufferMat.program();
    prog.use();
    prog.setUniform("uViewProj", viewProj);
    prog.setUniform("uPrevViewProj", prevViewProj);
    prog.setUniform("uResolution", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                      static_cast<float>(internalHeight_)));
    for (std::size_t i = 0; i < scene.items.size(); ++i) {
        const fuhgl::render::DrawItem& item = scene.items[i];
        if (curWater_ && item.mesh == &curWater_->mesh()) continue; // G13: water leaves the G-buffer
        int lodLevel = -1;
        const fuhgl::render::Mesh* mesh =
            fuhgl::render::resolveLodMesh(item, scene.camera->eye(), scene.lodEnabled, &lodLevel);
        if (!mesh) continue;
        if (lodLevel >= 0) scene.lastLodLevel = lodLevel;
        prog.setUniform("uModel", item.model);
        prog.setUniform("uInstanced", mesh->instanceCount() > 0 ? 1 : 0);
        prog.setUniform("uMaterialId", materialIds && i < materialIds->size() ? (*materialIds)[i] : 0u);
        const fuhgl::render::Material* preset =
            (item.material && item.material->builtinShader) ? item.material : nullptr;
        if (preset) {
            prog.setUniform("uAlbedo", preset->albedo);
            prog.setUniform("uEmissive", preset->emissive);
            prog.setUniform("uUseTex", preset->albedoTexture ? 1 : 0);
            if (preset->albedoTexture) {
                fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitAlbedo);
                f.glBindTexture(static_cast<fuhgl::gl::GLenum>(preset->albedoTexture->target()),
                                preset->albedoTexture->id());
                prog.setUniform("uMatTex", fuhgl::render::kTextureUnitAlbedo);
            }
            prog.setUniform("uUseNormalMap", preset->normalMap ? 1 : 0);
            if (preset->normalMap) {
                fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitNormalMap);
                f.glBindTexture(static_cast<fuhgl::gl::GLenum>(preset->normalMap->target()),
                                preset->normalMap->id());
                prog.setUniform("uNormalTex", fuhgl::render::kTextureUnitNormalMap);
            }
            prog.setUniform("uMetallic", preset->metallic);
            prog.setUniform("uRoughness", preset->roughness);
            prog.setUniform("uAo", preset->ao);
        } else {
            prog.setUniform("uAlbedo", fuhgl::math::Vec3{1.f, 1.f, 1.f});
            prog.setUniform("uEmissive", fuhgl::math::Vec3{0.f, 0.f, 0.f});
            prog.setUniform("uUseTex", 0);
            prog.setUniform("uUseNormalMap", 0);
            prog.setUniform("uMetallic", 0.f);
            prog.setUniform("uRoughness", 0.5f);
            prog.setUniform("uAo", 1.f);
        }
        mesh->draw();
    }

    // --- MSAA resolve: one blit per color attachment + one depth blit ---
    if (msaa_ > 0) {
        f.glBindFramebuffer(fuhgl::gl::GL_READ_FRAMEBUFFER, im.gbuffer.id());
        f.glBindFramebuffer(fuhgl::gl::GL_DRAW_FRAMEBUFFER, im.resolve.id());
        for (int i = 0; i < 6; ++i) {
            const fuhgl::gl::GLenum att = fuhgl::gl::GL_COLOR_ATTACHMENT0 + i;
            f.glReadBuffer(att); // applies to the bound READ framebuffer (gbuffer)
            const fuhgl::gl::GLenum dst[1] = {att};
            fuhgl::gl::drawBuffers(1, dst); // applies to the bound DRAW framebuffer (resolve)
            f.glBlitFramebuffer(0, 0, internalWidth_, internalHeight_, 0, 0, internalWidth_, internalHeight_,
                                fuhgl::gl::GL_COLOR_BUFFER_BIT, fuhgl::gl::GL_NEAREST);
        }
        f.glBlitFramebuffer(0, 0, internalWidth_, internalHeight_, 0, 0, internalWidth_, internalHeight_,
                            fuhgl::gl::GL_DEPTH_BUFFER_BIT, fuhgl::gl::GL_NEAREST);
        f.glBindFramebuffer(fuhgl::gl::GL_READ_FRAMEBUFFER, 0);
        f.glBindFramebuffer(fuhgl::gl::GL_DRAW_FRAMEBUFFER, 0);
    }
}

// G13: planar reflection pass — sky cubemap background (seen by the mirrored camera) plus
// cheap forward shading (lambert + blinn + fog, no shadows) of all non-water items. Items
// below the water plane are clipped in the reflection vertex shader; the mirrored camera
// flips the winding, so frontFace is inverted for this pass.
void DeferredRenderer::renderReflection(const fuhgl::render::SceneView& s,
                                        const fuhgl::math::Mat4& reflViewProj,
                                        const fuhgl::math::Vec4& clipPlane,
                                        const fuhgl::math::Vec3& reflEye) {
    Impl& im = *impl_;
    auto& f = fuhgl::gl::functions();
    im.reflFbo.bind();
    const fuhgl::gl::GLenum buf[1] = {fuhgl::gl::GL_COLOR_ATTACHMENT0};
    fuhgl::gl::drawBuffers(1, buf);
    fuhgl::gl::viewport(0, 0, im.reflW, im.reflH);
    const float clear[4] = {s.fogColor.x, s.fogColor.y, s.fogColor.z, 1.f};
    fuhgl::gl::clearBufferfv(fuhgl::gl::GL_COLOR, 0, clear);
    fuhgl::gl::clear(fuhgl::gl::ClearBit::Depth);
    fuhgl::gl::enable(fuhgl::gl::Cap::DepthTest);
    fuhgl::gl::depthFunc(fuhgl::gl::CompareFunc::Less);
    fuhgl::gl::depthMask(true);
    fuhgl::gl::disable(fuhgl::gl::Cap::Blend);

    // sun = first directional light (sky disk + geometry shading)
    fuhgl::math::Vec3 sunDir{0.f, 1.f, 0.f};
    fuhgl::math::Vec3 sunIrr{0.f, 0.f, 0.f};
    for (const fuhgl::render::Light& l : s.lights)
        if (l.type == fuhgl::render::LightType::Directional) {
            sunDir = -fuhgl::math::normalize(l.direction);
            sunIrr = l.color * l.intensity;
            break;
        }

    // sky cubemap + sun disk for the reflection background (unit kTextureUnitSkyCube)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + kTextureUnitSkyCube);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_CUBE_MAP, skyCube_ ? skyCube_->id() : im.dummyCube.id());
    drawFs(im.reflSkyMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uSkyCube"), kTextureUnitSkyCube);
        p.setUniform(p.uniformLocation("uUseSkyCube"), skyCube_ ? 1 : 0);
        p.setUniform("uInvViewProj", reflViewProj.inverse());
        p.setUniform("uCamPos", reflEye);
        p.setUniform("uSkyTop", skyTop);
        p.setUniform("uSkyBottom", skyBottom);
        p.setUniform("uSunDir", sunDir);
        p.setUniform("uSunColor", sunIrr * 4.f);
        p.setUniform("uSunCos", s.sunCos);
    });

    // scene geometry, mirrored
    fuhgl::gl::enable(fuhgl::gl::Cap::CullFace);
    fuhgl::gl::cullFace(fuhgl::gl::Face::Back);
    fuhgl::gl::frontFace(false); // mirrored camera flips the winding
    fuhgl::Program& prog = im.reflMat.program();
    prog.use();
    prog.setUniform("uViewProj", reflViewProj);
    prog.setUniform("uClipPlane", clipPlane);
    prog.setUniform("uCamPos", reflEye);
    prog.setUniform("uAmbient", s.ambient);
    prog.setUniform("uSunDir", sunDir);
    prog.setUniform("uSunIrradiance", sunIrr);
    prog.setUniform("uFogColor", s.fogColor);
    prog.setUniform("uFogDensity", s.fogDensity);
    for (const fuhgl::render::DrawItem& item : s.items) {
        if (curWater_ && item.mesh == &curWater_->mesh()) continue; // no water in its own reflection
        const fuhgl::render::Mesh* mesh = fuhgl::render::resolveLodMesh(item, s.camera->eye(), s.lodEnabled);
        if (!mesh) continue;
        prog.setUniform("uModel", item.model);
        const fuhgl::render::Material* preset =
            (item.material && item.material->builtinShader) ? item.material : nullptr;
        if (preset) {
            prog.setUniform("uAlbedo", preset->albedo);
            prog.setUniform(prog.uniformLocation("uUseTex"), preset->albedoTexture ? 1 : 0);
            if (preset->albedoTexture) {
                fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitAlbedo);
                f.glBindTexture(static_cast<fuhgl::gl::GLenum>(preset->albedoTexture->target()),
                                preset->albedoTexture->id());
                prog.setUniform(prog.uniformLocation("uMatTex"), fuhgl::render::kTextureUnitAlbedo);
            }
        } else {
            prog.setUniform("uAlbedo", fuhgl::math::Vec3{1.f, 1.f, 1.f});
            prog.setUniform(prog.uniformLocation("uUseTex"), 0);
        }
        mesh->draw();
    }
    fuhgl::gl::frontFace(true);
    fuhgl::Framebuffer::unbind();
}

void DeferredRenderer::render(const fuhgl::render::SceneView& scene,
                              const std::vector<std::uint32_t>* materialIds,
                              const std::vector<bool>* castShadow,
                              const std::vector<const particles::ParticleSystem*>* particleSystems,
                              const probes::LightProbeGrid* lightProbes,
                              const water::WaterSurface* water) {
    if (!scene.camera || width_ <= 0) return;
    fuhgl::render::SceneView s = scene;
    applyCapabilities();
    curParticles_ = particleSystems;
    curProbes_ = lightProbes;

    // --- G13 water: resolve the active surface + its world plane, render the planar
    // reflection from the mirrored camera (clipped at the plane) before the G-buffer ---
    curWater_ = (water && water->isReady() && effectEnabled(Effect::Water)) ? water : nullptr;
    waterModel_ = fuhgl::math::Mat4{};
    if (curWater_) {
        for (const fuhgl::render::DrawItem& item : s.items)
            if (item.mesh == &curWater_->mesh()) {
                waterModel_ = item.model;
                break;
            }
        float planeD = 0.f;
        water::waterPlaneFromModel(waterModel_, waterPlaneN_, planeD);
        waterClipPlane_ = {waterPlaneN_.x, waterPlaneN_.y, waterPlaneN_.z, planeD};
        const fuhgl::math::Mat4 refl = water::reflectionMatrix(waterPlaneN_, planeD);
        const fuhgl::math::Mat4 reflViewProj = s.camera->projection(s.aspect) * s.camera->view() * refl;
        const fuhgl::math::Vec3 eye = s.camera->eye();
        const fuhgl::math::Vec4 reflEye = refl * fuhgl::math::Vec4{eye.x, eye.y, eye.z, 1.f};
        renderReflection(s, reflViewProj, waterClipPlane_, {reflEye.x, reflEye.y, reflEye.z});
    }

    if (s.env && !effectEnabled(Effect::Ibl)) {
        s.env = nullptr; // gated in applyCapabilities() with a one-time warning
    }

    // per-item shadow casting (G9): the depth pass only draws items flagged castShadow;
    // the G13 water surface never casts a shadow
    fuhgl::render::SceneView shadowScene;
    const fuhgl::render::SceneView* shadowView = &s;
    if ((castShadow && !castShadow->empty()) || curWater_) {
        shadowScene = s;
        std::vector<fuhgl::render::DrawItem> items;
        items.reserve(s.items.size());
        for (std::size_t i = 0; i < s.items.size(); ++i) {
            if (curWater_ && s.items[i].mesh == &curWater_->mesh()) continue; // water: no shadow
            if (castShadow && !castShadow->empty() && (i >= castShadow->size() || !(*castShadow)[i])) continue;
            items.push_back(s.items[i]);
        }
        shadowScene.items = std::move(items);
        shadowView = &shadowScene;
    }

    // --- shadow depth pass: CSM (first directional light) or single map ---
    // (F45 kit: cascade VPs are texel-snapped when shadowSnapToTexels is set)
    activePointShadowLightIndex_ = -1;
    if (csmEnabled_ && impl_->csm.cascadeCount() > 0) {
        impl_->csm.snapToTexels = shadowSnapToTexels;
        for (std::size_t i = 0; i < s.lights.size(); ++i) {
            if (s.lights[i].type == fuhgl::render::LightType::Directional) {
                impl_->csm.render(*shadowView, *s.camera, s.lights[i], shadowCenter_, shadowRadius_, shadowZNear_, shadowZFar_);
                s.cascades = &impl_->csm;
                s.shadowLightIndex = static_cast<int>(i);
                s.shadowStrength = shadowStrength_;
                break;
            }
        }
    } else if (shadows_ && impl_->shadowMap.size() > 0) {
        for (std::size_t i = 0; i < s.lights.size(); ++i) {
            if (s.lights[i].type == fuhgl::render::LightType::Directional ||
                s.lights[i].type == fuhgl::render::LightType::Spot) {
                s.lightVP = fuhgl::render::lightViewProj(s.lights[i], shadowCenter_, shadowRadius_);
                impl_->shadowMap.render(*shadowView, s.lightVP);
                s.shadowMap = &impl_->shadowMap;
                s.shadowLightIndex = static_cast<int>(i);
                s.shadowStrength = shadowStrength_;
                break;
            }
        }
    }

    // --- F46: point-light cube shadow (first point light, or the requested index) ---
    if (pointShadows_ && pointShadowMap_.size() > 0) {
        for (std::size_t i = 0; i < s.lights.size(); ++i) {
            const bool match = pointShadowLightIndex_ >= 0
                                   ? static_cast<int>(i) == pointShadowLightIndex_
                                   : s.lights[i].type == fuhgl::render::LightType::Point;
            if (match && s.lights[i].type == fuhgl::render::LightType::Point) {
                pointShadowMap_.render(*shadowView, s.lights[i], pointShadowNear_, pointShadowFar_);
                activePointShadowLightIndex_ = static_cast<int>(i);
                break;
            }
        }
    }

    // sky sun disk from the first directional light
    for (const fuhgl::render::Light& l : s.lights) {
        if (l.type == fuhgl::render::LightType::Directional) {
            s.sunDirection = -fuhgl::math::normalize(l.direction);
            s.sunColor = l.color * (l.intensity * 4.f);
            break;
        }
    }

    // TAA: Halton subpixel jitter in NDC units, applied by the G-buffer pass
    const bool taaOn = effectEnabled(Effect::Taa);
    if (taaOn) {
        s.jitter = taaJitterNdc(frameIndex_, internalWidth_, internalHeight_);
    } else {
        s.jitter = {0.f, 0.f};
        if (frameIndex_ != 0) {
            frameIndex_ = 0;
            prevViewProj_ = fuhgl::math::Mat4{};
        }
    }

    curScene_ = &s;
    renderGBuffer(s, materialIds);
    // remember this frame's (jittered) viewProj for next frame's motion vectors
    prevViewProj_ = fuhgl::math::jitterViewProj(s.camera->viewProj(s.aspect), s.jitter);
    impl_->post.execute();
    if (taaOn) ++frameIndex_;
    curScene_ = nullptr;
    curParticles_ = nullptr;
    curProbes_ = nullptr;
    curWater_ = nullptr;
}

// G13 water post pass: hdr -> hdrW. Without an active water surface this is a plain copy;
// with one, the water mesh is shaded on top (Gerstner waves + planar reflection +
// refraction + foam). The post FBO has no depth attachment, so the fragment shader does
// the occlusion test manually against the G-buffer depth (uGDepth).
void DeferredRenderer::drawWater(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdr");
    ctx.bindInput(1, "gDepth");
    Impl& im = *impl_;
    // always start from the lighting result (hdr -> hdrW) ...
    drawFs(im.copyMat, [&](fuhgl::Program& p) { p.setUniform(p.uniformLocation("uSrc"), 0); });
    if (!curWater_ || !curScene_) return; // ... and only shade the water surface on top when active
    auto& f = fuhgl::gl::functions();
    const fuhgl::render::SceneView& s = *curScene_;
    const water::WaterSurface& w = *curWater_;
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + 2);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, im.reflColor.id());
    fuhgl::gl::disable(fuhgl::gl::Cap::DepthTest);
    fuhgl::gl::disable(fuhgl::gl::Cap::Blend);
    fuhgl::gl::enable(fuhgl::gl::Cap::CullFace);
    fuhgl::gl::cullFace(fuhgl::gl::Face::Back);
    fuhgl::gl::frontFace(true);
    fuhgl::Program& p = im.waterMat.program();
    p.use();
    p.setUniform(p.uniformLocation("uSceneHdr"), 0);
    p.setUniform(p.uniformLocation("uGDepth"), 1);
    p.setUniform(p.uniformLocation("uReflTex"), 2);
    p.setUniform("uModel", waterModel_);
    // one uViewProj for VS (position) and FS (depth test): the G-buffer was rendered with
    // the same jittered viewProj, so the depth comparison uses the same quantization
    p.setUniform("uViewProj", fuhgl::math::jitterViewProj(s.camera->viewProj(s.aspect), s.jitter));
    p.setUniform("uInvViewProj", s.camera->viewProj(s.aspect).inverse());
    p.setUniform("uCamPos", s.camera->eye());
    p.setUniform("uResolution", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                   static_cast<float>(internalHeight_)));
    p.setUniform("uTime", w.time);
    p.setUniform("uPlaneN", waterPlaneN_);
    p.setUniform("uDeepColor", w.deepColor);
    p.setUniform("uShallowColor", w.shallowColor);
    p.setUniform("uFoamColor", w.foamColor);
    p.setUniform("uFresnelF0", w.fresnelF0);
    p.setUniform("uReflectionStrength", w.reflectionStrength);
    p.setUniform("uDistortion", w.distortion);
    p.setUniform("uRefractionDistortion", w.refractionDistortion);
    p.setUniform("uFoamCrestStart", w.foamCrestStart);
    p.setUniform("uFoamCrestSoftness", w.foamCrestSoftness);
    p.setUniform("uShoreFoamWidth", w.shoreFoamWidth);
    p.setUniform("uFoamStrength", w.foamStrength);
    p.setUniform("uGlintPower", w.glintPower);
    p.setUniform("uDetailScale", w.detailNormalScale);
    p.setUniform("uSunDir", s.sunDirection);
    p.setUniform("uSunColor", s.sunColor);
    p.setUniform("uFogColor", s.fogColor);
    p.setUniform("uFogDensity", s.fogDensity);
    const int waveCount = std::min(static_cast<int>(w.waves.size()), water::WaterSurface::kMaxWaves);
    p.setUniform(p.uniformLocation("uWaveCount"), waveCount);
    for (int i = 0; i < water::WaterSurface::kMaxWaves; ++i) {
        char nameA[16], nameB[16];
        std::snprintf(nameA, sizeof(nameA), "uWaveA[%d]", i);
        std::snprintf(nameB, sizeof(nameB), "uWaveB[%d]", i);
        if (i < waveCount) {
            const water::GerstnerWave& g = w.waves[static_cast<std::size_t>(i)];
            const fuhgl::math::Vec2 d = fuhgl::math::normalize(g.direction);
            const float k = 2.f * fuhgl::math::kPi / std::max(g.wavelength, 1e-3f);
            const float q = g.steepness / std::max(k * g.amplitude * static_cast<float>(waveCount), 1e-6f);
            p.setUniform(p.uniformLocation(nameA), fuhgl::math::Vec4{d.x, d.y, g.amplitude, k});
            p.setUniform(p.uniformLocation(nameB), fuhgl::math::Vec4{k * g.speed, q, 0.f, 0.f});
        } else {
            p.setUniform(p.uniformLocation(nameA), fuhgl::math::Vec4{0.f, 0.f, 0.f, 0.f});
            p.setUniform(p.uniformLocation(nameB), fuhgl::math::Vec4{0.f, 0.f, 0.f, 0.f});
        }
    }
    w.mesh().draw();
}

// G5 particles post pass: copy hdrW -> hdrP, then draw each system's instanced billboards
// (blended, depth-tested against the G-buffer depth) into hdrP.
void DeferredRenderer::drawParticles(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdrW"); // lighting + G13 water
    ctx.bindInput(1, "gDepth");
    Impl& im = *impl_;
    drawFs(im.copyMat, [&](fuhgl::Program& p) { p.setUniform(p.uniformLocation("uSrc"), 0); });
    if (!effectEnabled(Effect::Particles) || !curParticles_ || curParticles_->empty() || !curScene_) return;
    const fuhgl::render::SceneView& s = *curScene_;
    // the pass target hdrP has no depth attachment — occlusion is done in the billboard
    // shader against uSceneDepth (unit 1 = gDepth), with a soft-particle distance fade
    for (const particles::ParticleSystem* ps : *curParticles_) {
        if (!ps || !ps->isReady()) continue;
        ps->beginPass(*s.camera, s.aspect, internalWidth_, internalHeight_, 1);
        if (ps->emitter().blend == particles::BlendMode::Additive)
            fuhgl::gl::blendFunc(fuhgl::gl::BlendFactor::One, fuhgl::gl::BlendFactor::One);
        else
            fuhgl::gl::blendFunc(fuhgl::gl::BlendFactor::SrcAlpha, fuhgl::gl::BlendFactor::OneMinusSrcAlpha);
        fuhgl::gl::enable(fuhgl::gl::Cap::Blend);
        ps->drawPass();
        fuhgl::gl::disable(fuhgl::gl::Cap::Blend);
    }
}

void DeferredRenderer::drawFs(fuhgl::render::Material& mat,
                              const std::function<void(fuhgl::Program&)>& setUniforms) {
    fuhgl::gl::disable(fuhgl::gl::Cap::DepthTest);
    fuhgl::gl::disable(fuhgl::gl::Cap::Blend);
    mat.bind();
    if (setUniforms) setUniforms(mat.program());
    impl_->fsVao.bind();
    fuhgl::gl::drawArrays(fuhgl::gl::Primitive::Triangles, 0, 3);
}

void DeferredRenderer::drawLighting(fuhgl::render::RenderGraph::PassContext& ctx) {
    const fuhgl::render::SceneView& s = *curScene_;
    Impl& im = *impl_;
    auto& f = fuhgl::gl::functions();
    ctx.bindInput(0, "gAlbedo");
    ctx.bindInput(1, "gNormal");
    ctx.bindInput(2, "gPbr");
    ctx.bindInput(3, "gEmissive");
    ctx.bindInput(5, "gDepth");
    // shadow samplers (units 4 / 10; dummies when off)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitShadowMap);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, s.shadowMap ? s.shadowMap->textureId() : im.dummyShadowTex.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitShadowMapArray);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D_ARRAY,
                    s.cascades ? s.cascades->textureId() : im.dummyShadowArrayTex.id());
    // F46 VSM moments (units 11/12; dummies when the filter is not VSM)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitShadowMoments);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D,
                    s.shadowMap && s.shadowMap->moments() ? s.shadowMap->momentsTextureId() : im.dummyMomentsTex.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitShadowMomentsArray);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D_ARRAY,
                    s.cascades && s.cascades->moments() ? s.cascades->momentsTextureId() : im.dummyMomentsArrayTex.id());
    // F46 point-light cube shadow (units 13/14; dummies when off)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitShadowCube);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_CUBE_MAP,
                    activePointShadowLightIndex_ >= 0 ? pointShadowMap_.textureId() : im.dummyShadowCubeTex.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitShadowCubeMoments);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_CUBE_MAP,
                    activePointShadowLightIndex_ >= 0 && pointShadowMap_.moments()
                        ? pointShadowMap_.momentsTextureId()
                        : im.dummyCubeMomentsTex.id());
    // IBL samplers (units 7/8/9; dummies when off)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitIrradiance);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_CUBE_MAP, s.env ? s.env->irradianceMap().id() : im.dummyCube.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitPrefilter);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_CUBE_MAP, s.env ? s.env->prefilterMap().id() : im.dummyCube.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitBrdfLut);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, s.env ? s.env->brdfLut().id() : im.dummy2D.id());
    // sky cubemap (unit 16; dummy when off)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + kTextureUnitSkyCube);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_CUBE_MAP, skyCube_ ? skyCube_->id() : im.dummyCube.id());
    // G7 light probes: seven RGBA16F 3D textures on units 17..23 (lazy upload, needs
    // the current GL context — render() guarantees one); a dummy when off
    bool probesOn = effectEnabled(Effect::LightProbes) && curProbes_ && curProbes_->valid();
    if (probesOn) probesOn = curProbes_->ensureUploaded();
    for (int i = 0; i < probes::LightProbeGrid::kTextureCount; ++i) {
        fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + probes::LightProbeGrid::kFirstTextureUnit + i);
        f.glBindTexture(fuhgl::gl::GL_TEXTURE_3D,
                        probesOn ? curProbes_->texture(i).id() : im.dummy3D.id());
    }

    drawFs(im.lightingMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uGAlbedo"), 0);
        p.setUniform(p.uniformLocation("uGNormal"), 1);
        p.setUniform(p.uniformLocation("uGPbr"), 2);
        p.setUniform(p.uniformLocation("uGEmissive"), 3);
        p.setUniform(p.uniformLocation("uGDepth"), 5);
        p.setUniform(p.uniformLocation("uShadowMap"), fuhgl::render::kTextureUnitShadowMap);
        p.setUniform(p.uniformLocation("uShadowMapArray"), fuhgl::render::kTextureUnitShadowMapArray);
        p.setUniform(p.uniformLocation("uShadowMoments"), fuhgl::render::kTextureUnitShadowMoments);
        p.setUniform(p.uniformLocation("uShadowMomentsArray"), fuhgl::render::kTextureUnitShadowMomentsArray);
        p.setUniform(p.uniformLocation("uShadowCube"), fuhgl::render::kTextureUnitShadowCube);
        p.setUniform(p.uniformLocation("uShadowCubeMoments"), fuhgl::render::kTextureUnitShadowCubeMoments);
        p.setUniform(p.uniformLocation("uSceneDepth"), 5); // = uGDepth
        p.setUniform(p.uniformLocation("uIrradianceMap"), fuhgl::render::kTextureUnitIrradiance);
        p.setUniform(p.uniformLocation("uPrefilterMap"), fuhgl::render::kTextureUnitPrefilter);
        p.setUniform(p.uniformLocation("uBrdfLut"), fuhgl::render::kTextureUnitBrdfLut);
        p.setUniform(p.uniformLocation("uSkyCube"), kTextureUnitSkyCube);
        p.setUniform(p.uniformLocation("uUseSkyCube"), skyCube_ ? 1 : 0);
        // G7 light probes
        for (int i = 0; i < probes::LightProbeGrid::kTextureCount; ++i) {
            char name[16];
            std::snprintf(name, sizeof(name), "uProbe%d", i);
            p.setUniform(p.uniformLocation(name), probes::LightProbeGrid::kFirstTextureUnit + i);
        }
        p.setUniform(p.uniformLocation("uProbesEnabled"), probesOn ? 1 : 0);
        if (probesOn) {
            const fuhgl::math::Vec3 c = curProbes_->cell();
            p.setUniform("uProbeMin", curProbes_->min());
            p.setUniform("uProbeCellInv", fuhgl::math::Vec3{1.f / c.x, 1.f / c.y, 1.f / c.z});
            p.setUniform("uProbeMaxCoord",
                         fuhgl::math::Vec3{static_cast<float>(curProbes_->nx() - 1),
                                           static_cast<float>(curProbes_->ny() - 1),
                                           static_cast<float>(curProbes_->nz() - 1)});
        }
        p.setUniform("uProbeStrength", lightProbeStrength);
        p.setUniform("uInvViewProj", s.camera->viewProj(s.aspect).inverse());
        p.setUniform("uCamPos", s.camera->eye());
        p.setUniform("uAmbient", s.ambient);
        p.setUniform("uLightCount", static_cast<int>(std::min(s.lights.size(), static_cast<std::size_t>(fuhgl::render::kMaxLights))));
        p.setUniform("uSkyTop", skyTop);
        p.setUniform("uSkyBottom", skyBottom);
        p.setUniform("uFogColor", s.fogColor);
        p.setUniform("uFogDensity", s.fogDensity);
        p.setUniform("uSunDir", s.sunDirection);
        p.setUniform("uSunColor", s.sunColor);
        p.setUniform("uSunCos", s.sunCos);
        p.setUniform(p.uniformLocation("uEnvEnabled"), (s.env && effectEnabled(Effect::Ibl)) ? 1 : 0);
        p.setUniform("uPrefilterMaxLod", s.env ? static_cast<float>(s.env->prefilterMaxLod()) : 0.f);
        p.setUniform("uEnvIntensity", s.env ? s.env->intensity : 1.f);
        const bool csm = s.cascades && s.cascades->cascadeCount() > 0;
        if (s.shadowMap || csm) {
            p.setUniform(p.uniformLocation("uShadowEnabled"), 1);
            p.setUniform(p.uniformLocation("uShadowLightIndex"), s.shadowLightIndex);
            p.setUniform("uShadowStrength", s.shadowStrength);
            p.setUniform("uView", s.camera->view());
            if (csm) {
                p.setUniform(p.uniformLocation("uShadowMode"), 1);
                p.setUniform(p.uniformLocation("uCascadeCount"), s.cascades->cascadeCount());
                p.setUniform(p.uniformLocation("uCascadeSplits"), s.cascades->cascadeCount() + 1,
                             s.cascades->splits().data());
                p.setUniform(p.uniformLocation("uLightVPs"), s.cascades->cascadeCount(), &s.cascades->cascadeVP(0));
            } else {
                p.setUniform(p.uniformLocation("uShadowMode"), 0);
                p.setUniform("uLightVP", s.lightVP);
            }
        } else {
            p.setUniform(p.uniformLocation("uShadowEnabled"), 0);
            p.setUniform(p.uniformLocation("uShadowMode"), 0);
        }
        // --- F45 shadow-kit filter/params (GLSL: fuhgl::render::kSkFuncs) ---
        p.setUniform(p.uniformLocation("uShadowFilterMode"), static_cast<int>(shadowFilter));
        p.setUniform("uShadowBlend", shadowCascadeBlend);
        p.setUniform("uLightSize", shadowLightSize);
        p.setUniform("uShadowWorldWidth", shadowRadius_ * 2.f);
        p.setUniform("uMinVariance", shadowMinVariance);
        p.setUniform("uPcfRadius", shadowPcfRadius);
        p.setUniform("uShadowRotation", static_cast<float>(frameIndex_ % 64) * 0.7f);
        const float mapSize = csm ? static_cast<float>(s.cascades->size())
                                  : static_cast<float>(s.shadowMap ? s.shadowMap->size() : 1024);
        p.setUniform("uNormalOffset", shadowNormalOffset * (shadowRadius_ * 2.f / mapSize));
        // point-light cube shadow (F46)
        if (activePointShadowLightIndex_ >= 0 && activePointShadowLightIndex_ < static_cast<int>(s.lights.size())) {
            p.setUniform(p.uniformLocation("uPointShadows"), 1);
            p.setUniform(p.uniformLocation("uPointShadowLightIndex"), activePointShadowLightIndex_);
            p.setUniform("uPointShadowStrength", pointShadowStrength_);
            p.setUniform("uPointShadowRadius", shadowLightSize * 0.02f + 0.01f);
            p.setUniform("uLightPos", s.lights[static_cast<std::size_t>(activePointShadowLightIndex_)].position);
            p.setUniform("uShadowFar", pointShadowMap_.farPlane());
        } else {
            p.setUniform(p.uniformLocation("uPointShadows"), 0);
        }
        // contact shadows (F46): raymarch against the G-buffer depth in the lighting pass
        p.setUniform(p.uniformLocation("uContactShadows"), contactShadowsEnabled ? 1 : 0);
        p.setUniform("uContactShadowStrength", contactShadowStrength);
        p.setUniform("uContactShadowMaxDist", contactShadowMaxDist);
        p.setUniform("uContactShadowThickness", contactShadowThickness);
        p.setUniform(p.uniformLocation("uContactShadowSteps"), contactShadowSteps);
        p.setUniform("uProj", s.camera->projection(s.aspect));
        p.setUniform("uCsZNear", s.camera->zNear());
        p.setUniform("uCsZFar", s.camera->zFar());
    });
}

void DeferredRenderer::drawTaa(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdrP"); // lighting + G5 particles
    ctx.bindInput(1, "gMotion");
    ctx.bindInput(2, "taa");
    const bool taaOn = effectEnabled(Effect::Taa);
    const float alpha = (frameIndex_ == 0 || !taaOn) ? 1.f : taaBlend;
    drawFs(impl_->taaMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uCur"), 0);
        p.setUniform(p.uniformLocation("uMotion"), 1);
        p.setUniform(p.uniformLocation("uHist"), 2);
        p.setUniform("uAlpha", alpha);
        p.setUniform("uTexel", fuhgl::math::Vec2(1.f / static_cast<float>(internalWidth_),
                                                  1.f / static_cast<float>(internalHeight_)));
    });
}

void DeferredRenderer::drawUpscale(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "taa");
    const bool active = effectEnabled(Effect::Upscale) && renderScale_ < 1.f;
    drawFs(impl_->upscaleMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uSrc"), 0);
        p.setUniform("uSrcSize", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                   static_cast<float>(internalHeight_)));
        p.setUniform("uSharpen", upscaleSharpness);
        p.setUniform(p.uniformLocation("uMode"), active ? 1 : 0);
    });
}

void DeferredRenderer::drawTonemap(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "up");
    ctx.bindInput(1, "gDepth"); // for the god-ray occlusion
    fuhgl::Framebuffer& out = impl_->output;
    out.bind();
    const fuhgl::gl::GLenum buf[1] = {fuhgl::gl::GL_COLOR_ATTACHMENT0};
    fuhgl::gl::drawBuffers(1, buf);
    fuhgl::gl::viewport(0, 0, width_, height_);
    // sun screen position for the god rays (behind the camera -> rays off)
    float rayStrength = 0.f;
    fuhgl::math::Vec2 sunScreen{-10.f, -10.f};
    if (godRays.enabled && curScene_ && curScene_->camera) {
        const fuhgl::render::SceneView& s = *curScene_;
        const fuhgl::math::Vec4 clip =
            s.camera->viewProj(s.aspect) * fuhgl::math::Vec4{s.sunDirection.x, s.sunDirection.y, s.sunDirection.z, 0.f};
        if (clip.w > 0.f) {
            sunScreen = {clip.x / clip.w * 0.5f + 0.5f, clip.y / clip.w * 0.5f + 0.5f};
            rayStrength = godRays.strength;
        }
    }
    drawFs(impl_->tonemapMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uHdr"), 0);
        p.setUniform(p.uniformLocation("uGDepth"), 1);
        p.setUniform("uExposure", exposure);
        p.setUniform(p.uniformLocation("uUseTonemap"), effectEnabled(Effect::Tonemap) ? 1 : 0);
        p.setUniform("uSunScreen", sunScreen);
        p.setUniform("uRayStrength", rayStrength);
        p.setUniform("uRayDecay", godRays.decay);
        p.setUniform("uRaySamples", godRays.samples);
    });
    fuhgl::Framebuffer::unbind();
}

// --- accessors -----------------------------------------------------------------------
fuhgl::Framebuffer& DeferredRenderer::output() { return impl_->output; }
const fuhgl::Texture& DeferredRenderer::texAlbedo() const { return msaa_ > 0 ? impl_->rAlbedo : impl_->texAlbedo; }
const fuhgl::Texture& DeferredRenderer::texNormal() const { return msaa_ > 0 ? impl_->rNormal : impl_->texNormal; }
const fuhgl::Texture& DeferredRenderer::texPbr() const { return msaa_ > 0 ? impl_->rPbr : impl_->texPbr; }
const fuhgl::Texture& DeferredRenderer::texEmissive() const { return msaa_ > 0 ? impl_->rEmissive : impl_->texEmissive; }
const fuhgl::Texture& DeferredRenderer::texMotion() const { return msaa_ > 0 ? impl_->rMotion : impl_->texMotion; }
const fuhgl::Texture& DeferredRenderer::texMaterialId() const { return msaa_ > 0 ? impl_->rMatId : impl_->texMatId; }
const fuhgl::Texture& DeferredRenderer::texDepth() const { return impl_->texDepth; }
const fuhgl::Texture& DeferredRenderer::hdrTexture() const { return *impl_->post.texture("hdr"); }
const fuhgl::Texture& DeferredRenderer::reflectionTexture() const { return impl_->reflColor; }
fuhgl::render::RenderGraph& DeferredRenderer::postGraph() { return impl_->post; }

void DeferredRenderer::setWaterReflectionScale(float scale) {
    const float s = scale < 0.1f ? 0.1f : (scale > 1.f ? 1.f : scale);
    if (s == waterReflectionScale || width_ <= 0) {
        waterReflectionScale = s;
        return;
    }
    waterReflectionScale = s;
    frameIndex_ = 0;
    prevViewProj_ = fuhgl::math::Mat4{};
    createInternal();
}

// --- effects (F39/F40-style gating) ---------------------------------------------------
void DeferredRenderer::gate(Effect e, bool& flag) {
    if (flag && !effectSupported(e)) {
        const unsigned bit = 1u << static_cast<int>(e);
        if (!(warnedMask_ & bit)) {
            warnUnsupported("fuhglgraphics::DeferredRenderer", e, glMajor_, glMinor_);
            warnedMask_ |= bit;
        }
        flag = false;
    }
}

void DeferredRenderer::applyCapabilities() {
    gate(Effect::Taa, taaEnabled_);
    gate(Effect::Upscale, upscaleEnabled_);
    gate(Effect::Tonemap, tonemapEnabled_);
    gate(Effect::Shadows, shadows_);
    gate(Effect::CsmShadows, csmEnabled_);
    gate(Effect::Ibl, iblEnabled_);
    gate(Effect::PointShadows, pointShadows_);
    gate(Effect::ContactShadows, contactShadowsEnabled);
    gate(Effect::Particles, particlesEnabled_);
    gate(Effect::LightProbes, lightProbesEnabled_);
    gate(Effect::Water, waterEnabled_);
}

void DeferredRenderer::setEffectEnabled(Effect e, bool on) {
    switch (e) {
        case Effect::Taa: taaEnabled_ = on; break;
        case Effect::Upscale: upscaleEnabled_ = on; break;
        case Effect::Tonemap: tonemapEnabled_ = on; break;
        case Effect::Ibl: iblEnabled_ = on; break;
        case Effect::Shadows: shadows_ = on; break;
        case Effect::CsmShadows: csmEnabled_ = on; break;
        case Effect::PointShadows:
            // the cube map itself is allocated by enablePointShadow() (needs a GL context)
            pointShadows_ = on;
            break;
        case Effect::ContactShadows: contactShadowsEnabled = on; break;
        case Effect::Particles: particlesEnabled_ = on; break;
        case Effect::LightProbes: lightProbesEnabled_ = on; break;
        case Effect::Water: waterEnabled_ = on; break;
        case Effect::Msaa:
            // MSAA is a create() parameter (multisampled G-buffer), not runtime-toggleable
            if (on != (msaa_ > 0))
                std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: MSAA is set via create(w, h, msaaSamples)\n");
            break;
    }
    if (on && e != Effect::Msaa && !effectSupported(e) && !((warnedMask_ >> static_cast<int>(e)) & 1u)) {
        warnUnsupported("fuhglgraphics::DeferredRenderer", e, glMajor_, glMinor_);
        warnedMask_ |= 1u << static_cast<int>(e);
    }
    applyCapabilities();
}

bool DeferredRenderer::effectEnabled(Effect e) const {
    if (!effectSupported(e)) return false;
    switch (e) {
        case Effect::Taa: return taaEnabled_;
        case Effect::Upscale: return upscaleEnabled_;
        case Effect::Tonemap: return tonemapEnabled_;
        case Effect::Msaa: return msaa_ > 0;
        case Effect::Shadows: return shadows_;
        case Effect::CsmShadows: return csmEnabled_;
        case Effect::Ibl: return iblEnabled_;
        case Effect::PointShadows: return pointShadows_;
        case Effect::ContactShadows: return contactShadowsEnabled;
        case Effect::Particles: return particlesEnabled_;
        case Effect::LightProbes: return lightProbesEnabled_;
        case Effect::Water: return waterEnabled_;
    }
    return false;
}

void DeferredRenderer::setAllEffects(bool on) {
    taaEnabled_ = upscaleEnabled_ = tonemapEnabled_ = iblEnabled_ = shadows_ = csmEnabled_ = on;
    pointShadows_ = contactShadowsEnabled = particlesEnabled_ = lightProbesEnabled_ = waterEnabled_ = on;
    applyCapabilities();
}

// --- shadows (on the FuhGLCore F45 shadow kit) -----------------------------------------
void DeferredRenderer::enableShadows(int mapSize) {
    if (mapSize <= 0) return;
    shadowSize_ = mapSize;
    impl_->shadowMap.create(mapSize, shadowFilter == fuhgl::render::ShadowFilter::Vsm);
    shadows_ = true;
}

void DeferredRenderer::enableCsmShadows(int mapSize, int cascades) {
    if (mapSize <= 0 || cascades <= 0) return;
    csmSize_ = mapSize;
    csmCascades_ = cascades;
    impl_->csm.create(mapSize, cascades, shadowFilter == fuhgl::render::ShadowFilter::Vsm);
    impl_->csm.snapToTexels = shadowSnapToTexels;
    csmEnabled_ = true;
}

void DeferredRenderer::setShadowFilter(fuhgl::render::ShadowFilter f) {
    if (f == shadowFilter) return;
    const bool hadMoments = shadowFilter == fuhgl::render::ShadowFilter::Vsm;
    const bool wantMoments = f == fuhgl::render::ShadowFilter::Vsm;
    shadowFilter = f;
    // VSM needs the RGBA32F moments attachment — recreate the targets when it toggles
    if (hadMoments != wantMoments && impl_) {
        if (shadows_ && shadowSize_ > 0) impl_->shadowMap.create(shadowSize_, wantMoments);
        if (csmEnabled_ && csmSize_ > 0) impl_->csm.create(csmSize_, csmCascades_, wantMoments);
        if (pointShadows_ && pointShadowSize_ > 0) pointShadowMap_.create(pointShadowSize_, wantMoments);
    }
}

void DeferredRenderer::enablePointShadow(int lightIndex, int mapSize) {
    if (mapSize <= 0) return;
    pointShadowLightIndex_ = lightIndex;
    pointShadowSize_ = mapSize;
    pointShadowMap_.create(mapSize, shadowFilter == fuhgl::render::ShadowFilter::Vsm);
    pointShadows_ = true;
}

} // namespace fuhglgraphics::render
