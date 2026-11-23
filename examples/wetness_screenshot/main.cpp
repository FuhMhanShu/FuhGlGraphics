// G22 wetness/rain screenshot tool: a plaza (asphalt ground + crates + an emissive pillar)
// with SSR on, rendered dry / wet / puddly — Effect::Wetness modulates albedo+roughness in the
// lighting pass (up-facing surfaces get a glossy film, noise puddles in depressions) and
// boosts the SSR strength. Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_wetness_screenshot --prefix g22_wetness [--width 1280] [--height 720]
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
    std::string prefix = "g22_wetness";
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
                 bool wet, float amount, float puddle, const std::string& outPath) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.fogDensity = 0.0012f;
        frame.view.fogColor = {0.88f, 0.84f, 0.74f};
        renderer.setEffectEnabled(render::Effect::Wetness, wet);
        renderer.wetnessAmount = amount;
        renderer.wetnessPuddle = puddle;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    fuhgl::gl::readPixels(0, 0, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    const bool ok = fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), width, height);
    std::fprintf(stderr, "wetness_screenshot: wrote %s (wet=%d amount=%.2f puddle=%.2f)\n",
                 outPath.c_str(), (int)wet, amount, puddle);
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
    if (!window.create("fuhglgraphics_wetness_screenshot", 64, 64)) {
        std::fprintf(stderr, "wetness_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "wetness_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    fuhgl::render::Mesh ground, box, pillar;
    fuhgl::render::shapes::quad(ground, 30.f, {0.42f, 0.42f, 0.45f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    fuhgl::render::shapes::box(pillar, {0.4f, 2.6f, 0.4f}, {1, 1, 1});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.42f, 0.42f, 0.45f};
    ge.roughness = 0.85f; // dry asphalt
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
    // emissive pillar: the SSR pass reflects it in the wet ground
    scene::Node& pn = scene.root().createChild("pillar");
    pn.translation = {0.f, 2.6f, -7.f};
    scene::MeshEntry& pe = pn.attach<scene::MeshComponent>().addMesh(&pillar);
    pe.emissive = {6.f, 4.f, 1.f};
    pe.castShadow = false;
    pe.materialId = 20;

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.5f, -0.7f, -0.4f}, {1.0f, 0.92f, 0.78f}, 3.0f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 3.0f, 7.5f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 1.f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ssr); // reflections show the wetness

    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.64f}, skyHorizon{0.88f, 0.84f, 0.74f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "wetness_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.ssrMaxSteps = 32;
    renderer.ssrMaxDist = 30.f;
    renderer.ssrStrength = 0.9f;
    renderer.wetnessNoiseScale = 3.5f;

    bool ok = true;
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height, false, 0.f, 0.f,
                      args.prefix + "_dry.png");
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height, true, 0.9f, 0.3f,
                      args.prefix + "_wet.png");
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height, true, 1.f, 1.f,
                      args.prefix + "_puddles.png");
    if (!ok) return 1;
    std::fprintf(stderr, "wetness_screenshot: done (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
