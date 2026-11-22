// Headless G10 smoke test (label "gl_context"): exercises the two new G10 features of the
// G3 DeferredRenderer on a small scene (near crate in front of a far ground plane):
//  1. Effect::Dof — with the focus on the far ground, the near crate must blur (frames
//     with DoF on vs off differ in the crate region);
//  2. Effect::DebugHud — the HUD panel (fps / frame ms / effect toggles, FuhGLCore
//     TextRenderer) must change the top-left corner pixels.
// Also verifies no GL error. Skips cleanly with exit code 77 without GL >= 3.3.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include "test_framework.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kSkip = 77;
constexpr int W = 256, H = 256;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_g10", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_g10: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_g10: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- scene: ground plane far away + a near crate (foreground) ---
    scene::Scene scene;
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 12.f, {0.45f, 0.45f, 0.47f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.45f, 0.45f, 0.47f};
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    fuhgl::render::Mesh crate;
    fuhgl::render::shapes::box(crate, {0.5f, 0.5f, 0.5f}, {0.75f, 0.3f, 0.2f});
    scene::Node& crateNode = scene.root().createChild("crate");
    crateNode.translation = {0.f, 0.5f, -1.2f}; // close to the camera
    scene::MeshEntry& ce = crateNode.attach<scene::MeshComponent>().addMesh(&crate);
    ce.albedo = {0.75f, 0.3f, 0.2f};
    ce.castShadow = false;
    ce.materialId = 2;

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.3f, -1.f, -0.4f}, {1.f, 0.95f, 0.85f}, 2.f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 1.6f, 3.2f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{0.f, 0.6f, -4.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Tonemap);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_g10: renderer create failed\n");
        return 1;
    }
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.dofFocusDist = 8.f;  // focus on the far ground, not the near crate
    renderer.dofStrength = 30.f;  // strong blur for the test
    renderer.dofMaxCoC = 20.f;

    auto renderFrame = [&](bool dof, bool hud) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, W, H);
        frame.effects.disable(render::Effect::Dof);
        frame.effects.disable(render::Effect::DebugHud);
        if (dof) frame.effects.enable(render::Effect::Dof);
        if (hud) frame.effects.enable(render::Effect::DebugHud);
        frame.effects.applyTo(renderer);
        for (int i = 0; i < 2; ++i)
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                            frame.lightProbes, &frame.decals);
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
        fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    const std::vector<unsigned char> dofOff = renderFrame(false, false);
    const std::vector<unsigned char> dofOn = renderFrame(true, false);
    const std::vector<unsigned char> hudOn = renderFrame(false, true);
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_g10: FAIL (GL error)\n");
        return 1;
    }
    CHECK(renderer.hud().isCreated());
    CHECK(renderer.hud().frames() > 0);
    CHECK(renderer.lastFrameMs() >= 0.f);

    auto diffIn = [&](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, int x0,
                      int x1, int y0, int y1) {
        long n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * W + x) * 4;
                if (std::labs(static_cast<long>(a[o]) - b[o]) + std::labs(static_cast<long>(a[o + 1]) - b[o + 1]) +
                        std::labs(static_cast<long>(a[o + 2]) - b[o + 2]) >
                    12)
                    ++n;
            }
        return n;
    };
    // the crate occupies the frame center; DoF must blur it (frames differ there)
    const long crateDiff = diffIn(dofOn, dofOff, W / 2 - 40, W / 2 + 40, H / 2 - 30, H / 2 + 30);
    // the HUD panel is top-left — readPixels rows are bottom-up, so top = high y indices
    const long hudDiff = diffIn(hudOn, dofOff, 0, 220, H - 220, H);
    std::fprintf(stderr, "gl_smoke_g10: crateDiff(dof)=%ld  hudDiff=%ld  fps=%.1f frameMs=%.2f\n", crateDiff,
                 hudDiff, renderer.hud().fps(), renderer.lastFrameMs());
    CHECK(crateDiff > 100); // DoF visibly blurs the near crate
    CHECK(hudDiff > 50);    // HUD panel drawn top-left

    if (fuhglgraphicstest::failures() == 0)
        std::fprintf(stderr, "gl_smoke_g10: PASS (GL %d.%d)\n", fuhgl::gl::version().major,
                     fuhgl::gl::version().minor);
    return fuhglgraphicstest::failures() ? 1 : 0;
}
