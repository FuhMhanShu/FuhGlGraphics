#include "fuhglgraphics/render/deferred_renderer.hpp"

#include <fuhglgraphics/anim/gpu_skinner.hpp>
#include <fuhglgraphics/render/bloom.hpp>
#include <fuhglgraphics/render/ssao.hpp>
#include <fuhglgraphics/render/ssr.hpp>
#include <fuhglgraphics/particles/particle_system.hpp>
#include <fuhglgraphics/probes/light_probes.hpp>
#include <fuhglgraphics/water/water_surface.hpp>

#include "deferred_glsl.hpp"

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render/lod.hpp>
#include <fuhgl/render/shadow_glsl.hpp>

#include <algorithm>
#include <chrono>
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

    fuhgl::render::Material gbufferMat, gbufferSkinMat, lightingMat, ssaoMat, hbaoMat, ssrMat, bloomMat, flareMat, volumetricMat, taaMat, upscaleMat, dofMat, tonemapMat, copyMat, decalMat, giMat, caMat;
    std::vector<fuhgl::math::Vec3> ssaoKernel; ///< G15: generated once per createInternal
    bool skinProgramReady = false; ///< G14: the GLSL 430 skinning G-buffer program (GL >= 4.3)
    fuhgl::render::Mesh decalBox; ///< G8: unit box for the decal projection pass
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
    // G14: skinning variant of the G-buffer pass (GLSL 430 SSBO palette) on GL >= 4.3;
    // when it fails to compile the renderer silently falls back to CPU skinning (G6)
    im.skinProgramReady = false;
    if (glMajor_ > 4 || (glMajor_ == 4 && glMinor_ >= 3)) {
        if (!im.gbufferSkinMat.create()) return false;
        fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
        if (vs.compile(kGBufferVertSkin) && fs.compile(kGBufferFrag)) {
            im.gbufferSkinMat.program().attach(vs);
            im.gbufferSkinMat.program().attach(fs);
            im.skinProgramReady = im.gbufferSkinMat.program().link();
            if (!im.skinProgramReady)
                std::fprintf(stderr, "DeferredRenderer: gbuffer skin link: %s\n",
                             im.gbufferSkinMat.program().infoLog().c_str());
        } else {
            std::fprintf(stderr, "DeferredRenderer: gbuffer skin VS: %s\n", vs.infoLog().c_str());
        }
    }
    // G8 decal pass (projects into gAlbedo/gNormal between geometry and lighting)
    if (!im.decalMat.create()) return false;
    {
        fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
        if (!vs.compile(kDecalVert) || !fs.compile(kDecalFrag)) {
            std::fprintf(stderr, "DeferredRenderer: decal shader: %s%s\n", vs.infoLog().c_str(), fs.infoLog().c_str());
            return false;
        }
        im.decalMat.program().attach(vs);
        im.decalMat.program().attach(fs);
        if (!im.decalMat.program().link()) {
            std::fprintf(stderr, "DeferredRenderer: decal link: %s\n", im.decalMat.program().infoLog().c_str());
            return false;
        }
    }
    if (!im.decalBox.create()) return false;
    im.decalBox.setMeshData(fuhgl::render::shapes::boxData({1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}));
    const std::string lightFrag = std::string(kLightFragHead) + kLightFragKit +
                                  fuhgl::render::kSkFuncs + kLightFragProbes + kLightFragSsao + kLightFragWetness + kLightFragWeathering + kLightFragPbr + kLightFragTail;
    im.ssaoKernel = ssaoKernel(std::min(ssaoSamples > 0 ? ssaoSamples : 24, 64), 1337u);
    if (!compilePostFs(im.lightingMat, lightFrag, "lighting") ||
        !compilePostFs(im.ssaoMat, kSsaoFrag, "ssao") ||
        !compilePostFs(im.hbaoMat, kHbaoFrag, "hbao") ||
        !compilePostFs(im.ssrMat, kSsrFrag, "ssr") ||
        !compilePostFs(im.bloomMat, kBloomFrag, "bloom") ||
        !compilePostFs(im.flareMat, kFlareFrag, "flare") ||
        !compilePostFs(im.giMat, kGiFrag, "gi") ||
        !compilePostFs(im.caMat, kCaFrag, "ca") ||
        !compilePostFs(im.volumetricMat, kVolumetricFrag, "volumetric") ||
        !compilePostFs(im.taaMat, kTaaFrag, "taa") ||
        !compilePostFs(im.upscaleMat, kUpscaleFrag, "upscale") ||
        !compilePostFs(im.dofMat, kDofFrag, "dof") ||
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
    hud_.create(); // G10 HUD (best effort — the hud pass checks isCreated())
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
    im.post.addResource("ssao", iw, ih, fuhgl::gl::GL_RG16F); // G15 AO factor in .r
    im.post.addResource("hbao", iw, ih, fuhgl::gl::GL_RG16F); // G31 horizon-based AO factor in .r
    im.post.addResource("hdr", iw, ih, fuhgl::gl::GL_RGBA16F);
    im.post.addResource("hdrG", iw, ih, fuhgl::gl::GL_RGBA16F); // G34: hdr + screen-space GI
    im.post.addResource("hdrS", iw, ih, fuhgl::gl::GL_RGBA16F); // G16: hdrG + screen-space reflections
    im.post.addResource("hdrW", iw, ih, fuhgl::gl::GL_RGBA16F); // hdr + G13 water
    im.post.addResource("hdrP", iw, ih, fuhgl::gl::GL_RGBA16F); // hdrW + G5 particles
    im.post.addResource("hdrV", iw, ih, fuhgl::gl::GL_RGBA16F); // G21: hdrP + volumetric fog
    im.post.addResource("hdrB", iw, ih, fuhgl::gl::GL_RGBA16F); // G17: hdrV + bloom
    im.post.addResource("hdrF", iw, ih, fuhgl::gl::GL_RGBA16F); // G30: hdrB + lens flare/anamorphic
    im.post.addResource("taa", iw, ih, fuhgl::gl::GL_RGBA16F);
    im.post.addResource("up", width_, height_, fuhgl::gl::GL_RGBA16F);
    im.post.addResource("ca", width_, height_, fuhgl::gl::GL_RGBA16F);  // G32 chromatic aberration at output res
    im.post.addResource("dof", width_, height_, fuhgl::gl::GL_RGBA16F); // G10 DoF at output res
    im.post.addPass("ssao", {"gNormal", "gDepth"}, {"ssao"}, // G15, runs before lighting
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawSsao(ctx); });
    im.post.addPass("hbao", {"gNormal", "gDepth"}, {"hbao"}, // G31, runs before lighting (next to SSAO)
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawHbao(ctx); });
    im.post.addPass("lighting", {"gAlbedo", "gNormal", "gPbr", "gEmissive", "gDepth", "ssao", "hbao"}, {"hdr"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawLighting(ctx); });
    im.post.addPass("gi", {"hdr", "gAlbedo", "gNormal", "gDepth"}, {"hdrG"}, // G34, after lighting
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawGi(ctx); });
    im.post.addPass("ssr", {"hdrG", "gNormal", "gDepth", "gPbr"}, {"hdrS"}, // G16, after GI
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawSsr(ctx); });
    im.post.addPass("water", {"hdrS", "gDepth"}, {"hdrW"}, // G13 water shades the SSR result
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawWater(ctx); });
    im.post.addPass("particles", {"hdrW", "gDepth"}, {"hdrP"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawParticles(ctx); });
    im.post.addPass("volumetric", {"hdrP", "gDepth"}, {"hdrV"}, // G21, after particles, before bloom
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawVolumetric(ctx); });
    im.post.addPass("bloom", {"hdrV"}, {"hdrB"}, // G17, after volumetric fog, before TAA
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawBloom(ctx); });
    im.post.addPass("flare", {"hdrB"}, {"hdrF"}, // G30 lens flare + anamorphic, after bloom, before TAA
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawFlare(ctx); });
    im.post.addPass("taa", {"hdrF", "gMotion", "taa"}, {"taa"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawTaa(ctx); }, /*feedback=*/true);
    im.post.addPass("upscale", {"taa"}, {"up"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawUpscale(ctx); });
    im.post.addPass("ca", {"up"}, {"ca"}, // G32 chromatic aberration, after upscale, before DoF
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawCa(ctx); });
    im.post.addPass("dof", {"ca", "gDepth"}, {"dof"},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawDof(ctx); });
    im.post.addPass("tonemap", {"dof", "gDepth"}, {},
                    [this](fuhgl::render::RenderGraph::PassContext& ctx) { drawTonemap(ctx); });
    // NOTE: the G10 HUD is NOT a graph pass — resolvePassOrder() is LIFO for independent
    // passes, so a no-output pass would run before tonemap and be overwritten. render()
    // draws it directly after post.execute() instead.
    if (!im.post.compile()) {
        std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: post graph compile failed\n");
        return false;
    }
    return true;
}

void DeferredRenderer::renderGBuffer(const fuhgl::render::SceneView& scene,
                                     const std::vector<std::uint32_t>* materialIds,
                                     const std::vector<const anim::GpuSkinner*>* skins,
                                     const std::vector<float>* subsurface) {
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
    prog.setUniform("uCamPos", scene.camera->eye()); // POM view ray
    prog.setUniform("uResolution", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                      static_cast<float>(internalHeight_)));
    // per-item uniforms, shared by the plain and the G14 skinning program
    auto setItemUniforms = [&](fuhgl::Program& p, const fuhgl::render::DrawItem& item, std::size_t i,
                               int instanced) {
        p.setUniform("uModel", item.model);
        p.setUniform("uInstanced", instanced);
        p.setUniform("uMaterialId", materialIds && i < materialIds->size() ? (*materialIds)[i] : 0u);
        const fuhgl::render::Material* preset =
            (item.material && item.material->builtinShader) ? item.material : nullptr;
        if (preset) {
            p.setUniform("uAlbedo", preset->albedo);
            p.setUniform("uEmissive", preset->emissive);
            p.setUniform("uUseTex", preset->albedoTexture ? 1 : 0);
            if (preset->albedoTexture) {
                fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitAlbedo);
                f.glBindTexture(static_cast<fuhgl::gl::GLenum>(preset->albedoTexture->target()),
                                preset->albedoTexture->id());
                p.setUniform("uMatTex", fuhgl::render::kTextureUnitAlbedo);
            }
            p.setUniform("uUseNormalMap", preset->normalMap ? 1 : 0);
            if (preset->normalMap) {
                fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitNormalMap);
                f.glBindTexture(static_cast<fuhgl::gl::GLenum>(preset->normalMap->target()),
                                preset->normalMap->id());
                p.setUniform("uNormalTex", fuhgl::render::kTextureUnitNormalMap);
            }
            // POM: parallax height map on unit kTextureUnitHeightMap (5)
            p.setUniform("uUseHeightMap", preset->heightMap ? 1 : 0);
            if (preset->heightMap) {
                fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitHeightMap);
                f.glBindTexture(static_cast<fuhgl::gl::GLenum>(preset->heightMap->target()),
                                preset->heightMap->id());
                p.setUniform("uHeightTex", fuhgl::render::kTextureUnitHeightMap);
                p.setUniform("uParallaxScale", preset->parallaxScale);
                p.setUniform("uParallaxLayers", preset->parallaxLayers);
            } else {
                p.setUniform("uParallaxScale", 0.f);
                p.setUniform("uParallaxLayers", 1);
            }
            p.setUniform("uMetallic", preset->metallic);
            p.setUniform("uRoughness", preset->roughness);
            p.setUniform("uAo", preset->ao);
            // G33: per-item subsurface factor -> oPbr.a (parallel array, 0 when absent)
            p.setUniform("uSss", subsurface && i < subsurface->size() ? (*subsurface)[i] : 0.f);
        } else {
            p.setUniform("uAlbedo", fuhgl::math::Vec3{1.f, 1.f, 1.f});
            p.setUniform("uEmissive", fuhgl::math::Vec3{0.f, 0.f, 0.f});
            p.setUniform("uUseTex", 0);
            p.setUniform("uUseNormalMap", 0);
            p.setUniform("uUseHeightMap", 0);
            p.setUniform("uSss", 0.f);
            p.setUniform("uMetallic", 0.f);
            p.setUniform("uRoughness", 0.5f);
            p.setUniform("uAo", 1.f);
        }
    };
    for (std::size_t i = 0; i < scene.items.size(); ++i) {
        const fuhgl::render::DrawItem& item = scene.items[i];
        if (curWater_ && item.mesh == &curWater_->mesh()) continue; // G13: water leaves the G-buffer
        int lodLevel = -1;
        const fuhgl::render::Mesh* mesh =
            fuhgl::render::resolveLodMesh(item, scene.camera->eye(), scene.lodEnabled, &lodLevel);
        if (!mesh) continue;
        if (lodLevel >= 0) scene.lastLodLevel = lodLevel;
        // G14: skinned item -> skinning program (GLSL 430 palette SSBO) when enabled+supported
        const anim::GpuSkinner* skin =
            (skins && i < skins->size()) ? (*skins)[i] : nullptr;
        if (skin && skin->isReady() && im.skinProgramReady && effectEnabled(Effect::GpuSkinning)) {
            fuhgl::Program& sp = im.gbufferSkinMat.program();
            sp.use();
            sp.setUniform("uViewProj", viewProj);
            sp.setUniform("uPrevViewProj", prevViewProj);
            sp.setUniform("uCamPos", scene.camera->eye()); // POM view ray
            sp.setUniform("uResolution", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                           static_cast<float>(internalHeight_)));
            setItemUniforms(sp, item, i, 0);
            skin->bindPalette();
            skin->draw();
            prog.use(); // restore the plain program for the following items
            prog.setUniform("uViewProj", viewProj);
            prog.setUniform("uPrevViewProj", prevViewProj);
            prog.setUniform("uResolution", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                              static_cast<float>(internalHeight_)));
            continue;
        }
        setItemUniforms(prog, item, i, mesh->instanceCount() > 0 ? 1 : 0);
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

    // --- G8 decals: project into gAlbedo/gNormal (after the MSAA resolve when MSAA) ---
    if (effectEnabled(Effect::Decals) && curDecals_ && !curDecals_->empty()) drawDecals(scene);
}

// G8 decal pass: draw each decal box (depth-tested, front faces) and let the decal shader
// rewrite attachments 0 (albedo) + 1 (normal) where the G-buffer surface lies inside the
// box and matches the decal's material-ID mask. Sorted back-to-front, capped at
// kMaxDecals; with direct overwrite the nearest decal wins.
void DeferredRenderer::drawDecals(const fuhgl::render::SceneView& scene) {
    Impl& im = *impl_;
    auto& f = fuhgl::gl::functions();
    std::vector<const Decal*> sorted;
    sorted.reserve(curDecals_->size());
    for (const Decal& d : *curDecals_) sorted.push_back(&d);
    const fuhgl::math::Vec3 eye = scene.camera->eye();
    std::sort(sorted.begin(), sorted.end(), [&](const Decal* a, const Decal* b) {
        const fuhgl::math::Vec4 ca = a->world * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
        const fuhgl::math::Vec4 cb = b->world * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
        const fuhgl::math::Vec3 da{ca.x - eye.x, ca.y - eye.y, ca.z - eye.z};
        const fuhgl::math::Vec3 db{cb.x - eye.x, cb.y - eye.y, cb.z - eye.z};
        return fuhgl::math::lengthSq(da) > fuhgl::math::lengthSq(db); // farthest first
    });
    if (sorted.size() > static_cast<std::size_t>(kMaxDecals)) {
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "fuhglgraphics::DeferredRenderer: %zu decals > kMaxDecals=%d, farthest dropped\n",
                         sorted.size(), kMaxDecals);
            warned = true;
        }
        sorted.resize(static_cast<std::size_t>(kMaxDecals));
    }


    // target: the resolved FBO when MSAA (samplers need non-multisample textures), else the G-buffer
    fuhgl::Framebuffer& target = msaa_ > 0 ? im.resolve : im.gbuffer;
    const fuhgl::Texture& gAlbedo = msaa_ > 0 ? im.rAlbedo : im.texAlbedo;
    const fuhgl::Texture& gNormal = msaa_ > 0 ? im.rNormal : im.texNormal;
    const fuhgl::Texture& gMatId = msaa_ > 0 ? im.rMatId : im.texMatId;
    target.bind();
    const fuhgl::gl::GLenum decalBufs[2] = {fuhgl::gl::GL_COLOR_ATTACHMENT0, fuhgl::gl::GL_COLOR_ATTACHMENT1};
    fuhgl::gl::drawBuffers(2, decalBufs);
    fuhgl::gl::viewport(0, 0, internalWidth_, internalHeight_);
    fuhgl::gl::depthMask(false); // decals must not write depth
    fuhgl::gl::disable(fuhgl::gl::Cap::Blend); // the shader outputs the already-mixed color

    // G-buffer samplers for the decal shader
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + 0);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, gAlbedo.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + 1);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, gNormal.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + 5);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, im.texDepth.id());
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + 6);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, gMatId.id());

    fuhgl::Program& p = im.decalMat.program();
    p.use();
    p.setUniform("uViewProj", fuhgl::math::jitterViewProj(scene.camera->viewProj(scene.aspect), scene.jitter));
    p.setUniform("uInvViewProj", scene.camera->viewProj(scene.aspect).inverse());
    p.setUniform("uResolution", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                  static_cast<float>(internalHeight_)));
    p.setUniform(p.uniformLocation("uGAlbedo"), 0);
    p.setUniform(p.uniformLocation("uGNormal"), 1);
    p.setUniform(p.uniformLocation("uDecalTex"), fuhgl::render::kTextureUnitAlbedo);
    p.setUniform(p.uniformLocation("uDecalNormal"), fuhgl::render::kTextureUnitNormalMap);
    p.setUniform(p.uniformLocation("uGDepth"), 5);
    p.setUniform(p.uniformLocation("uGMatId"), 6);
    for (const Decal* d : sorted) {
        p.setUniform("uDecalWorld", d->world);
        p.setUniform("uInvDecalWorld", d->world.inverse());
        p.setUniform("uHalfExtents", d->halfExtents);
        p.setUniform("uAlbedo", d->albedo);
        p.setUniform("uOpacity", d->opacity);
        p.setUniform(p.uniformLocation("uMatIdMask"), d->materialIdMask);
        p.setUniform(p.uniformLocation("uUseTex"), d->albedoTexture ? 1 : 0);
        if (d->albedoTexture) {
            fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitAlbedo);
            f.glBindTexture(static_cast<fuhgl::gl::GLenum>(d->albedoTexture->target()), d->albedoTexture->id());
        }
        p.setUniform(p.uniformLocation("uUseNormalMap"), d->normalMap ? 1 : 0);
        if (d->normalMap) {
            fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + fuhgl::render::kTextureUnitNormalMap);
            f.glBindTexture(static_cast<fuhgl::gl::GLenum>(d->normalMap->target()), d->normalMap->id());
        }
        im.decalBox.draw();
    }

    // restore the G-buffer state (all 6 attachments + depth writes) for the next passes
    const fuhgl::gl::GLenum allBufs[6] = {fuhgl::gl::GL_COLOR_ATTACHMENT0, fuhgl::gl::GL_COLOR_ATTACHMENT1,
                                           fuhgl::gl::GL_COLOR_ATTACHMENT2, fuhgl::gl::GL_COLOR_ATTACHMENT3,
                                           fuhgl::gl::GL_COLOR_ATTACHMENT4, fuhgl::gl::GL_COLOR_ATTACHMENT5};
    fuhgl::gl::drawBuffers(6, allBufs);
    fuhgl::gl::depthMask(true);
    fuhgl::Framebuffer::unbind();
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
                              const std::vector<Decal>* decals,
                              const water::WaterSurface* water,
                              const std::vector<const anim::GpuSkinner*>* skins,
                              const std::vector<float>* subsurface) {
    if (!scene.camera || width_ <= 0) return;
    const auto t0 = std::chrono::steady_clock::now();
    fuhgl::render::SceneView s = scene;
    applyCapabilities();
    curParticles_ = particleSystems;
    curProbes_ = lightProbes;
    curDecals_ = decals;
    curSkins_ = skins; // G14

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

    // per-item shadow casting (G9): the depth pass only draws items flagged castShadow
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
    renderGBuffer(s, materialIds, skins, subsurface); // G14 skins + G33 SSS factors forwarded to the G-buffer pass
    scene.lastLodLevel = s.lastLodLevel;  // G20: propagate the resolved LOD level (F41) to the caller
    // remember this frame's (jittered) viewProj for next frame's motion vectors
    prevViewProj_ = fuhgl::math::jitterViewProj(s.camera->viewProj(s.aspect), s.jitter);
    // G32/G34: skip the ca/gi passes entirely when off (the consumers then read the
    // upstream resource directly) — a fullscreen copy pass still costs frame time
    impl_->post.setPassEnabled("ca", effectEnabled(Effect::ChromaticAberration));
    impl_->post.setPassEnabled("gi", effectEnabled(Effect::Gi));
    impl_->post.execute();
    // G10 HUD: drawn after the post graph (into the output, over the tonemapped frame)
    if (effectEnabled(Effect::DebugHud) && hud_.isCreated()) drawHud();
    if (taaOn) ++frameIndex_;
    const auto t1 = std::chrono::steady_clock::now();
    lastFrameMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();
    hud_.frame(std::chrono::duration<double>(t1.time_since_epoch()).count());
    curScene_ = nullptr;
    curParticles_ = nullptr;
    curProbes_ = nullptr;
    curDecals_ = nullptr;
    curSkins_ = nullptr;
    curWater_ = nullptr;
}

// G5 particles post pass: copy hdr -> hdrP, then draw each system's instanced billboards
// (blended, depth-tested against the G-buffer depth) into hdrP.
// G13 water post pass: hdrS -> hdrW. Always starts from the lighting/SSR result; with an active
// water surface the mesh is shaded on top (Gerstner waves + planar reflection + refraction +
// foam). The post FBO has no depth attachment, so the fragment shader does the occlusion
// test manually against the G-buffer depth (uGDepth).
void DeferredRenderer::drawWater(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdrS"); // G16: lighting + screen-space reflections
    ctx.bindInput(1, "gDepth");
    Impl& im = *impl_;
    drawFs(im.copyMat, [&](fuhgl::Program& p) { p.setUniform(p.uniformLocation("uSrc"), 0); });
    if (!curWater_ || !curScene_) return;
    auto& f = fuhgl::gl::functions();
    const fuhgl::render::SceneView& s = *curScene_;
    const water::WaterSurface& w = *curWater_;
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + 2);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_2D, im.reflColor.id());
    // sky cubemap for the sharp sky-mirror term in the water shader (unit kTextureUnitSkyCube)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0 + kTextureUnitSkyCube);
    f.glBindTexture(fuhgl::gl::GL_TEXTURE_CUBE_MAP, skyCube_ ? skyCube_->id() : im.dummyCube.id());
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
    p.setUniform(p.uniformLocation("uSkyCube"), kTextureUnitSkyCube);
    p.setUniform(p.uniformLocation("uUseSkyCube"), skyCube_ ? 1 : 0);
    p.setUniform("uSkyTop", skyTop);
    p.setUniform("uSkyBottom", skyBottom);
    p.setUniform("uSunCos", s.sunCos);
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

// G15 SSAO pass: G-buffer normal + depth -> AO factor (resource "ssao", R16F). Runs before
// the lighting pass; writes 1.0 everywhere when Effect::Ssao is off so the lighting pass
// can sample the resource unconditionally.
void DeferredRenderer::drawSsao(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "gNormal");
    ctx.bindInput(1, "gDepth");
    const fuhgl::render::SceneView& s = *curScene_;
    Impl& im = *impl_;
    const bool on = effectEnabled(Effect::Ssao);
    const int samples = std::min(std::max(ssaoSamples, 1), static_cast<int>(im.ssaoKernel.size()));
    drawFs(im.ssaoMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uGNormal"), 0);
        p.setUniform(p.uniformLocation("uGDepth"), 1);
        p.setUniform(p.uniformLocation("uSsaoEnabled"), on ? 1 : 0);
        p.setUniform("uInvViewProj", s.camera->viewProj(s.aspect).inverse());
        p.setUniform("uViewProj", s.camera->viewProj(s.aspect)); // non-jittered
        p.setUniform("uResolution", fuhgl::math::Vec2(static_cast<float>(internalWidth_),
                                                       static_cast<float>(internalHeight_)));
        const int ksize = std::min(static_cast<int>(im.ssaoKernel.size()), 64);
        for (int i = 0; i < ksize; ++i) {
            char name[16];
            std::snprintf(name, sizeof(name), "uKernel[%d]", i);
            p.setUniform(name, im.ssaoKernel[static_cast<std::size_t>(i)]);
        }
        p.setUniform(p.uniformLocation("uKernelSize"), ksize);
        p.setUniform("uRadius", ssaoRadius);
        p.setUniform("uBias", ssaoBias);
        p.setUniform("uIntensity", ssaoIntensity);
        p.setUniform(p.uniformLocation("uSamples"), samples);
        p.setUniform("uRotSeed", static_cast<float>(frameIndex_ % 64) * 0.7f); // per-frame rotation
        p.setUniform("uZNear", s.camera->zNear());
        p.setUniform("uZFar", s.camera->zFar());
    });
}

// G31 HBAO pass: G-buffer normal + depth -> horizon-based AO factor (resource "hbao",
// RG16F). Runs before lighting next to G15 SSAO; writes 1.0 everywhere when Effect::Hbao is
// off so the lighting pass can sample the resource unconditionally. March mirrors the CPU
// twin render/hbao.hpp.
void DeferredRenderer::drawHbao(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "gNormal");
    ctx.bindInput(1, "gDepth");
    const fuhgl::render::SceneView& s = *curScene_;
    const bool on = effectEnabled(Effect::Hbao);
    drawFs(impl_->hbaoMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uGNormal"), 0);
        p.setUniform(p.uniformLocation("uGDepth"), 1);
        p.setUniform(p.uniformLocation("uHbaoEnabled"), on ? 1 : 0);
        p.setUniform("uInvViewProj", s.camera->viewProj(s.aspect).inverse());
        p.setUniform("uViewProj", s.camera->viewProj(s.aspect)); // non-jittered
        p.setUniform("uRadius", hbaoRadius);
        p.setUniform("uBias", hbaoBias);
        p.setUniform(p.uniformLocation("uDirs"), std::min(std::max(hbaoDirections, 1), 16));
        p.setUniform(p.uniformLocation("uSteps"), std::min(std::max(hbaoSteps, 1), 8));
        p.setUniform("uPower", hbaoPower);
        p.setUniform("uRotSeed", static_cast<float>(frameIndex_ % 64) * 0.7f); // per-frame rotation
    });
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
    ctx.bindInput(6, "ssao"); // G15
    ctx.bindInput(24, "hbao"); // G31 (unit 24: sky cube = 16, probes = 17..23)
    ctx.bindInput(25, "gMatId"); // G31 weathering: material-ID gate (unit 25 is free here)
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
        // G15 SSAO (unit 6)
        p.setUniform(p.uniformLocation("uSsao"), 6);
        p.setUniform(p.uniformLocation("uSsaoEnabled"), effectEnabled(Effect::Ssao) ? 1 : 0);
        p.setUniform(p.uniformLocation("uHbao"), 24); // G31
        p.setUniform(p.uniformLocation("uHbaoEnabled"), effectEnabled(Effect::Hbao) ? 1 : 0);
        // G33 subsurface scattering
        p.setUniform(p.uniformLocation("uSssEnabled"), effectEnabled(Effect::Subsurface) ? 1 : 0);
        p.setUniform("uSssDistortion", sssDistortion);
        p.setUniform("uSssPower", sssPower);
        p.setUniform("uSssScale", sssScale);
        p.setUniform("uSssThickness", sssThickness);
        // G22 wetness
        p.setUniform(p.uniformLocation("uWetnessEnabled"), effectEnabled(Effect::Wetness) ? 1 : 0);
        p.setUniform("uWetness", wetnessAmount);
        p.setUniform("uWetnessPuddle", wetnessPuddle);
        p.setUniform("uWetnessNoiseScale", wetnessNoiseScale);
        // G31 weathering
        p.setUniform(p.uniformLocation("uGMatId"), 25);
        p.setUniform(p.uniformLocation("uWeatheringEnabled"), effectEnabled(Effect::Weathering) ? 1 : 0);
        p.setUniform("uWeatherStrength", weatherStrength);
        p.setUniform("uWeatherHumidity", weatherHumidity);
        p.setUniform("uWeatherBaseY", weatherBaseY);
        p.setUniform("uWeatherRainStreaks", weatherRainStreaks);
        p.setUniform("uWeatherSoil", weatherSoilColor);
        p.setUniform("uWeatherMoss", weatherMossColor);
        p.setUniform("uWeatherBrick", weatherBrickColor);
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

// G16 SSR pass: raymarch the reflection vector against the G-buffer depth, add the hit HDR
// color masked by fresnel/roughness/edge-fade. Plain copy when Effect::Ssr is off.
// G34 screen-space GI pass: lit HDR + albedo + G-buffer -> HDR + single-bounce indirect
// (hdr -> hdrG, which the SSR pass then consumes). Plain copy when Effect::Gi is off.
void DeferredRenderer::drawGi(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdr");
    ctx.bindInput(1, "gAlbedo");
    ctx.bindInput(2, "gNormal");
    ctx.bindInput(3, "gDepth");
    const fuhgl::render::SceneView& s = *curScene_;
    const bool on = effectEnabled(Effect::Gi);
    drawFs(impl_->giMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uHdr"), 0);
        p.setUniform(p.uniformLocation("uGAlbedo"), 1);
        p.setUniform(p.uniformLocation("uGNormal"), 2);
        p.setUniform(p.uniformLocation("uGDepth"), 3);
        p.setUniform(p.uniformLocation("uGiEnabled"), on ? 1 : 0);
        p.setUniform("uInvViewProj", s.camera->viewProj(s.aspect).inverse());
        p.setUniform("uViewProj", s.camera->viewProj(s.aspect)); // non-jittered
        p.setUniform("uStrength", giStrength);
        p.setUniform(p.uniformLocation("uCones"), std::min(std::max(giCones, 1), 4));
        p.setUniform(p.uniformLocation("uSteps"), std::min(std::max(giSteps, 1), 32));
        p.setUniform("uMaxDist", giMaxDist);
        p.setUniform("uThickness", giThickness);
        p.setUniform("uRotSeed", static_cast<float>(frameIndex_ % 64) * 0.7f);
    });
}

void DeferredRenderer::drawSsr(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, effectEnabled(Effect::Gi) ? "hdrG" : "hdr"); // G34: hdr + GI when the gi pass ran
    ctx.bindInput(1, "gNormal");
    ctx.bindInput(2, "gDepth");
    ctx.bindInput(3, "gPbr");
    const fuhgl::render::SceneView& s = *curScene_;
    const bool on = effectEnabled(Effect::Ssr);
    drawFs(impl_->ssrMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uHdr"), 0);
        p.setUniform(p.uniformLocation("uGNormal"), 1);
        p.setUniform(p.uniformLocation("uGDepth"), 2);
        p.setUniform(p.uniformLocation("uGPbr"), 3);
        p.setUniform(p.uniformLocation("uSsrEnabled"), on ? 1 : 0);
        p.setUniform("uInvViewProj", s.camera->viewProj(s.aspect).inverse());
        p.setUniform("uViewProj", s.camera->viewProj(s.aspect)); // non-jittered
        p.setUniform("uCamPos", s.camera->eye());
        p.setUniform(p.uniformLocation("uMaxSteps"), std::min(std::max(ssrMaxSteps, 1), 128));
        p.setUniform("uThickness", ssrThickness);
        p.setUniform("uMaxDist", ssrMaxDist);
        // G22: wetness adds reflection strength on top of the manual ssrWetnessBoost
        const float wetBoost = ssrWetnessBoost + (effectEnabled(Effect::Wetness) ? wetnessAmount : 0.f);
        p.setUniform("uStrength", ssrStrength * (1.f + wetBoost));
        p.setUniform("uZNear", s.camera->zNear());
        p.setUniform("uZFar", s.camera->zFar());
    });
}

// G21 volumetric fog/clouds pass: raymarch the view ray through the 3D-noise volume
// (height falloff + fBm), accumulate transmittance + Henyey-Greenstein sun scattering and
// ambient, output hdr * T + inscatter (hdrP -> hdrV, which bloom then consumes). Plain copy
// when Effect::VolumetricFog is off.
void DeferredRenderer::drawVolumetric(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdrP");
    ctx.bindInput(1, "gDepth");
    const fuhgl::render::SceneView& s = *curScene_;
    const bool on = effectEnabled(Effect::VolumetricFog);
    const fuhgl::math::Vec3 ambient = (skyTop + skyBottom) * 0.25f;
    drawFs(impl_->volumetricMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uHdr"), 0);
        p.setUniform(p.uniformLocation("uGDepth"), 1);
        p.setUniform(p.uniformLocation("uFogEnabled"), on ? 1 : 0);
        p.setUniform("uInvViewProj", s.camera->viewProj(s.aspect).inverse());
        p.setUniform("uCamPos", s.camera->eye());
        p.setUniform("uSunDir", s.sunDirection);
        p.setUniform("uSunColor", s.sunColor);
        p.setUniform("uAmbient", ambient);
        p.setUniform("uDensity", volumetricDensity);
        p.setUniform("uCloudiness", volumetricCloudiness);
        p.setUniform("uHeightFalloff", volumetricHeightFalloff);
        p.setUniform("uNoiseScale", volumetricNoiseScale);
        p.setUniform("uAnisotropy", volumetricAnisotropy);
        p.setUniform("uMaxDist", volumetricMaxDist);
        p.setUniform("uStrength", volumetricStrength);
        p.setUniform(p.uniformLocation("uSteps"), std::min(std::max(volumetricSteps, 1), 64));
        p.setUniform("uTime", volumetricTime);
        p.setUniform("uZNear", s.camera->zNear());
        p.setUniform("uZFar", s.camera->zFar());
    });
}

// G17 bloom pass: bright-pass + 9-tap blur + add on the HDR image (hdrV -> hdrB, which the
// TAA pass then blends temporally). Plain copy when Effect::Bloom is off.
void DeferredRenderer::drawBloom(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdrV");
    const bool on = effectEnabled(Effect::Bloom);
    drawFs(impl_->bloomMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uSrc"), 0);
        p.setUniform(p.uniformLocation("uBloomEnabled"), on ? 1 : 0);
        p.setUniform("uThreshold", bloomThreshold);
        p.setUniform("uStrength", bloomStrength);
        p.setUniform("uRadius", bloomRadius);
        p.setUniform("uTexel", fuhgl::math::Vec2(1.f / static_cast<float>(internalWidth_),
                                                  1.f / static_cast<float>(internalHeight_)));
    });
}

// G30 lens flare + anamorphic streaks pass: hdrB -> hdrF (the TAA pass blends hdrF). The
// sun's screen position comes from projecting SceneView::sunDirection at infinity
// (w = 0 direction; valid when the sun is in front of the camera). Plain copy when both
// Effect::LensFlare and Effect::Anamorphic are off.
void DeferredRenderer::drawFlare(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdrB");
    const fuhgl::render::SceneView& s = *curScene_;
    const fuhgl::math::Mat4 vp = s.camera->viewProj(s.aspect); // non-jittered
    const fuhgl::math::Vec4 clip = vp * fuhgl::math::Vec4(s.sunDirection, 0.f);
    const bool sunValid = clip.w > 1e-3f;
    const fuhgl::math::Vec2 sunUV =
        sunValid ? fuhgl::math::Vec2(clip.x / clip.w * 0.5f + 0.5f, clip.y / clip.w * 0.5f + 0.5f)
                 : fuhgl::math::Vec2(0.f, 0.f);
    drawFs(impl_->flareMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uSrc"), 0);
        p.setUniform("uTexel", fuhgl::math::Vec2(1.f / static_cast<float>(internalWidth_),
                                                  1.f / static_cast<float>(internalHeight_)));
        p.setUniform("uSunUV", sunUV);
        p.setUniform(p.uniformLocation("uSunValid"), sunValid ? 1 : 0);
        p.setUniform(p.uniformLocation("uFlareEnabled"), effectEnabled(Effect::LensFlare) ? 1 : 0);
        p.setUniform(p.uniformLocation("uAnamorphicEnabled"), effectEnabled(Effect::Anamorphic) ? 1 : 0);
        p.setUniform("uFlareStrength", lensFlareStrength);
        p.setUniform("uFlareTint", lensFlareTint);
        p.setUniform("uAnamorphicStrength", anamorphicStrength);
        p.setUniform("uAnamorphicStretch", anamorphicStretch);
        p.setUniform("uAnamorphicThreshold", anamorphicThreshold);
        p.setUniform("uAnamorphicTint", anamorphicTint);
    });
}

void DeferredRenderer::drawTaa(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "hdrF"); // lighting + water + particles + G17 bloom + G30 flare/anamorphic
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

// G10 DoF pass: hexagonal bokeh gather on the upscaled HDR image ("up" -> "dof"),
// CoC from the G-buffer depth; plain copy when Effect::Dof is off.
// G32 chromatic aberration pass: upscaled HDR -> ca (output resolution, feeds DoF).
// Radial R/B split around caCenter. Plain copy when Effect::ChromaticAberration is off.
void DeferredRenderer::drawCa(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "up");
    const bool on = effectEnabled(Effect::ChromaticAberration);
    drawFs(impl_->caMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uSrc"), 0);
        p.setUniform(p.uniformLocation("uCaEnabled"), on ? 1 : 0);
        p.setUniform("uCaStrength", caStrength);
        p.setUniform("uCaCenter", caCenter);
    });
}

void DeferredRenderer::drawDof(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, effectEnabled(Effect::ChromaticAberration) ? "ca" : "up"); // G32 output when the ca pass ran
    ctx.bindInput(1, "gDepth");
    const bool on = effectEnabled(Effect::Dof);
    drawFs(impl_->dofMat, [&](fuhgl::Program& p) {
        p.setUniform(p.uniformLocation("uSrc"), 0);
        p.setUniform(p.uniformLocation("uGDepth"), 1);
        p.setUniform(p.uniformLocation("uDofEnabled"), on ? 1 : 0);
        if (curScene_ && curScene_->camera) {
            p.setUniform("uZNear", curScene_->camera->zNear());
            p.setUniform("uZFar", curScene_->camera->zFar());
        } else {
            p.setUniform("uZNear", 0.1f);
            p.setUniform("uZFar", 100.f);
        }
        p.setUniform("uFocusDist", dofFocusDist);
        p.setUniform("uDofStrength", dofStrength);
        p.setUniform("uMaxCoC", dofMaxCoC);
        p.setUniform("uBokehAngle", 0.35f);
        p.setUniform("uTexel", fuhgl::math::Vec2(width_ > 0 ? 1.f / static_cast<float>(width_) : 1.f,
                                                  height_ > 0 ? 1.f / static_cast<float>(height_) : 1.f));
    });
}

// G10 HUD: draws the debug panel (fps / frame ms / effect toggles) into the output.
// Called from render() after the post graph (not as a graph pass — see createInternal).
void DeferredRenderer::drawHud() {
    if (!effectEnabled(Effect::DebugHud) || !hud_.isCreated()) return;
    fuhgl::Framebuffer& out = impl_->output;
    out.bind();
    const fuhgl::gl::GLenum buf[1] = {fuhgl::gl::GL_COLOR_ATTACHMENT0};
    fuhgl::gl::drawBuffers(1, buf);
    fuhgl::gl::viewport(0, 0, width_, height_);
    hud_.draw(width_, height_, lastFrameMs_, *this);
    fuhgl::Framebuffer::unbind();
}

void DeferredRenderer::drawTonemap(fuhgl::render::RenderGraph::PassContext& ctx) {
    ctx.bindInput(0, "dof"); // upscaled + G10 DoF
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
        p.setUniform(p.uniformLocation("uOperator"), static_cast<int>(tonemapOperator)); // G23
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
const fuhgl::Texture& DeferredRenderer::texSsao() const { return *impl_->post.texture("ssao"); }
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
    gate(Effect::Decals, decalsEnabled_);
    gate(Effect::Dof, dofEnabled_);
    gate(Effect::DebugHud, hudEnabled_);
    gate(Effect::Water, waterEnabled_);
    gate(Effect::GpuSkinning, gpuSkinningEnabled_);
    gate(Effect::Ssao, ssaoEnabled_);
    gate(Effect::Ssr, ssrEnabled_);
    gate(Effect::Bloom, bloomEnabled_);
    gate(Effect::VolumetricFog, volumetricEnabled_);
    gate(Effect::Wetness, wetnessEnabled_);
    gate(Effect::LensFlare, lensFlareEnabled_);
    gate(Effect::Anamorphic, anamorphicEnabled_);
    gate(Effect::Hbao, hbaoEnabled_);
    gate(Effect::ChromaticAberration, caEnabled_);
    gate(Effect::Subsurface, sssEnabled_);
    gate(Effect::Gi, giEnabled_);
    gate(Effect::Weathering, weatheringEnabled_);
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
        case Effect::Decals: decalsEnabled_ = on; break;
        case Effect::Dof: dofEnabled_ = on; break;
        case Effect::DebugHud: hudEnabled_ = on; break;
        case Effect::Water: waterEnabled_ = on; break;
        case Effect::GpuSkinning: gpuSkinningEnabled_ = on; break;
        case Effect::Ssao: ssaoEnabled_ = on; break;
        case Effect::Ssr: ssrEnabled_ = on; break;
        case Effect::Bloom: bloomEnabled_ = on; break;
        case Effect::VolumetricFog: volumetricEnabled_ = on; break;
        case Effect::Wetness: wetnessEnabled_ = on; break;
        case Effect::LensFlare: lensFlareEnabled_ = on; break;
        case Effect::Anamorphic: anamorphicEnabled_ = on; break;
        case Effect::Hbao: hbaoEnabled_ = on; break;
        case Effect::ChromaticAberration: caEnabled_ = on; break;
        case Effect::Subsurface: sssEnabled_ = on; break;
        case Effect::Gi: giEnabled_ = on; break;
        case Effect::Weathering: weatheringEnabled_ = on; break;
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
        case Effect::Decals: return decalsEnabled_;
        case Effect::Dof: return dofEnabled_;
        case Effect::DebugHud: return hudEnabled_;
        case Effect::Water: return waterEnabled_;
        case Effect::GpuSkinning: return gpuSkinningEnabled_;
        case Effect::Ssao: return ssaoEnabled_;
        case Effect::Ssr: return ssrEnabled_;
        case Effect::Bloom: return bloomEnabled_;
        case Effect::VolumetricFog: return volumetricEnabled_;
        case Effect::Wetness: return wetnessEnabled_;
        case Effect::LensFlare: return lensFlareEnabled_;
        case Effect::Anamorphic: return anamorphicEnabled_;
        case Effect::Hbao: return hbaoEnabled_;
        case Effect::ChromaticAberration: return caEnabled_;
        case Effect::Subsurface: return sssEnabled_;
        case Effect::Gi: return giEnabled_;
        case Effect::Weathering: return weatheringEnabled_;
    }
    return false;
}

void DeferredRenderer::setAllEffects(bool on) {
    taaEnabled_ = upscaleEnabled_ = tonemapEnabled_ = iblEnabled_ = shadows_ = csmEnabled_ = on;
    pointShadows_ = contactShadowsEnabled = particlesEnabled_ = lightProbesEnabled_ = decalsEnabled_ = waterEnabled_ = on;
    dofEnabled_ = hudEnabled_ = gpuSkinningEnabled_ = ssaoEnabled_ = ssrEnabled_ = bloomEnabled_ = on;
    volumetricEnabled_ = on;
    wetnessEnabled_ = on;
    lensFlareEnabled_ = anamorphicEnabled_ = hbaoEnabled_ = on;
    caEnabled_ = sssEnabled_ = giEnabled_ = on;
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
