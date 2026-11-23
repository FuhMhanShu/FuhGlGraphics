// G32/G33/G34 post-effect showcase, headless to PNG:
//  - Chromatic aberration (Effect::ChromaticAberration): dusk plaza with lamps — radial
//    R/B channel split around the frame center, visible at high-contrast edges.
//  - Subsurface scattering (Effect::Subsurface): a thin white panel backlit by the sun —
//    the dark camera side glows (wrap diffuse + translucency); the per-item factor is
//    MeshEntry::subsurface (rides in G-buffer pbr.a).
//  - Screen-space GI (Effect::Gi): white box next to a sunlit red wall — the shadowed side
//    picks up red bounce light from the cones hitting the wall.
// Shots: <prefix>_ca.png / _ca_off.png, <prefix>_sss.png / _sss_off.png, <prefix>_gi.png / _gi_off.png.
//
// Usage: fuhglgraphics_postfx2_screenshot --prefix postfx2 [--width 1280] [--height 720]
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
    std::string prefix = "postfx2";
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
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 4, 0);
    fuhgl::gl::readPixels(0, 0, w, h, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, pixels.data());
    if (!screenshot::writePng(path.c_str(), pixels.data(), w, h)) {
        std::fprintf(stderr, "postfx2_screenshot: failed to write %s\n", path.c_str());
        return false;
    }
    std::fprintf(stderr, "postfx2_screenshot: wrote %s (%dx%d, %d frames, GL %d.%d %s)\n", path.c_str(), w,
                 h, frames, fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return true;
}

// Dusk plaza with HDR lamp cores (for the chromatic aberration edges).
void buildDuskScene(fuhglgraphics::scene::Scene& scene, fuhgl::render::Mesh& ground,
                    fuhgl::render::Mesh& lamp, fuhgl::render::Mesh& crate) {
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
    fx.params.bloomThreshold = 1.0f;
    fx.params.bloomStrength = 0.7f;
    fx.params.bloomRadius = 2.0f;
    fx.params.caStrength = 0.012f; // visible fringe at the 1280px frame edges
}

// Backlit thin panel (for SSS): sun behind the panel, camera in front of it.
void buildSssScene(fuhglgraphics::scene::Scene& scene, fuhgl::render::Mesh& ground,
                   fuhgl::render::Mesh& panel, fuhgl::render::Mesh& block) {
    using namespace fuhglgraphics;
    fuhgl::render::shapes::quad(ground, 12.f, {0.30f, 0.33f, 0.28f});
    fuhgl::render::shapes::box(panel, {1.6f, 2.1f, 0.04f}, {1.f, 1.f, 1.f});
    fuhgl::render::shapes::box(block, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.30f, 0.33f, 0.28f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    // sun behind the panel (-Z), slightly above: the panel's camera side is backlit
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize({0.25f, 0.45f, -1.f});
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.95f, 0.85f}, 3.2f);

    scene::Node& p = scene.root().createChild("panel");
    p.translation = {0.f, 2.1f, 0.f};
    scene::MeshEntry& pe = p.attach<scene::MeshComponent>().addMesh(&panel);
    pe.albedo = {0.92f, 0.90f, 0.82f}; // wax/paper-like
    pe.roughness = 0.55f;
    pe.subsurface = 1.f; // G33: full SSS on the thin panel
    pe.castShadow = true;
    pe.materialId = 2;

    scene::Node& b = scene.root().createChild("block"); // opaque reference box
    b.translation = {2.2f, 0.5f, 0.6f};
    scene::MeshEntry& be = b.attach<scene::MeshComponent>().addMesh(&block);
    be.albedo = {0.5f, 0.5f, 0.5f};
    be.roughness = 0.8f;
    be.subsurface = 0.f;
    be.castShadow = true;
    be.materialId = 3;

    scene::Effects& fx = scene.root().effects;
    fx.enable(render::Effect::Taa);
    fx.enable(render::Effect::Upscale);
    fx.enable(render::Effect::Tonemap);
    fx.enable(render::Effect::CsmShadows);
    fx.params.shadowCenter = {0.f, 1.f, 0.f};
    fx.params.shadowRadius = 10.f;
    fx.params.shadowZFar = 40.f;
    fx.params.sssDistortion = 0.5f;
    fx.params.sssPower = 2.5f;
    fx.params.sssScale = 2.0f;
    fx.params.sssThickness = 1.2f;
}

// Courtyard with a sunlit red wall + white box (for GI bounce).
void buildGiScene(fuhglgraphics::scene::Scene& scene, fuhgl::render::Mesh& ground,
                  fuhgl::render::Mesh& box, fuhgl::render::Mesh& wall) {
    using namespace fuhglgraphics;
    fuhgl::render::shapes::quad(ground, 14.f, {0.42f, 0.40f, 0.36f});
    fuhgl::render::shapes::box(box, {0.7f, 0.7f, 0.7f}, {0.85f, 0.85f, 0.85f});
    fuhgl::render::shapes::box(wall, {0.25f, 1.6f, 3.2f}, {1.f, 0.12f, 0.08f});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.42f, 0.40f, 0.36f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{0.75f, 0.55f, 0.35f}; // from +X: wall's +X face and box lit
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.95f, 0.88f}, 3.0f);

    scene::Node& b = scene.root().createChild("box");
    b.translation = {0.f, 0.7f, 0.f};
    scene::MeshEntry& be = b.attach<scene::MeshComponent>().addMesh(&box);
    be.albedo = {0.85f, 0.85f, 0.85f};
    be.roughness = 0.6f;
    be.castShadow = true;
    be.materialId = 2;

    scene::Node& w = scene.root().createChild("redWall");
    w.translation = {-1.6f, 1.6f, -0.6f};
    scene::MeshEntry& we = w.attach<scene::MeshComponent>().addMesh(&wall);
    we.albedo = {1.f, 0.12f, 0.08f};
    we.roughness = 0.7f;
    we.castShadow = true;
    we.materialId = 3;

    scene::Effects& fx = scene.root().effects;
    fx.enable(render::Effect::Taa);
    fx.enable(render::Effect::Upscale);
    fx.enable(render::Effect::Tonemap);
    fx.enable(render::Effect::CsmShadows);
    fx.params.shadowCenter = {0.f, 1.f, 0.f};
    fx.params.shadowRadius = 10.f;
    fx.params.shadowZFar = 40.f;
    fx.params.giStrength = 0.9f;
    fx.params.giCones = 4;
    fx.params.giSteps = 16;
    fx.params.giMaxDist = 6.f;
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
    if (!window.create("fuhglgraphics_postfx2_screenshot", 64, 64)) {
        std::fprintf(stderr, "postfx2_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "postfx2_screenshot: context below GL 3.3\n");
        return 2;
    }

    fuhgl::Skybox skyDusk, skyDay;
    if (!skyDusk.create() || !skyDay.create()) return 1;
    skyDusk.makeGradient({0.05f, 0.07f, 0.16f}, {0.98f, 0.45f, 0.18f}, 256);
    skyDay.makeGradient({0.18f, 0.34f, 0.66f}, {0.88f, 0.84f, 0.74f}, 256);
    fuhgl::render::Environment envDusk, envDay;
    if (!envDusk.createFromCubemap(skyDusk.texture(), 16, 32, 64) ||
        !envDay.createFromCubemap(skyDay.texture(), 16, 32, 64))
        std::fprintf(stderr, "postfx2_screenshot: IBL bake failed, continuing without env\n");
    envDusk.intensity = 0.5f;
    envDay.intensity = 0.8f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.enableCsmShadows(1024, 4);
    renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 40.f);
    renderer.setShadowZRange(0.5f, 60.f);

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(52.f);
    bool ok = true;

    // --- G32 chromatic aberration: dusk plaza, on vs off ---
    {
        scene::Scene scene;
        fuhgl::render::Mesh ground, lamp, crate;
        buildDuskScene(scene, ground, lamp, crate);
        scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
        renderer.setSkyCubemap(&skyDusk.texture());
        renderer.skyTop = {0.05f, 0.07f, 0.16f};
        renderer.skyBottom = {0.98f, 0.45f, 0.18f};
        const fuhgl::math::Vec3 pos{0.5f, 2.0f, 8.5f}, target{0.f, 1.0f, -2.f};
        const fuhgl::math::Vec3 fog{0.10f, 0.07f, 0.12f};
        ok &= shoot(scene, cam, renderer, &envDusk, pos, target, args.width, args.height, args.frames,
                    args.scale, fog, 0.004f,
                    [](scene::FrameData& f) { f.effects.enable(render::Effect::ChromaticAberration); },
                    args.prefix + "_ca.png");
        ok &= shoot(scene, cam, renderer, &envDusk, pos, target, args.width, args.height, args.frames,
                    args.scale, fog, 0.004f,
                    [](scene::FrameData& f) { f.effects.disable(render::Effect::ChromaticAberration); },
                    args.prefix + "_ca_off.png");
    }

    // --- G33 subsurface scattering: backlit panel, on vs off ---
    {
        scene::Scene scene;
        fuhgl::render::Mesh ground, panel, block;
        buildSssScene(scene, ground, panel, block);
        scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
        renderer.setSkyCubemap(&skyDay.texture());
        renderer.skyTop = {0.18f, 0.34f, 0.66f};
        renderer.skyBottom = {0.88f, 0.84f, 0.74f};
        const fuhgl::math::Vec3 pos{0.f, 1.7f, 6.0f}, target{0.f, 1.8f, 0.f};
        const fuhgl::math::Vec3 fog{0.88f, 0.84f, 0.74f};
        ok &= shoot(scene, cam, renderer, &envDay, pos, target, args.width, args.height, args.frames,
                    args.scale, fog, 0.002f,
                    [](scene::FrameData& f) { f.effects.enable(render::Effect::Subsurface); },
                    args.prefix + "_sss.png");
        ok &= shoot(scene, cam, renderer, &envDay, pos, target, args.width, args.height, args.frames,
                    args.scale, fog, 0.002f,
                    [](scene::FrameData& f) { f.effects.disable(render::Effect::Subsurface); },
                    args.prefix + "_sss_off.png");
    }

    // --- G34 screen-space GI: red wall bounce on the box's shadow side, on vs off ---
    {
        scene::Scene scene;
        fuhgl::render::Mesh ground, box, wall;
        buildGiScene(scene, ground, box, wall);
        scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
        renderer.setSkyCubemap(&skyDay.texture());
        renderer.skyTop = {0.18f, 0.34f, 0.66f};
        renderer.skyBottom = {0.88f, 0.84f, 0.74f};
        const fuhgl::math::Vec3 pos{3.2f, 2.0f, 4.5f}, target{-0.4f, 0.7f, -0.3f};
        const fuhgl::math::Vec3 fog{0.88f, 0.84f, 0.74f};
        ok &= shoot(scene, cam, renderer, &envDay, pos, target, args.width, args.height, args.frames,
                    args.scale, fog, 0.002f,
                    [](scene::FrameData& f) { f.effects.enable(render::Effect::Gi); },
                    args.prefix + "_gi.png");
        ok &= shoot(scene, cam, renderer, &envDay, pos, target, args.width, args.height, args.frames,
                    args.scale, fog, 0.002f,
                    [](scene::FrameData& f) { f.effects.disable(render::Effect::Gi); },
                    args.prefix + "_gi_off.png");
    }
    return ok ? 0 : 1;
}
