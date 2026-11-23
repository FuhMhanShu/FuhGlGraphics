// G23 tonemapping screenshot tool: a dusk plaza with HDR-bright lamps + low sun, rendered
// once per operator (ACES / Reinhard / Uncharted2 / linear, render::TonemapOperator via
// uOperator in the tonemap pass) plus one shot with bloom on and a high bloom threshold —
// headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_tonemap_screenshot --prefix g23_tonemap [--width 1280] [--height 720]
//          [--frames 16]
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
    std::string prefix = "g23_tonemap";
    int width = 1280, height = 720, frames = 16;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
    }
    return a;
}

bool renderToPng(fuhglgraphics::scene::Scene& scn, fuhgl::FirstPersonCamera& cam,
                 fuhglgraphics::render::DeferredRenderer& renderer, int frames, int width, int height,
                 fuhglgraphics::render::TonemapOperator op, bool bloom, float bloomThreshold,
                 const std::string& outPath) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.fogDensity = 0.002f;
        frame.view.fogColor = {0.24f, 0.18f, 0.26f};
        renderer.tonemapOperator = op;
        renderer.setEffectEnabled(render::Effect::Bloom, bloom);
        renderer.bloomThreshold = bloomThreshold;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    fuhgl::gl::readPixels(0, 0, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    const bool ok = fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), width, height);
    std::fprintf(stderr, "tonemap_screenshot: wrote %s (op=%s bloom=%d thr=%.1f)\n", outPath.c_str(),
                 render::tonemapOperatorName(op), (int)bloom, bloomThreshold);
    return ok;
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
    if (!window.create("fuhglgraphics_tonemap_screenshot", 64, 64)) {
        std::fprintf(stderr, "tonemap_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "tonemap_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    fuhgl::render::Mesh ground, crate, lamp;
    fuhgl::render::shapes::quad(ground, 30.f, {0.3f, 0.28f, 0.3f});
    fuhgl::render::shapes::box(crate, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    fuhgl::render::shapes::box(lamp, {0.22f, 0.22f, 0.22f}, {1.f, 0.85f, 0.55f});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.3f, 0.28f, 0.3f};
    ge.roughness = 0.9f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{std::cos(fuhgl::math::radians(205.f)) * std::cos(fuhgl::math::radians(12.f)),
                                   std::sin(fuhgl::math::radians(12.f)),
                                   std::sin(fuhgl::math::radians(205.f)) * std::cos(fuhgl::math::radians(12.f))};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.55f, 0.25f}, 2.2f);
    scene.root().createChild("fill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({0.3f, -0.6f, 0.7f}, {0.2f, 0.3f, 0.5f}, 0.6f);

    auto addLamp = [&](const char* name, float x, float z) {
        scene::Node& n = scene.root().createChild(name);
        n.translation = {x, 1.1f, z};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&lamp);
        e.emissive = {9.f, 5.f, 1.5f}; // HDR-bright lamp core
        e.castShadow = false;
        e.materialId = 30;
    };
    addLamp("lamp1", -3.0f, -1.5f);
    addLamp("lamp2", 0.5f, -3.5f);
    addLamp("lamp3", 3.2f, -0.8f);

    const fuhgl::math::Vec3 crateColors[3] = {{0.8f, 0.45f, 0.2f}, {0.25f, 0.55f, 0.8f}, {0.3f, 0.75f, 0.35f}};
    const fuhgl::math::Vec3 pos[3] = {{-2.2f, 0.8f, -1.5f}, {0.4f, 0.8f, -3.0f}, {2.4f, 0.8f, -0.8f}};
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("crate" + std::to_string(i));
        n.translation = pos[i];
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&crate);
        e.albedo = crateColors[i];
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 2.2f, 6.5f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 1.f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);

    const fuhgl::math::Vec3 skyTop{0.05f, 0.08f, 0.22f}, skyHorizon{0.55f, 0.32f, 0.2f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "tonemap_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.exposure = 1.1f;

    bool ok = true;
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height,
                      render::TonemapOperator::Aces, false, 1.f, args.prefix + "_aces.png");
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height,
                      render::TonemapOperator::Reinhard, false, 1.f, args.prefix + "_reinhard.png");
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height,
                      render::TonemapOperator::Uncharted2, false, 1.f, args.prefix + "_uncharted2.png");
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height,
                      render::TonemapOperator::Linear, false, 1.f, args.prefix + "_linear.png");
    // adjustable bloom threshold (G17) on top of ACES: high threshold -> only the lamp cores bloom
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height,
                      render::TonemapOperator::Aces, true, 4.f, args.prefix + "_bloomthr4.png");
    if (!ok) return 1;
    std::fprintf(stderr, "tonemap_screenshot: done (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
