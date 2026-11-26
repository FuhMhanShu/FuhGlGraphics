// G38 weather screenshot tool: a plaza (ground + crates + emissive pillar) driven by a
// weather::WeatherSystem through a scene::WeatherComponent -> FrameData::weather ->
// DeferredRenderer::render(..., weather). Renders one PNG for a given weather state:
// Effect::Sky (procedural sky + clouds), Effect::VolumetricFog (weather-driven coverage),
// G5 rain particles (spawn ~ rainIntensity) + G22 wetness coupling, Effect::Lightning
// (sky flash + temporary point light + optional L-system bolt mesh with --flash), bloom,
// TAA + ACES. Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_weather_screenshot --out g38_weather_rain.png --weather rain --tod 12
//          [--transition 8] [--frames 24] [--width 1280] [--height 720] [--seed 7] [--flash]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out = "g38_weather.png";
    std::string weather = "clear";
    float tod = 10.f;
    float transition = 0.f;
    int frames = 16;
    int width = 1280, height = 720;
    unsigned seed = 7u;
    bool flash = false;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--weather") == 0 && i + 1 < argc) a.weather = argv[++i];
        else if (std::strcmp(argv[i], "--tod") == 0 && i + 1 < argc) a.tod = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--transition") == 0 && i + 1 < argc) a.transition = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) a.seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--flash") == 0) a.flash = true;
    }
    return a;
}

bool parseWeather(const std::string& name, fuhglgraphics::weather::WeatherType& out) {
    if (name == "clear") out = fuhglgraphics::weather::WeatherType::Clear;
    else if (name == "partly" || name == "partly_cloudy") out = fuhglgraphics::weather::WeatherType::PartlyCloudy;
    else if (name == "overcast") out = fuhglgraphics::weather::WeatherType::Overcast;
    else if (name == "rain") out = fuhglgraphics::weather::WeatherType::Rain;
    else if (name == "storm" || name == "thunderstorm") out = fuhglgraphics::weather::WeatherType::Thunderstorm;
    else return false;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    using namespace fuhglgraphics;
    const Args args = parseArgs(argc, argv);
    weather::WeatherType weatherType;
    if (!parseWeather(args.weather, weatherType)) {
        std::fprintf(stderr, "weather_screenshot: unknown --weather '%s' (clear|partly|overcast|rain|storm)\n",
                     args.weather.c_str());
        return 2;
    }
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_weather_screenshot", 64, 64)) {
        std::fprintf(stderr, "weather_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "weather_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    fuhgl::render::Mesh ground, box, pillar, boltMesh;
    fuhgl::render::shapes::quad(ground, 60.f, {0.42f, 0.42f, 0.45f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    fuhgl::render::shapes::box(pillar, {0.4f, 2.6f, 0.4f}, {1, 1, 1});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.42f, 0.42f, 0.45f};
    ge.roughness = 0.9f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 crateColors[3] = {{0.8f, 0.45f, 0.2f}, {0.25f, 0.55f, 0.8f}, {0.3f, 0.75f, 0.35f}};
    const fuhgl::math::Vec3 pos[3] = {{-2.2f, 0.8f, -1.5f}, {0.4f, 0.8f, -3.0f}, {2.4f, 0.8f, -0.8f}};
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("crate" + std::to_string(i));
        n.translation = pos[i];
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = crateColors[i];
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }
    scene::Node& pn = scene.root().createChild("pillar");
    pn.translation = {0.f, 2.6f, -7.f};
    scene::MeshEntry& pe = pn.attach<scene::MeshComponent>().addMesh(&pillar);
    pe.emissive = {6.f, 4.f, 1.f};
    pe.castShadow = false;
    pe.materialId = 20;

    // G38 wiring: weather component owns the state machine (frame.weather), rain emitter (G5)
    scene::WeatherComponent& wcomp = scene.root().createChild("weather").attach<scene::WeatherComponent>();
    wcomp.system = weather::WeatherSystem(args.seed);
    wcomp.system.setTimeOfDay(args.tod);
    wcomp.system.setTarget(weatherType, args.transition);
    scene::ParticleEmitterComponent& rainComp =
        scene.root().createChild("rain").attach<scene::ParticleEmitterComponent>();
    if (!rainComp.system.create(6000, weather::rainEmitterConfig(wcomp.system.params()))) {
        std::fprintf(stderr, "weather_screenshot: rain particle system create failed\n");
        return 1;
    }

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 3.2f, 8.5f};
    cam.fovY = fuhgl::math::radians(52.f);
    const fuhgl::math::Vec3 look{0.f, 2.5f, -1.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(look - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // effects: the G38 set plus bloom (picks up the sun disk / lightning bolt)
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Sky);
    scene.root().effects.enable(render::Effect::VolumetricFog);
    scene.root().effects.enable(render::Effect::Wetness);
    scene.root().effects.enable(render::Effect::Particles);
    scene.root().effects.enable(render::Effect::Lightning);
    scene.root().effects.enable(render::Effect::Bloom);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, true);
    renderer.bloomThreshold = 1.2f;
    renderer.bloomStrength = 0.7f;
    renderer.volumetricSteps = 24;
    renderer.volumetricMaxDist = 120.f;

    // --flash: advance the seeded schedule until a strike is active, then render that frame
    // with the emissive L-system bolt mesh in front of the camera
    scene::Node* boltNode = nullptr;
    if (args.flash) {
        int steps = 0;
        while (wcomp.system.lightningFlash() < 0.5f && steps < 4000) {
            wcomp.system.update(0.05f);
            ++steps;
        }
        if (wcomp.system.lightningFlash() < 0.5f) {
            std::fprintf(stderr, "weather_screenshot: --flash found no strike within the search\n");
            return 1;
        }
        const fuhgl::math::Vec3 dir = wcomp.system.lightningDirection();
        boltMesh.setMeshData(weather::makeLightningBolt(args.seed * 7u + 1u, dir, 60.f));
        boltNode = &scene.root().createChild("bolt");
        // place the bolt root high IN FRONT of the camera (the random strike azimuth would
        // otherwise put it outside the view cone); it grows along dir toward the ground
        const fuhgl::math::Vec3 fwd = fuhgl::math::normalize(look - cam.position);
        boltNode->translation = cam.position + fwd * 24.f + fuhgl::math::Vec3{0.f, 28.f, 0.f};
        scene::MeshEntry& be = boltNode->attach<scene::MeshComponent>().addMesh(&boltMesh);
        be.emissive = {14.f, 16.f, 22.f}; // HDR-bright: bloom picks it up
        be.castShadow = false;
        be.materialId = 30;
    }

    // --flash: the strike envelope decays in ~0.6 s of sim time, so render exactly the
    // frame in which the strike was found (not a later, decayed one)
    const int renderFrames = args.flash ? 1 : args.frames;
    for (int i = 0; i < renderFrames; ++i) {
        const float dt = 1.f / 30.f;
        wcomp.system.update(dt);
        weather::makeRainEmitter(rainComp.system, wcomp.system.params());
        rainComp.system.update(dt);
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, args.width, args.height);
        frame.effects.applyTo(renderer);
        renderer.skyTime += dt;
        renderer.volumetricTime = renderer.skyTime;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface,
                        frame.weather);
    }

    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "weather_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "weather_screenshot: wrote %s (weather=%s tod=%.1f flash=%.2f)\n", args.out.c_str(),
                 args.weather.c_str(), wcomp.system.timeOfDay(), wcomp.system.lightningFlash());
    std::fprintf(stderr, "weather_screenshot: done (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
