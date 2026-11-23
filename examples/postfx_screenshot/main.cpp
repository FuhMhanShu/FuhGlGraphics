// G24/G25 post-effect showcase, headless to PNG:
//  - Lens flare (Effect::LensFlare) + anamorphic streaks (Effect::Anamorphic): dusk plaza
//    with a low HDR sun and lamp cores, camera aimed at the sun -> glow + ghosts + vertical
//    diffraction streak + horizontal anamorphic streak through every bright light.
//  - HBAO (Effect::Hbao, horizon-based AO) vs G15 SSAO: courtyard with crates + wall,
//    the contact corners darken with either, HBAO follows the horizon of the occluders.
// Shots: <prefix>_flare.png (flare+anamorphic on), <prefix>_noflare.png (off, comparison),
//        <prefix>_hbao.png, <prefix>_ssao.png.
//
// Usage: fuhglgraphics_postfx_screenshot --prefix postfx [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "../screenshot/png_writer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string prefix = "postfx";
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
    }
    return a;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

// Renders `frames` frames of `scene` with the camera at `pos` looking at `target`, applies the
// per-shot effect tweaks, and writes the PNG. Returns false on write failure.
bool shoot(fuhglgraphics::scene::Scene& scene, fuhgl::FirstPersonCamera& cam,
           fuhglgraphics::render::DeferredRenderer& renderer, const fuhgl::render::Environment* env,
           const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& target, int w, int h, int frames,
           float scale, const fuhgl::math::Vec3& fogColor, float fogDensity,
           const std::function<void(fuhglgraphics::scene::FrameData&)>& tweak, const std::string& path) {
    using namespace fuhglgraphics;
    cam.position = pos;
    aimAt(cam, target);
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, w, h);
        frame.view.env = env;
        frame.view.fogColor = fogColor;
        frame.view.fogDensity = fogDensity;
        if (tweak) tweak(frame);
        frame.effects.applyTo(renderer);
        renderer.setRenderScale(scale);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 4, 0);
    fuhgl::gl::readPixels(0, 0, w, h, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, pixels.data());
    if (!screenshot::writePng(path.c_str(), pixels.data(), w, h)) {
        std::fprintf(stderr, "postfx_screenshot: failed to write %s\n", path.c_str());
        return false;
    }
    std::fprintf(stderr, "postfx_screenshot: wrote %s (%dx%d, %d frames, GL %d.%d %s)\n", path.c_str(), w,
                 h, frames, fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return true;
}

// Dusk plaza: dark ground, HDR-bright lamp cores, crates, low sun toward -X/-Z.
void buildDuskScene(fuhglgraphics::scene::Scene& scene, fuhgl::render::Mesh& ground,
                    fuhgl::render::Mesh& lamp, fuhgl::render::Mesh& crate,
                    fuhgl::math::Vec3& sunDirOut) {
    using namespace fuhglgraphics;
    fuhgl::render::shapes::quad(ground, 40.f, {0.10f, 0.10f, 0.12f});
    fuhgl::render::shapes::box(lamp, {0.22f, 0.22f, 0.22f}, {1.f, 0.85f, 0.55f});
    fuhgl::render::shapes::box(crate, {0.8f, 0.8f, 0.8f}, {0.35f, 0.28f, 0.22f});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.10f, 0.10f, 0.12f};
    ge.roughness = 0.9f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{std::cos(fuhgl::math::radians(205.f)) * std::cos(fuhgl::math::radians(12.f)),
                                   std::sin(fuhgl::math::radians(12.f)),
                                   std::sin(fuhgl::math::radians(205.f)) * std::cos(fuhgl::math::radians(12.f))};
    sunDirOut = sunDir;
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.55f, 0.25f}, 2.6f);
    scene.root().createChild("fill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({0.f, 4.f, 2.f}, {0.2f, 0.3f, 0.6f}, 30.f, 30.f);

    auto addLamp = [&](const char* name, float x, float z) {
        scene::Node& n = scene.root().createChild(name);
        n.translation = {x, 0.9f, z};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&lamp);
        e.albedo = {1.f, 0.85f, 0.55f};
        e.emissive = {9.f, 5.f, 1.5f};
        e.roughness = 0.3f;
        e.materialId = 2;
    };
    addLamp("lamp1", -3.0f, -1.5f);
    addLamp("lamp2", 0.5f, -3.5f);
    addLamp("lamp3", 3.2f, -0.8f);

    auto addCrate = [&](const char* name, float x, float z, float s) {
        scene::Node& n = scene.root().createChild(name);
        n.translation = {x, 0.8f * s, z};
        n.scale = {s, s, s};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&crate);
        e.albedo = {0.35f, 0.28f, 0.22f};
        e.roughness = 0.7f;
        e.materialId = 3;
    };
    addCrate("crate1", -1.2f, 1.5f, 1.f);
    addCrate("crate2", 1.8f, 2.2f, 0.7f);

    scene::Effects& fx = scene.root().effects;
    fx.enable(render::Effect::Taa);
    fx.enable(render::Effect::Upscale);
    fx.enable(render::Effect::Tonemap);
    fx.enable(render::Effect::Bloom);
    fx.enable(render::Effect::CsmShadows);
    fx.params.shadowCenter = {0.f, 1.f, 0.f};
    fx.params.shadowRadius = 14.f;
    fx.params.shadowZFar = 50.f;
    fx.params.bloomThreshold = 1.0f;
    fx.params.bloomStrength = 0.7f;
    fx.params.bloomRadius = 2.0f;
    fx.params.lensFlareStrength = 1.0f;
    fx.params.anamorphicStrength = 0.55f;
    fx.params.anamorphicStretch = 12.f;
    fx.params.anamorphicThreshold = 0.9f;
}

// Daylight courtyard: ground, crates, a wall — for the HBAO vs SSAO comparison.
void buildCourtyardScene(fuhglgraphics::scene::Scene& scene, fuhgl::render::Mesh& ground,
                         fuhgl::render::Mesh& box, fuhgl::render::Mesh& wall) {
    using namespace fuhglgraphics;
    fuhgl::render::shapes::quad(ground, 30.f, {0.42f, 0.40f, 0.36f});
    fuhgl::render::shapes::box(box, {0.6f, 0.6f, 0.6f}, {0.8f, 0.55f, 0.3f});
    fuhgl::render::shapes::box(wall, {4.f, 1.6f, 0.25f}, {0.55f, 0.53f, 0.48f});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.42f, 0.40f, 0.36f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{0.55f, 0.62f, 0.35f};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.9f, 0.75f}, 3.0f);

    auto addBox = [&](const char* name, float x, float y, float z, float s) {
        scene::Node& n = scene.root().createChild(name);
        n.translation = {x, y, z};
        n.scale = {s, s, s};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = {0.8f, 0.55f, 0.3f};
        e.roughness = 0.6f;
        e.materialId = 2;
    };
    addBox("crate1", -1.6f, 0.6f, -0.4f, 1.f);
    addBox("crate2", -0.7f, 0.6f, 0.5f, 1.f);
    addBox("crate3", -1.1f, 1.8f, 0.0f, 1.f);
    scene::Node& wn = scene.root().createChild("wall");
    wn.translation = {2.8f, 1.6f, -1.2f};
    scene::MeshEntry& we = wn.attach<scene::MeshComponent>().addMesh(&wall);
    we.albedo = {0.55f, 0.53f, 0.48f};
    we.roughness = 0.9f;
    we.materialId = 3;

    scene::Effects& fx = scene.root().effects;
    fx.enable(render::Effect::Taa);
    fx.enable(render::Effect::Upscale);
    fx.enable(render::Effect::Tonemap);
    fx.enable(render::Effect::CsmShadows);
    fx.params.shadowCenter = {0.f, 1.f, 0.f};
    fx.params.shadowRadius = 10.f;
    fx.params.shadowZFar = 40.f;
    fx.params.ssaoRadius = 1.2f;
    fx.params.ssaoIntensity = 1.6f;
    fx.params.ssaoSamples = 32;
    fx.params.hbaoRadius = 1.4f;
    fx.params.hbaoDirections = 8;
    fx.params.hbaoSteps = 4;
    fx.params.hbaoPower = 1.8f;
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
    if (!window.create("fuhglgraphics_postfx_screenshot", 64, 64)) {
        std::fprintf(stderr, "postfx_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "postfx_screenshot: context below GL 3.3\n");
        return 2;
    }

    // skies: dusk gradient (sun side hot) + daylight gradient, both as cubemaps (+ IBL envs)
    fuhgl::Skybox skyDusk, skyDay;
    if (!skyDusk.create() || !skyDay.create()) return 1;
    skyDusk.makeGradient({0.05f, 0.07f, 0.16f}, {0.98f, 0.45f, 0.18f}, 256);
    skyDay.makeGradient({0.18f, 0.34f, 0.66f}, {0.88f, 0.84f, 0.74f}, 256);
    fuhgl::render::Environment envDusk, envDay;
    if (!envDusk.createFromCubemap(skyDusk.texture(), 16, 32, 64) ||
        !envDay.createFromCubemap(skyDay.texture(), 16, 32, 64))
        std::fprintf(stderr, "postfx_screenshot: IBL bake failed, continuing without env\n");
    envDusk.intensity = 0.5f;
    envDay.intensity = 0.8f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.enableCsmShadows(1024, 4);
    renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 40.f);
    renderer.setShadowZRange(0.5f, 60.f);
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.3f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 40;

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(52.f);
    bool ok = true;

    // --- G24: dusk plaza, camera aimed at the low sun ---
    {
        scene::Scene scene;
        fuhgl::render::Mesh ground, lamp, crate;
        fuhgl::math::Vec3 sunDir;
        buildDuskScene(scene, ground, lamp, crate, sunDir);
        scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

        const fuhgl::math::Vec3 camPos{0.5f, 2.0f, 8.5f};
        // sun off-center (left third): the lens-flare ghosts lie on the axis sun<->screen
        // center, so they are only distinct when the sun is not in the exact middle
        const fuhgl::math::Vec3 right =
            fuhgl::math::normalize(fuhgl::math::cross(sunDir, {0.f, 1.f, 0.f}));
        const fuhgl::math::Vec3 camTarget = camPos + sunDir * 12.f + right * 4.5f;
        const fuhgl::math::Vec3 fog{0.10f, 0.07f, 0.12f};

        renderer.setSkyCubemap(&skyDusk.texture());
        renderer.skyTop = {0.05f, 0.07f, 0.16f};
        renderer.skyBottom = {0.98f, 0.45f, 0.18f};

        ok &= shoot(scene, cam, renderer, &envDusk, camPos, camTarget, args.width, args.height,
                    args.frames, args.scale, fog, 0.004f,
                    [](scene::FrameData& f) {
                        f.effects.enable(render::Effect::LensFlare);
                        f.effects.enable(render::Effect::Anamorphic);
                    },
                    args.prefix + "_flare.png");
        ok &= shoot(scene, cam, renderer, &envDusk, camPos, camTarget, args.width, args.height,
                    args.frames, args.scale, fog, 0.004f,
                    [](scene::FrameData& f) {
                        f.effects.disable(render::Effect::LensFlare);
                        f.effects.disable(render::Effect::Anamorphic);
                    },
                    args.prefix + "_noflare.png");
    }

    // --- G25: daylight courtyard, HBAO vs SSAO ---
    {
        scene::Scene scene;
        fuhgl::render::Mesh ground, box, wall;
        buildCourtyardScene(scene, ground, box, wall);
        scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

        const fuhgl::math::Vec3 camPos{0.5f, 2.6f, 7.5f};
        const fuhgl::math::Vec3 camTarget{-0.5f, 0.8f, -0.5f};
        const fuhgl::math::Vec3 fog{0.88f, 0.84f, 0.74f};

        renderer.setSkyCubemap(&skyDay.texture());
        renderer.skyTop = {0.18f, 0.34f, 0.66f};
        renderer.skyBottom = {0.88f, 0.84f, 0.74f};

        ok &= shoot(scene, cam, renderer, &envDay, camPos, camTarget, args.width, args.height, args.frames,
                    args.scale, fog, 0.002f,
                    [](scene::FrameData& f) {
                        f.effects.disable(render::Effect::Bloom);
                        f.effects.enable(render::Effect::Hbao);
                        f.effects.disable(render::Effect::Ssao);
                    },
                    args.prefix + "_hbao.png");
        ok &= shoot(scene, cam, renderer, &envDay, camPos, camTarget, args.width, args.height, args.frames,
                    args.scale, fog, 0.002f,
                    [](scene::FrameData& f) {
                        f.effects.disable(render::Effect::Bloom);
                        f.effects.disable(render::Effect::Hbao);
                        f.effects.enable(render::Effect::Ssao);
                    },
                    args.prefix + "_ssao.png");
    }
    return ok ? 0 : 1;
}
