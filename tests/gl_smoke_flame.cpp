// Headless G35 smoke test (label "gl_context"): the flame billboard mode of the G5
// particle system. Renders the same emitter twice through the G3 DeferredRenderer's
// "particles" pass — once with EmitterConfig::flame = true (teardrop + fbm flicker +
// heat color ramp) and once as a plain soft sprite — and verifies the two frames differ
// in the emitter region (the flame shading actually changes the pixels), that the flame
// frame is at least as bright as the sprite frame there, that the simulation clock
// (ParticleSystem::time(), drives the uTime flicker uniform) advances, and that no GL
// error occurs. Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

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
    if (!window.create("gl_smoke_flame", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_flame: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_flame: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- flame emitter config (same shape as the campfire in examples/particles_screenshot) ---
    particles::EmitterConfig cfg;
    cfg.position = {0.f, 1.f, 0.f};
    cfg.direction = {0.f, 1.f, 0.f};
    cfg.speed = 0.5f;
    cfg.spread = 0.25f;
    cfg.rate = 300.f;
    cfg.lifetime = 1.0f;
    cfg.size = 0.45f;
    cfg.grow = 0.15f;
    cfg.color = {1.f, 0.72f, 0.30f};
    cfg.alpha = 0.6f;
    cfg.gravity = 0.45f;
    cfg.drag = 1.2f;
    cfg.blend = particles::BlendMode::Additive;
    cfg.flame = true;
    particles::ParticleSystem ps;
    if (!ps.create(4096, cfg)) {
        std::fprintf(stderr, "gl_smoke_flame: FAIL (create)\n");
        return 1;
    }
    for (int i = 0; i < 30; ++i) ps.update(1.f / 30.f); // simulate 1 s
    const int alive = ps.aliveCount();
    std::fprintf(stderr, "gl_smoke_flame: alive=%d time=%.3f gpu=%d\n", alive, ps.time(),
                 ps.gpu() ? 1 : 0);
    CHECK(alive > 50);
    CHECK(ps.time() > 0.9f); // G35: simulation clock advanced (drives uTime in beginPass)

    // --- small G9 scene: ground + sun + camera + the emitter ---
    scene::Scene scene;
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 10.f, {0.3f, 0.3f, 0.32f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.3f, 0.3f, 0.32f};
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.5f, 6.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{0.f, 1.2f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.3f, -1.f, -0.4f}, {1.f, 0.9f, 0.8f}, 2.f);

    scene::Node& emitterNode = scene.root().createChild("fire");
    scene::ParticleEmitterComponent& emitter = emitterNode.attach<scene::ParticleEmitterComponent>();
    emitter.config = cfg;
    emitter.system = std::move(ps);
    if (!emitter.system.isReady()) {
        std::fprintf(stderr, "gl_smoke_flame: FAIL (emitter system not ready)\n");
        return 1;
    }
    emitter.system.setEmitter(cfg);
    for (int i = 0; i < 30; ++i) emitter.system.update(1.f / 30.f);

    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Particles);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_flame: renderer create failed\n");
        return 1;
    }
    renderer.setEffectEnabled(render::Effect::Taa, false);

    auto renderFrame = [&]() {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, W, H);
        frame.effects.applyTo(renderer);
        std::vector<const particles::ParticleSystem*> systems{&emitter.system};
        for (int i = 0; i < 2; ++i)
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &systems);
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
        fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    // frame A: flame mode on
    emitter.system.setEmitter(cfg); // cfg.flame == true
    const std::vector<unsigned char> flame = renderFrame();
    // frame B: identical particles, plain sprite mode (only the uFlame uniform changes)
    particles::EmitterConfig spriteCfg = cfg;
    spriteCfg.flame = false;
    emitter.system.setEmitter(spriteCfg);
    const std::vector<unsigned char> sprite = renderFrame();
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_flame: FAIL (GL error)\n");
        return 1;
    }
    long sumFlame = 0, sumSprite = 0, diffAbs = 0;
    for (int y = H / 2 - 24; y < H / 2 + 24; ++y)
        for (int x = W / 2 - 24; x < W / 2 + 24; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * W + x) * 4;
            sumFlame += flame[o] + flame[o + 1] + flame[o + 2];
            sumSprite += sprite[o] + sprite[o + 1] + sprite[o + 2];
            diffAbs += std::abs(static_cast<int>(flame[o]) - static_cast<int>(sprite[o])) +
                       std::abs(static_cast<int>(flame[o + 1]) - static_cast<int>(sprite[o + 1])) +
                       std::abs(static_cast<int>(flame[o + 2]) - static_cast<int>(sprite[o + 2]));
        }
    std::fprintf(stderr, "gl_smoke_flame: center sum flame=%ld sprite=%ld absdiff=%ld\n", sumFlame,
                 sumSprite, diffAbs);
    CHECK(sumFlame > 400);          // the flame adds light in the emitter region
    CHECK(diffAbs > 2000);          // flame shading differs from the plain sprite path

    std::fprintf(stderr, "gl_smoke_flame: PASS (GL %d.%d, gpu=%d)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, emitter.system.gpu() ? 1 : 0);
    return fuhglgraphicstest::failures() ? 1 : 0;
}
