// G21 volumetric fog/clouds screenshot tool: a terrain scene (TerrainComponent, G20) with
// crates, rendered three times — volumetric fog off, on (default density) and dense —
// headless (surfaceless EGL) to PNG for docs/images/. The "volumetric" post pass raymarches
// each view ray through a 3D-noise volume (height falloff + fBm clouds, Henyey-Greenstein
// sun scattering), ending at the G-buffer depth.
//
// Usage: fuhglgraphics_volumetric_screenshot --prefix g21_volumetric [--width 1280]
//          [--height 720] [--frames 16]
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
    std::string prefix = "g21_volumetric";
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
                 bool fogOn, float density, const std::string& outPath) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.fogDensity = 0.f; // the analytic distance fog stays off — G21 fog only
        renderer.setEffectEnabled(render::Effect::VolumetricFog, fogOn);
        renderer.volumetricDensity = density;
        renderer.volumetricTime = 0.35f * i; // slow cloud drift between frames
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    fuhgl::gl::readPixels(0, 0, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    const bool ok = fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), width, height);
    std::fprintf(stderr, "volumetric_screenshot: wrote %s (fog=%d density=%.3f)\n", outPath.c_str(),
                 (int)fogOn, density);
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
    if (!window.create("fuhglgraphics_volumetric_screenshot", 64, 64)) {
        std::fprintf(stderr, "volumetric_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "volumetric_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    scene::Node& tn = scene.root().createChild("terrain");
    scene::TerrainComponent& tc = tn.attach<scene::TerrainComponent>();
    tc.levels = 3;
    tc.fovYDeg = 50.f;
    tc.seed = 11u;
    if (!tc.create(160, 160, 160.f, 160.f, 10.f)) {
        std::fprintf(stderr, "volumetric_screenshot: terrain create failed\n");
        return 1;
    }

    fuhgl::render::Mesh box;
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    const fuhgl::math::Vec2 spots[4] = {{-10.f, -12.f}, {4.f, -20.f}, {14.f, -6.f}, {-2.f, -34.f}};
    const fuhgl::math::Vec3 colors[4] = {{0.8f, 0.45f, 0.2f}, {0.25f, 0.55f, 0.8f},
                                         {0.3f, 0.75f, 0.35f}, {0.75f, 0.3f, 0.6f}};
    for (int i = 0; i < 4; ++i) {
        scene::Node& n = scene.root().createChild("crate" + std::to_string(i));
        n.translation = {spots[i].x, tc.heightAt(spots[i].x, spots[i].y) + 0.8f, spots[i].y};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = colors[i];
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }

    const fuhgl::math::Vec3 sunDir{0.4f, 0.55f, -0.6f};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.92f, 0.78f}, 3.2f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, tc.heightAt(0.f, 55.f) + 3.5f, 55.f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 4.f, -30.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);

    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.64f}, skyHorizon{0.88f, 0.84f, 0.74f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "volumetric_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.volumetricCloudiness = 0.7f;
    renderer.volumetricHeightFalloff = 10.f;
    renderer.volumetricNoiseScale = 7.f;
    renderer.volumetricAnisotropy = 0.5f;
    renderer.volumetricMaxDist = 140.f;
    renderer.volumetricSteps = 32;
    renderer.volumetricStrength = 1.f;

    bool ok = true;
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height, false, 0.02f,
                      args.prefix + "_off.png");
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height, true, 0.018f,
                      args.prefix + "_on.png");
    ok &= renderToPng(scene, cam, renderer, args.frames, args.width, args.height, true, 0.06f,
                      args.prefix + "_dense.png");
    if (!ok) return 1;
    std::fprintf(stderr, "volumetric_screenshot: done (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
