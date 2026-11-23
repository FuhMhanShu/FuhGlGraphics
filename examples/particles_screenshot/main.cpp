// G5 particle demo: GPU particle systems (compute on GL >= 4.3, CPU fallback below) as G9
// ParticleEmitterComponents, rendered through the G3 DeferredRenderer's "particles" post
// pass (additive sparks / alpha smoke / drifting pollen), headless to PNG for docs/images/.
//
// Usage: fuhglgraphics_particles_screenshot --out shot.png [--kind fire|smoke|pollen|all]
//          [--width 1280] [--height 720] [--frames 24] [--sim 2.0] [--scale 1.0] [--msaa 0]
//          [--no-rays] [--no-shadows] [--no-particles]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out = "particles.png";
    std::string kind = "all";
    int width = 1280, height = 720, frames = 24, msaa = 0;
    float scale = 1.f, sim = 2.f; // sim = seconds pre-simulated before the first frame
    bool rays = true, shadows = true, particles = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--kind") == 0 && i + 1 < argc) a.kind = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) a.msaa = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--sim") == 0 && i + 1 < argc) a.sim = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-rays") == 0) a.rays = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-particles") == 0) a.particles = false;
    }
    return a;
}

} // namespace

int main(int argc, char** argv) {
    using namespace fuhglgraphics;
    const Args args = parseArgs(argc, argv);
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_particles_screenshot", 64, 64)) {
        std::fprintf(stderr, "particles_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "particles_screenshot: context below GL 3.3\n");
        return 2;
    }
    const bool gpuParticles = fuhgl::gl::version().atLeast(4, 3);

    // --- stage: ground, log pile (campfire), sky + sun ---
    scene::Scene scene;
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 30.f, {0.30f, 0.28f, 0.25f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.30f, 0.28f, 0.25f};
    ge.roughness = 1.f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    fuhgl::render::Mesh log;
    fuhgl::render::shapes::box(log, {1.6f, 0.22f, 0.22f}, {0.30f, 0.20f, 0.12f});
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("log" + std::to_string(i));
        n.translation = {0.f, 0.11f + 0.22f * i, 0.f};
        n.rotation = fuhgl::math::fromAxisAngle(i * 1.1f, {0.f, 1.f, 0.f});
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&log);
        e.albedo = {0.30f, 0.20f, 0.12f};
        e.castShadow = true;
        e.materialId = 10 + i;
    }

    const fuhgl::math::Vec3 sunDir{std::cos(fuhgl::math::radians(125.f)) * std::cos(fuhgl::math::radians(24.f)),
                                   std::sin(fuhgl::math::radians(24.f)),
                                   std::sin(fuhgl::math::radians(125.f)) * std::cos(fuhgl::math::radians(24.f))};
    scene::Node& sunNode = scene.root().createChild("sun");
    sunNode.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.80f, 0.58f}, 3.0f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {4.2f, 2.2f, 5.2f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 target{0.f, 0.8f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::Particles);
    scene.root().effects.params.shadowCenter = {0.f, 1.f, 0.f};
    scene.root().effects.params.shadowRadius = 12.f;
    scene.root().effects.params.shadowZFar = 60.f;

    // --- G5 emitters (created with the GL context; GPU path on GL >= 4.3) ---
    const bool wantFire = args.kind == "fire" || args.kind == "all";
    const bool wantSmoke = args.kind == "smoke" || args.kind == "all";
    const bool wantPollen = args.kind == "pollen" || args.kind == "all";

    if (wantSmoke) { // smoke plume above the fire: alpha-blended, expanding — created BEFORE
        // the fire emitter so the particles pass draws it first and the flame stays visible
        scene::Node& n = scene.root().createChild("smoke");
        auto& em = n.attach<scene::ParticleEmitterComponent>();
        em.config.position = {0.f, 1.7f, 0.f}; // well above the flame tip
        em.config.direction = {0.f, 1.f, 0.f};
        em.config.speed = 0.7f;
        em.config.spread = 0.3f;
        em.config.rate = 70.f;
        em.config.lifetime = 3.0f;
        em.config.size = 0.22f;
        em.config.grow = 0.7f;
        em.config.color = {0.38f, 0.39f, 0.44f};
        em.config.alpha = 0.15f;
        em.config.gravity = 0.35f; // buoyant
        em.config.drag = 0.6f;
        em.config.blend = particles::BlendMode::Alpha;
        if (!em.system.create(4096, em.config)) {
            std::fprintf(stderr, "particles_screenshot: smoke emitter create failed\n");
            return 1;
        }
    }
    if (wantFire) { // campfire: G35 teardrop flame (fbm flicker, heat color ramp) + a few additive embers
        scene::Node& n = scene.root().createChild("fire");
        auto& em = n.attach<scene::ParticleEmitterComponent>();
        em.config.position = {0.f, 0.35f, 0.f};
        em.config.direction = {0.f, 1.f, 0.f};
        em.config.speed = 0.5f;
        em.config.spread = 0.22f;
        em.config.rate = 140.f;
        em.config.lifetime = 1.0f;
        em.config.size = 0.42f;
        em.config.grow = 0.18f;
        em.config.color = {1.0f, 0.72f, 0.30f};
        em.config.alpha = 0.55f;
        em.config.gravity = 0.45f; // buoyant hot gas rises
        em.config.drag = 1.2f;
        em.config.blend = particles::BlendMode::Additive; // overlapping tongues accumulate to a white-hot core
        em.config.flame = true; // G35: teardrop + noise flame shading in the billboard FS
        if (!em.system.create(8192, em.config)) {
            std::fprintf(stderr, "particles_screenshot: fire emitter create failed\n");
            return 1;
        }
        // embers: small fast additive sparks flying out of the flame
        scene::Node& n2 = scene.root().createChild("embers");
        auto& em2 = n2.attach<scene::ParticleEmitterComponent>();
        em2.config.position = {0.f, 0.42f, 0.f};
        em2.config.direction = {0.f, 1.f, 0.f};
        em2.config.speed = 2.0f;
        em2.config.spread = 0.45f;
        em2.config.rate = 45.f;
        em2.config.lifetime = 1.4f;
        em2.config.size = 0.045f;
        em2.config.grow = -0.012f; // sparks shrink while they burn out
        em2.config.color = {1.0f, 0.55f, 0.12f};
        em2.config.alpha = 1.0f;
        em2.config.gravity = 1.1f;
        em2.config.drag = 0.8f;
        em2.config.blend = particles::BlendMode::Additive;
        if (!em2.system.create(2048, em2.config)) {
            std::fprintf(stderr, "particles_screenshot: ember emitter create failed\n");
            return 1;
        }
    }
    if (wantPollen) { // drifting pollen/dust motes in the sun light
        scene::Node& n = scene.root().createChild("pollen");
        auto& em = n.attach<scene::ParticleEmitterComponent>();
        em.config.position = {0.f, 1.6f, 0.f};
        em.config.direction = {0.f, 1.f, 0.f};
        em.config.speed = 0.25f;
        em.config.spread = 1.6f; // wide scatter
        em.config.rate = 120.f;
        em.config.lifetime = 6.f;
        em.config.size = 0.025f;
        em.config.grow = 0.f;
        em.config.color = {1.0f, 0.95f, 0.6f};
        em.config.alpha = 0.85f;
        em.config.gravity = -0.05f;
        em.config.wind = {0.35f, 0.08f, 0.15f};
        em.config.drag = 2.0f;
        em.config.blend = particles::BlendMode::Additive;
        if (!em.system.create(8192, em.config)) {
            std::fprintf(stderr, "particles_screenshot: pollen emitter create failed\n");
            return 1;
        }
    }

    // pre-simulate so the first frame already has a developed system
    for (auto& c : scene.root().children()) {
        if (auto* em = c->find<scene::ParticleEmitterComponent>())
            for (int i = 0; i < static_cast<int>(args.sim * 60.f); ++i) em->system.update(1.f / 60.f);
    }

    // --- sky + renderer ---
    const fuhgl::math::Vec3 skyTop{0.14f, 0.28f, 0.60f}, skyHorizon{0.92f, 0.80f, 0.62f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "particles_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "particles_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 12.f);
        renderer.setShadowZRange(0.5f, 60.f);
        renderer.setShadowFilter(fuhgl::render::ShadowFilter::Pcss);
        renderer.shadowLightSize = 0.5f;
    }
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.godRays.enabled = args.rays;
    renderer.godRays.strength = 0.30f;
    renderer.godRays.decay = 0.94f;
    renderer.godRays.samples = 48;

    scene::FrameData frame;
    scene.fillFrame(frame, nullptr, args.width, args.height);
    if (!args.shadows) {
        frame.effects.disable(render::Effect::Shadows);
        frame.effects.disable(render::Effect::CsmShadows);
    }
    if (!args.particles) frame.effects.disable(render::Effect::Particles);
    frame.effects.applyTo(renderer);
    frame.view.fogColor = skyHorizon;
    frame.view.fogDensity = 0.002f;

    const float dt = 1.f / 60.f;
    for (int i = 0; i < args.frames; ++i) {
        for (auto& c : scene.root().children())
            if (auto* em = c->find<scene::ParticleEmitterComponent>()) em->system.update(dt);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "particles_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    int alive = 0;
    for (const auto* ps : frame.particleSystems) alive += const_cast<particles::ParticleSystem*>(ps)->aliveCount();
    std::fprintf(stderr,
                 "particles_screenshot: wrote %s (%dx%d, %d frames, kind=%s, alive=%d, gpu=%d, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, args.kind.c_str(), alive,
                 gpuParticles ? 1 : 0, fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
