// Headless G3 smoke test (label "gl_context"): renders a small lit scene through
// fuhglgraphics::render::DeferredRenderer (extended G-buffer + post chain) via a
// surfaceless EGL context and verifies pixels: lit geometry, sky background,
// material-ID attachment, MSAA resolve and the render-scale path.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <cstdio>
#include <string>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kSkip = 77;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_render", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_render: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_render: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    const int W = 256, H = 256;
    fuhglgraphics::render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_render: create failed\n");
        return 1;
    }
    renderer.enableShadows(512);

    fuhgl::render::Mesh box, ground;
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {0.9f, 0.4f, 0.2f});
    fuhgl::render::shapes::quad(ground, 8.f, {0.35f, 0.4f, 0.38f});

    fuhgl::render::Material boxMat = fuhgl::render::materials::pbr({0.9f, 0.35f, 0.15f}, 0.1f, 0.4f);
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.4f, 0.45f, 0.4f}, 0.f, 0.9f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.2f, 6.f};
    cam.pitch = -0.22f;

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&box, &boxMat, fuhgl::math::translate({0.f, 0.8f, 0.f})});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.4f, -1.f, -0.3f}, {1.f, 0.9f, 0.75f}, 3.f));
    scene.lights.push_back(fuhgl::render::Light::point({3.f, 3.f, 4.f}, {0.4f, 0.6f, 1.f}, 18.f));
    renderer.setShadowSceneBounds({0.f, 0.f, 0.f}, 12.f);

    const std::vector<std::uint32_t> materialIds = {0u, 42u}; // ground, box
    for (int frame = 0; frame < 4; ++frame) renderer.render(scene, &materialIds);
    fuhgl::gl::flush();

    renderer.output().bind();
    unsigned char center[4] = {0, 0, 0, 0}, corner[4] = {0, 0, 0, 0};
    fuhgl::gl::readPixels(W / 2, H / 2, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, center);
    fuhgl::gl::readPixels(2, H - 3, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, corner);
    std::fprintf(stderr, "gl_smoke_render: center=(%d,%d,%d,%d) corner=(%d,%d,%d,%d)\n", center[0], center[1],
                 center[2], center[3], corner[0], corner[1], corner[2], corner[3]);
    // debug: dump G-buffer + HDR at the center pixel via getTexImage
    auto dumpTex = [&](const char* name, const fuhgl::Texture& tex, fuhgl::gl::GLenum format) {
        fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0);
        fuhgl::gl::functions().glBindTexture(fuhgl::gl::GL_TEXTURE_2D, tex.id());
        std::vector<unsigned char> buf(W * H * 4, 0);
        fuhgl::gl::getTexImage(fuhgl::gl::GL_TEXTURE_2D, 0, format, fuhgl::gl::GL_UNSIGNED_BYTE, buf.data());
        const std::size_t c = (static_cast<std::size_t>(H / 2) * W + W / 2) * 4;
        std::fprintf(stderr, "  %s center=(%d,%d,%d,%d)\n", name, buf[c], buf[c + 1], buf[c + 2], buf[c + 3]);
    };
    dumpTex("gAlbedo", renderer.texAlbedo(), fuhgl::gl::GL_RGBA);
    dumpTex("gPbr", renderer.texPbr(), fuhgl::gl::GL_RGBA);
    dumpTex("gMotion", renderer.texMotion(), fuhgl::gl::GL_RGBA);
    dumpTex("hdr", renderer.hdrTexture(), fuhgl::gl::GL_RGBA);
    {
        fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0);
        fuhgl::gl::functions().glBindTexture(fuhgl::gl::GL_TEXTURE_2D, renderer.texDepth().id());
        std::vector<float> buf(W * H, 0.f);
        fuhgl::gl::getTexImage(fuhgl::gl::GL_TEXTURE_2D, 0, fuhgl::gl::GL_DEPTH_COMPONENT, fuhgl::gl::GL_FLOAT, buf.data());
        std::fprintf(stderr, "  gDepth center=%f\n", buf[static_cast<std::size_t>(H / 2) * W + W / 2]);
    }
    if (!(center[3] == 255 && center[0] > 30) || corner[2] <= corner[0]) {
        std::fprintf(stderr, "gl_smoke_render: FAIL (center not lit / corner not sky)\n");
        return 1;
    }

    // material-ID attachment must carry the box's id at the center pixel
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0);
    fuhgl::gl::functions().glBindTexture(fuhgl::gl::GL_TEXTURE_2D, renderer.texMaterialId().id());
    std::vector<unsigned char> matId(W * H * 4, 0);
    fuhgl::gl::getTexImage(fuhgl::gl::GL_TEXTURE_2D, 0, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, matId.data());
    const std::size_t c = (static_cast<std::size_t>(H / 2) * W + W / 2) * 4;
    const std::uint32_t id = fuhglgraphics::render::unpackMaterialId(&matId[c]);
    std::fprintf(stderr, "gl_smoke_render: center materialId=%u\n", id);
    if (id != 42u) {
        std::fprintf(stderr, "gl_smoke_render: FAIL (material id %u != 42)\n", id);
        return 1;
    }

    // F45/F46 shadow-kit smoke: switch the CSM filter pcf3x3 -> pcss -> vsm (moments
    // attachment is recreated on the VSM switch) and render a frame each — the frame
    // must stay lit and no GL error may occur. Then a point light with a cube shadow map.
    auto readCenter = [&](unsigned char out[4]) {
        renderer.output().bind();
        fuhgl::gl::readPixels(W / 2, H / 2, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, out);
        fuhgl::gl::flush();
    };
    for (const auto f : {fuhgl::render::ShadowFilter::Pcf3x3, fuhgl::render::ShadowFilter::Pcss,
                         fuhgl::render::ShadowFilter::Vsm, fuhgl::render::ShadowFilter::PoissonPcf}) {
        renderer.setShadowFilter(f);
        renderer.render(scene, &materialIds);
        unsigned char px[4] = {0, 0, 0, 0};
        readCenter(px);
        std::fprintf(stderr, "gl_smoke_render: filter %d center=(%d,%d,%d,%d)\n",
                     static_cast<int>(f), px[0], px[1], px[2], px[3]);
        if (!(px[3] == 255 && px[0] > 20)) {
            std::fprintf(stderr, "gl_smoke_render: FAIL (frame not lit with filter %d)\n", static_cast<int>(f));
            return 1;
        }
        if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
            std::fprintf(stderr, "gl_smoke_render: FAIL (GL error with filter %d)\n", static_cast<int>(f));
            return 1;
        }
    }
    renderer.setShadowFilter(fuhgl::render::ShadowFilter::Pcf3x3);
    // point-light cube shadow (F46): add a point light, enable Effect::PointShadows via
    // the map, render — frame must stay lit, cube map must be allocated
    scene.lights.push_back(fuhgl::render::Light::point({0.f, 3.f, 2.f}, {1.f, 0.8f, 0.6f}, 20.f, 30.f));
    renderer.enablePointShadow(-1, 512);
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::PointShadows, true);
    renderer.render(scene, &materialIds);
    {
        unsigned char px[4] = {0, 0, 0, 0};
        readCenter(px);
        std::fprintf(stderr, "gl_smoke_render: point-shadow center=(%d,%d,%d,%d) cubeSize=%d\n", px[0], px[1],
                     px[2], px[3], renderer.pointShadowMap() ? renderer.pointShadowMap()->size() : 0);
        if (!(px[3] == 255 && px[0] > 20) || !renderer.pointShadowEnabled() ||
            !renderer.pointShadowMap() || renderer.pointShadowMap()->size() != 512) {
            std::fprintf(stderr, "gl_smoke_render: FAIL (point shadow path)\n");
            return 1;
        }
    }
    // contact shadows on the shadow light (F46)
    renderer.contactShadowsEnabled = true;
    renderer.render(scene, &materialIds);
    {
        unsigned char px[4] = {0, 0, 0, 0};
        readCenter(px);
        if (!(px[3] == 255 && px[0] > 20)) {
            std::fprintf(stderr, "gl_smoke_render: FAIL (contact shadows frame not lit)\n");
            return 1;
        }
    }
    renderer.contactShadowsEnabled = false;
    renderer.disablePointShadow();
    scene.lights.pop_back();

    // render-scale path: internal resolution halves, output stays full size
    renderer.setRenderScale(0.5f);
    if (renderer.internalWidth() != W / 2 || renderer.internalHeight() != H / 2 ||
        renderer.width() != W || renderer.height() != H) {
        std::fprintf(stderr, "gl_smoke_render: FAIL (internal size %dx%d)\n", renderer.internalWidth(),
                     renderer.internalHeight());
        return 1;
    }
    renderer.render(scene, &materialIds);
    renderer.output().bind();
    unsigned char c2[4] = {0, 0, 0, 0};
    fuhgl::gl::readPixels(W / 2, H / 2, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, c2);
    std::fprintf(stderr, "gl_smoke_render: upscaled center=(%d,%d,%d,%d)\n", c2[0], c2[1], c2[2], c2[3]);
    if (!(c2[3] == 255 && c2[0] > 20)) {
        std::fprintf(stderr, "gl_smoke_render: FAIL (upscaled frame not lit)\n");
        return 1;
    }

    // MSAA path: creation + one full frame. On hardware GPUs the per-attachment blit
    // resolve must run without GL errors; software rasterizers (llvmpipe/softpipe/
    // SwiftShader) resolve multisample MRT inconsistently, so only creation is
    // asserted there.
    {
        fuhglgraphics::render::DeferredRenderer msaaRenderer;
        if (!msaaRenderer.create(W, H, 4) || !msaaRenderer.effectEnabled(fuhglgraphics::render::Effect::Msaa)) {
            std::fprintf(stderr, "gl_smoke_render: FAIL (MSAA create)\n");
            return 1;
        }
        msaaRenderer.render(scene, &materialIds);
        fuhgl::gl::flush();
        const fuhgl::gl::DeviceInfo info = fuhgl::gl::queryDeviceInfo();
        const std::string& r = info.renderer;
        const bool software = r.find("llvmpipe") != std::string::npos ||
                              r.find("softpipe") != std::string::npos ||
                              r.find("SwiftShader") != std::string::npos;
        const fuhgl::gl::GLenum glErr = fuhgl::gl::functions().glGetError();
        std::fprintf(stderr, "gl_smoke_render: MSAA 4 render on '%s', glError=0x%x\n", r.c_str(), glErr);
        if (!software && glErr != fuhgl::gl::GL_NO_ERROR) {
            std::fprintf(stderr, "gl_smoke_render: FAIL (GL error in MSAA path)\n");
            return 1;
        }
        std::fprintf(stderr, "gl_smoke_render: MSAA 4 path OK\n");
    }

    std::fprintf(stderr, "gl_smoke_render: PASS (GL %d.%d)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor);
    return 0;
}
