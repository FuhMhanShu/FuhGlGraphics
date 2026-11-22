// Headless G9 smoke test (label "gl_context"): builds a fuhglgraphics::scene::Scene
// (camera component, ground + box nodes with per-mesh material IDs / castShadow /
// albedo / normal map, an instanced grid node, a light node, per-node effects),
// fills a FrameData, applies the merged effects to the G3 DeferredRenderer and
// renders via a surfaceless EGL context. Verifies: frame contents, instancing
// upload, lit pixels, material-ID readback, the normal-map path (no GL error) and
// that effect parameters from scene::Effects change the output (exposure).
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_scene", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_scene: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_scene: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

fuhgl::gl::GLenum glErrAt(const char* stage) {
    const fuhgl::gl::GLenum e = fuhgl::gl::functions().glGetError();
    if (e != fuhgl::gl::GL_NO_ERROR)
        std::fprintf(stderr, "gl_smoke_scene: glError=0x%x at %s\n", e, stage);
    return e;
}

unsigned char readCenterR(fuhglgraphics::render::DeferredRenderer& renderer, int W, int H) {
    renderer.output().bind();
    unsigned char px[4] = {0, 0, 0, 0};
    fuhgl::gl::readPixels(W / 2, H / 2, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px);
    std::fprintf(stderr, "gl_smoke_scene: center=(%d,%d,%d,%d)\n", px[0], px[1], px[2], px[3]);
    return px[0];
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    const int W = 256, H = 256;
    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_scene: renderer create failed\n");
        return 1;
    }
    glErrAt("after renderer.create");
    renderer.enableShadows(512);
    renderer.setShadowSceneBounds({0.f, 0.f, 0.f}, 12.f);
    glErrAt("after enableShadows");

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.2f, 6.f};
    cam.pitch = -0.22f;

    fuhgl::render::Mesh box, ground, pillar;
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {0.9f, 0.4f, 0.2f});
    fuhgl::render::shapes::quad(ground, 10.f, {0.35f, 0.4f, 0.38f});
    fuhgl::render::shapes::box(pillar, {0.25f, 1.2f, 0.25f}, {0.7f, 0.7f, 0.75f});

    // flat tangent-space normal map (+Z) — exercises the uUseNormalMap path
    fuhgl::Texture normalTex;
    const bool haveNormalTex = normalTex.create(fuhgl::Texture::Target::Tex2D);
    if (haveNormalTex) {
        std::vector<unsigned char> px(4 * 4 * 4);
        for (int i = 0; i < 16; ++i) {
            px[i * 4 + 0] = 128;
            px[i * 4 + 1] = 128;
            px[i * 4 + 2] = 255;
            px[i * 4 + 3] = 255;
        }
        normalTex.setImage2D(0, fuhgl::gl::GL_RGBA8, 4, 4, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                             px.data());
        normalTex.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
        normalTex.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
        normalTex.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_REPEAT);
        normalTex.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_REPEAT);
    }

    scene::Scene scene;
    scene.root().effects.enable(render::Effect::Tonemap);

    scene::Node& camNode = scene.root().createChild("camera");
    camNode.attach<scene::CameraComponent>().camera = &cam;

    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.4f, 0.45f, 0.42f};
    ge.roughness = 0.9f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f); // quad XY -> XZ ground

    scene::Node& boxNode = scene.root().createChild("box");
    boxNode.translation = {0.f, 0.8f, 0.f};
    scene::MeshEntry& be = boxNode.attach<scene::MeshComponent>().addMesh(&box);
    be.materialId = 7;
    be.castShadow = true;
    be.albedo = {0.95f, 0.45f, 0.15f};
    be.metallic = 0.1f;
    be.roughness = 0.35f;
    be.normalMap = haveNormalTex ? &normalTex : nullptr;
    boxNode.effects.enable(render::Effect::Taa);
    boxNode.effects.enable(render::Effect::Shadows);

    scene::Node& pillars = scene.root().createChild("pillars");
    pillars.instancing.enabled = true;
    pillars.instancing.count = 9;
    pillars.instancing.layout = scene::InstanceLayout::Grid;
    pillars.instancing.extent = {6.f, 0.f, 6.f};
    scene::MeshEntry& pe = pillars.attach<scene::MeshComponent>().addMesh(&pillar);
    pe.albedo = {0.65f, 0.65f, 0.7f};
    pe.roughness = 0.5f;
    pe.castShadow = true;
    pe.materialId = 3;

    scene::Node& sunNode = scene.root().createChild("sun");
    scene::LightComponent& slc = sunNode.attach<scene::LightComponent>();
    slc.light = fuhgl::render::Light::directional({-0.4f, -1.f, -0.3f}, {1.f, 0.9f, 0.75f}, 3.f);

    glErrAt("after scene setup");
    scene::FrameData frame;
    scene.fillFrame(frame, nullptr, W, H); // camera from the CameraComponent
    glErrAt("after fillFrame (instancing upload)");

    // --- frame contents ---
    if (frame.view.camera != &cam || frame.view.width != W || frame.view.height != H) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (frame camera/size)\n");
        return 1;
    }
    if (frame.view.items.size() != 3 || frame.materialIds.size() != 3 || frame.castShadow.size() != 3) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (items %zu)\n", frame.view.items.size());
        return 1;
    }
    if (frame.materialIds[0] != 1u || frame.materialIds[1] != 7u || frame.materialIds[2] != 3u ||
        frame.castShadow[0] || !frame.castShadow[1] || !frame.castShadow[2]) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (materialIds/castShadow)\n");
        return 1;
    }
    if (!frame.effects.enabled(render::Effect::Taa) || !frame.effects.enabled(render::Effect::Shadows) ||
        !frame.effects.enabled(render::Effect::Tonemap)) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (effect union)\n");
        return 1;
    }
    if (frame.view.lights.size() != 1) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (lights %zu)\n", frame.view.lights.size());
        return 1;
    }
    if (pillar.instanceCount() != 9) { // instancing property uploaded during fill
        std::fprintf(stderr, "gl_smoke_scene: FAIL (instanceCount %d != 9)\n", pillar.instanceCount());
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_scene: frame OK (3 items, instancing x9, effects union)\n");

    // --- render through the G3 renderer with the merged effects applied ---
    frame.effects.applyTo(renderer);
    for (int i = 0; i < 4; ++i) {
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
        glErrAt("after render");
    }
    fuhgl::gl::flush();

    const unsigned char centerR = readCenterR(renderer, W, H);
    renderer.output().bind();
    unsigned char center[4] = {0, 0, 0, 0};
    fuhgl::gl::readPixels(W / 2, H / 2, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, center);
    if (!(center[3] == 255 && center[0] > 30)) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (center not lit)\n");
        return 1;
    }

    // material-ID attachment at the center pixel must be the box's id (7)
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0);
    fuhgl::gl::functions().glBindTexture(fuhgl::gl::GL_TEXTURE_2D, renderer.texMaterialId().id());
    std::vector<unsigned char> matId(W * H * 4, 0);
    fuhgl::gl::getTexImage(fuhgl::gl::GL_TEXTURE_2D, 0, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                           matId.data());
    const std::size_t c = (static_cast<std::size_t>(H / 2) * W + W / 2) * 4;
    const std::uint32_t id = render::unpackMaterialId(&matId[c]);
    std::fprintf(stderr, "gl_smoke_scene: center materialId=%u\n", id);
    if (id != 7u) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (materialId %u != 7)\n", id);
        return 1;
    }

    // the normal-map path (uUseNormalMap=1) ran without GL errors
    const fuhgl::gl::GLenum glErr = fuhgl::gl::functions().glGetError();
    if (glErr != fuhgl::gl::GL_NO_ERROR) {
        std::fprintf(stderr, "gl_smoke_scene: FAIL (glError=0x%x after normal-map render)\n", glErr);
        return 1;
    }

    // --- effect parameters from scene::Effects drive the renderer (exposure) ---
    frame.effects.setAll(false);
    frame.effects.enable(render::Effect::Tonemap);
    frame.effects.params.exposure = 1.f;
    frame.effects.applyTo(renderer);
    renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
    fuhgl::gl::flush();
    const unsigned char rLow = readCenterR(renderer, W, H);
    frame.effects.params.exposure = 4.f;
    frame.effects.applyTo(renderer);
    renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
    fuhgl::gl::flush();
    const unsigned char rHigh = readCenterR(renderer, W, H);
    if (!(rHigh > rLow)) { // ACES tonemap is monotonic: higher exposure -> brighter
        std::fprintf(stderr, "gl_smoke_scene: FAIL (exposure %d -> %d)\n", rLow, rHigh);
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_scene: exposure 1 -> %d, exposure 4 -> %d\n", rLow, rHigh);

    std::fprintf(stderr, "gl_smoke_scene: PASS (GL %d.%d)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor);
    return 0;
}
