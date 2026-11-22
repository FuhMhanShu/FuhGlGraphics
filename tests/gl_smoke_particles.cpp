// Headless G5 smoke test (label "gl_context"): creates a particles::ParticleSystem on the
// GL context (compute path on GL >= 4.3, CPU fallback below), updates it, and renders it
// through the G3 DeferredRenderer's "particles" post pass on a small G9 scene. Verifies:
// alive particles > 0, the gpu() flag matches the GL version, the rendered frame is
// brighter with particles than without, and no GL error occurs.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

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
    if (!window.create("gl_smoke_particles", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_particles: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_particles: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;
    const bool expectGpu = fuhgl::gl::version().atLeast(4, 3);

    // --- particle system ---
    particles::EmitterConfig cfg;
    cfg.position = {0.f, 1.f, 0.f};
    cfg.direction = {0.f, 1.f, 0.f};
    cfg.speed = 1.5f;
    cfg.spread = 0.3f;
    cfg.rate = 400.f;
    cfg.lifetime = 2.f;
    cfg.size = 0.25f;
    cfg.color = {1.f, 0.6f, 0.2f};
    cfg.alpha = 0.8f;
    cfg.blend = particles::BlendMode::Additive;
    particles::ParticleSystem ps;
    if (!ps.create(4096, cfg)) {
        std::fprintf(stderr, "gl_smoke_particles: FAIL (create)\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_particles: gpu=%d (GL %d.%d)\n", ps.gpu() ? 1 : 0, fuhgl::gl::version().major,
                 fuhgl::gl::version().minor);
    CHECK(ps.gpu() == expectGpu);
    for (int i = 0; i < 30; ++i) ps.update(1.f / 30.f); // simulate 1 s
    const int alive = ps.aliveCount();
    std::fprintf(stderr, "gl_smoke_particles: alive=%d\n", alive);
    CHECK(alive > 50);

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
    emitter.system = std::move(ps); // move the created system into the component
    if (!emitter.system.isReady()) {
        std::fprintf(stderr, "gl_smoke_particles: FAIL (emitter system not ready)\n");
        return 1;
    }
    emitter.system.setEmitter(cfg);
    for (int i = 0; i < 30; ++i) emitter.system.update(1.f / 30.f);

    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Particles);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_particles: renderer create failed\n");
        return 1;
    }
    renderer.setEffectEnabled(render::Effect::Taa, false);

    auto renderFrame = [&](bool withParticles) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, W, H);
        frame.effects.applyTo(renderer);
        std::vector<const particles::ParticleSystem*> systems;
        if (withParticles) systems.push_back(&emitter.system);
        for (int i = 0; i < 2; ++i)
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow,
                            withParticles ? &systems : nullptr);
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
        fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    const std::vector<unsigned char> without = renderFrame(false);
    const std::vector<unsigned char> with = renderFrame(true);
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_particles: FAIL (GL error)\n");
        return 1;
    }
    // the emitter sits at the frame center — with particles the center must be brighter
    long sumWithout = 0, sumWith = 0;
    for (int y = H / 2 - 20; y < H / 2 + 20; ++y)
        for (int x = W / 2 - 20; x < W / 2 + 20; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * W + x) * 4;
            sumWithout += without[o] + without[o + 1] + without[o + 2];
            sumWith += with[o] + with[o + 1] + with[o + 2];
        }
    std::fprintf(stderr, "gl_smoke_particles: center sum without=%ld with=%ld\n", sumWithout, sumWith);
    CHECK(sumWith > sumWithout + 400);

    std::fprintf(stderr, "gl_smoke_particles: PASS (GL %d.%d, gpu=%d)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, emitter.system.gpu() ? 1 : 0);
    return fuhglgraphicstest::failures() ? 1 : 0;
}
