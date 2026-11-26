// Headless G38 smoke test (label "gl_context"): drives a weather::WeatherSystem through a
// scene::WeatherComponent -> FrameData::weather -> DeferredRenderer::render(..., weather)
// and verifies, per feature:
//   A3 Effect::Sky: procedural sky on vs off -> sky pixels differ, between weather states differ
//   A4 clouds: Effect::VolumetricFog with weather coverage clear vs storm -> frames differ
//   A5 rain: G5 rain emitter (spawn ~ rainIntensity) + G22 wetness coupling -> frames differ
//   A6 Effect::Lightning: a flash frame (sky flash + temporary point light) is much brighter
//      than the same frame with the effect off
// Skips cleanly with exit code 77 without GL >= 3.3.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <cmath>
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
    if (!window.create("gl_smoke_weather", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_weather: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_weather: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

struct Frame {
    std::vector<unsigned char> px;
    long sum = 0;
};

// Renders `frames` frames with the given effect toggles; the weather state advances per
// frame and the rain emitter (when set) is reconfigured from the current weather params.
Frame renderWeather(fuhglgraphics::scene::Scene& scene, fuhgl::FirstPersonCamera& cam,
                    fuhglgraphics::render::DeferredRenderer& renderer,
                    fuhglgraphics::weather::WeatherSystem& ws,
                    fuhglgraphics::particles::ParticleSystem* rain, bool sky, bool volumetric,
                    bool wetness, bool particlesOn, bool lightning, int frames) {
    using namespace fuhglgraphics;
    Frame out;
    for (int i = 0; i < frames; ++i) {
        ws.update(1.f / 60.f);
        if (rain) {
            weather::makeRainEmitter(*rain, ws.params());
            rain->update(1.f / 60.f);
        }
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, W, H);
        frame.effects.applyTo(renderer);
        renderer.setEffectEnabled(render::Effect::Sky, sky);
        renderer.setEffectEnabled(render::Effect::VolumetricFog, volumetric);
        renderer.setEffectEnabled(render::Effect::Wetness, wetness);
        renderer.setEffectEnabled(render::Effect::Particles, particlesOn);
        renderer.setEffectEnabled(render::Effect::Lightning, lightning);
        renderer.skyTime += 1.f / 60.f;
        renderer.volumetricTime = renderer.skyTime;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface,
                        frame.weather);
    }
    renderer.output().bind();
    out.px.assign(static_cast<std::size_t>(W) * H * 4, 0);
    fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, out.px.data());
    fuhgl::gl::flush();
    for (std::size_t i = 0; i < out.px.size(); i += 4)
        out.sum += out.px[i] + out.px[i + 1] + out.px[i + 2];
    return out;
}

long pixDiff(const Frame& a, const Frame& b) {
    long d = 0;
    for (std::size_t i = 0; i < a.px.size(); i += 4)
        d += std::labs((long)a.px[i] - (long)b.px[i]) + std::labs((long)a.px[i + 1] - (long)b.px[i + 1]) +
             std::labs((long)a.px[i + 2] - (long)b.px[i + 2]);
    return d;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    scene::Scene scene;
    fuhgl::render::Mesh ground, box;
    fuhgl::render::shapes::quad(ground, 60.f, {0.35f, 0.34f, 0.31f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.34f, 0.31f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("crate" + std::to_string(i));
        n.translation = {-2.f + 2.f * i, 0.8f, -3.f - 2.f * i};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = {0.8f, 0.4f + 0.2f * i, 0.2f};
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }
    // G38 wiring: the weather component owns the state machine; fillFrame() exposes it as
    // frame.weather and applies sun/ambient/fog/effect params
    scene::WeatherComponent& wcomp = scene.root().createChild("weather").attach<scene::WeatherComponent>();
    wcomp.system.setTimeOfDay(10.f);
    // rain emitter (G5) registered through a ParticleEmitterComponent
    scene::ParticleEmitterComponent& rainComp =
        scene.root().createChild("rain").attach<scene::ParticleEmitterComponent>();
    if (!rainComp.system.create(4000, weather::rainEmitterConfig(wcomp.system.params()))) {
        std::fprintf(stderr, "gl_smoke_weather: rain particle system create failed\n");
        return 1;
    }

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.5f, 8.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.5f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.skyTop = {0.16f, 0.32f, 0.64f};
    renderer.skyBottom = {0.88f, 0.84f, 0.74f};
    renderer.volumetricSteps = 24;
    renderer.volumetricMaxDist = 80.f;
    renderer.setEffectEnabled(render::Effect::Tonemap, true);

    weather::WeatherSystem& ws = wcomp.system;

    // --- A3: procedural sky on vs off ---
    ws.setTarget(weather::WeatherType::Clear, 0.f);
    ws.setTimeOfDay(10.f);
    const Frame skyOff = renderWeather(scene, cam, renderer, ws, nullptr, false, false, false, false, false, 4);
    const Frame skyOn = renderWeather(scene, cam, renderer, ws, nullptr, true, false, false, false, false, 4);
    std::fprintf(stderr, "gl_smoke_weather: A3 sky off sum=%ld on sum=%ld diff=%ld\n", skyOff.sum, skyOn.sum,
                 pixDiff(skyOff, skyOn));
    if (skyOn.sum < 1000) { std::fprintf(stderr, "sky-on frame too dark\n"); return 1; }
    if (pixDiff(skyOff, skyOn) < 2000) { std::fprintf(stderr, "A3: sky on/off nearly identical\n"); return 1; }

    // --- A3/A4: weather states drive the sky + the volumetric coverage ---
    ws.setTarget(weather::WeatherType::Thunderstorm, 0.f);
    ws.setTimeOfDay(10.f);
    const Frame stormSky = renderWeather(scene, cam, renderer, ws, nullptr, true, true, false, false, false, 4);
    ws.setTarget(weather::WeatherType::Clear, 0.f);
    ws.setTimeOfDay(10.f);
    const Frame clearVol = renderWeather(scene, cam, renderer, ws, nullptr, true, true, false, false, false, 4);
    std::fprintf(stderr, "gl_smoke_weather: A4 clear-vol sum=%ld storm-vol sum=%ld diff=%ld\n", clearVol.sum,
                 stormSky.sum, pixDiff(clearVol, stormSky));
    if (pixDiff(clearVol, stormSky) < 2000) { std::fprintf(stderr, "A4: clear vs storm (sky+volumetric) nearly identical\n"); return 1; }

    // --- A5: rain particles + wetness coupling ---
    ws.setTarget(weather::WeatherType::Rain, 0.f);
    ws.setTimeOfDay(12.f);
    for (int i = 0; i < 120; ++i) ws.update(1.f / 30.f); // let the wetness lag build up
    const Frame dry = renderWeather(scene, cam, renderer, ws, nullptr, true, true, true, false, false, 4);
    const Frame rainy = renderWeather(scene, cam, renderer, ws, &rainComp.system, true, true, true, true, false, 8);
    std::fprintf(stderr, "gl_smoke_weather: A5 dry sum=%ld rainy sum=%ld diff=%ld alive=%d\n", dry.sum, rainy.sum,
                 pixDiff(dry, rainy), rainComp.system.aliveCount());
    if (rainComp.system.aliveCount() < 50) { std::fprintf(stderr, "A5: rain emitter spawned too few particles\n"); return 1; }
    if (pixDiff(dry, rainy) < 2000) { std::fprintf(stderr, "A5: rain on/off nearly identical\n"); return 1; }

    // --- A6: lightning flash frame vs the same frame with the effect off ---
    ws.setTarget(weather::WeatherType::Thunderstorm, 0.f);
    ws.setTimeOfDay(20.f); // dusk: flash contrast is high
    // advance the seeded schedule until a strike is active (cap the search)
    int steps = 0;
    while (ws.lightningFlash() < 0.6f && steps < 2000) {
        ws.update(0.05f);
        ++steps;
    }
    if (ws.lightningFlash() < 0.6f) {
        std::fprintf(stderr, "A6: no lightning flash within the schedule search\n");
        return 1;
    }
    const Frame flashOff = renderWeather(scene, cam, renderer, ws, nullptr, true, true, false, false, false, 1);
    const Frame flashOn = renderWeather(scene, cam, renderer, ws, nullptr, true, true, false, false, true, 1);
    std::fprintf(stderr, "gl_smoke_weather: A6 flash off sum=%ld on sum=%ld diff=%ld (flash=%.2f)\n", flashOff.sum,
                 flashOn.sum, pixDiff(flashOff, flashOn), ws.lightningFlash());
    if (flashOn.sum <= flashOff.sum + 500) { std::fprintf(stderr, "A6: flash frame not brighter\n"); return 1; }
    if (pixDiff(flashOff, flashOn) < 2000) { std::fprintf(stderr, "A6: flash on/off nearly identical\n"); return 1; }

    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_weather: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_weather: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
