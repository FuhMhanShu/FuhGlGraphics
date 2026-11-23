// G17 bloom screenshot tool: a dusk plaza with several HDR-bright lamps (emissive boxes)
// and the sun disk — the G3 "bloom" post pass (bright-pass above bloomThreshold + 9-tap blur
// + add, before TAA; Effect::Bloom) creates the halos. --no-bloom disables the effect,
// --threshold N sets the bright-pass threshold. Renders headless (surfaceless EGL) to PNG.
//
// Usage: fuhglgraphics_bloom_screenshot --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--no-bloom] [--threshold 1.0] [--strength 0.6]
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
    std::string out = "bloom.png";
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f, threshold = 1.0f, strength = 0.7f;
    bool bloom = true, shadows = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) a.threshold = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--strength") == 0 && i + 1 < argc) a.strength = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-bloom") == 0) a.bloom = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
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
    if (!window.create("fuhglgraphics_bloom_screenshot", 64, 64)) {
        std::fprintf(stderr, "bloom_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "bloom_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    fuhgl::render::Mesh ground, lamp, crate;
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

    // low sun: the sun disk is HDR-bright and blooms too
    const fuhgl::math::Vec3 sunDir{std::cos(fuhgl::math::radians(205.f)) * std::cos(fuhgl::math::radians(12.f)),
                                   std::sin(fuhgl::math::radians(12.f)),
                                   std::sin(fuhgl::math::radians(205.f)) * std::cos(fuhgl::math::radians(12.f))};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.55f, 0.25f}, 2.2f);
    scene.root().createChild("fill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({0.f, 4.f, 2.f}, {0.2f, 0.3f, 0.6f}, 30.f, 30.f);

    auto addLamp = [&](const char* name, float x, float z) {
        scene::Node& n = scene.root().createChild(name);
        n.translation = {x, 0.9f, z};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&lamp);
        e.albedo = {1.f, 0.85f, 0.55f};
        e.emissive = {9.f, 5.f, 1.5f}; // HDR-bright lamp core
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

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 2.0f, 8.5f};
    cam.fovY = fuhgl::math::radians(52.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.9f, -2.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    if (args.bloom) scene.root().effects.enable(render::Effect::Bloom);
    if (args.shadows) scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.shadowCenter = {0.f, 1.f, 0.f};
    scene.root().effects.params.shadowRadius = 14.f;
    scene.root().effects.params.shadowZFar = 50.f;

    // dusk sky
    const fuhgl::math::Vec3 skyTop{0.05f, 0.07f, 0.16f}, skyHorizon{0.98f, 0.45f, 0.18f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "bloom_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 14.f);
        renderer.setShadowZRange(0.5f, 50.f);
    }
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.3f;

    for (int i = 0; i < args.frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        // params after fillFrame: the per-node merge would overwrite root-level params
        frame.effects.params.bloomThreshold = args.threshold;
        frame.effects.params.bloomStrength = args.strength;
        frame.effects.params.bloomRadius = 2.0f;
        frame.effects.applyTo(renderer);
        frame.view.fogColor = {0.10f, 0.07f, 0.12f};
        frame.view.fogDensity = 0.004f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "bloom_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "bloom_screenshot: wrote %s (%dx%d, %d frames, bloom=%d thr=%.1f, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, (int)args.bloom, args.threshold,
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
