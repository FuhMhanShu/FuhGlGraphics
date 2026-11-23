// G20 terrain + LOD screenshot tool: a TerrainComponent (FuhGLCore F38/F41 heightmap terrain
// as a LodMesh) with crates placed via heightAt(), rendered near (LOD level 0) and far
// (higher LOD level) — the chosen level is printed (SceneView::lastLodLevel). Headless
// (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_terrain_screenshot --prefix g20_terrain [--width 1280] [--height 720]
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
    std::string prefix = "g20_terrain";
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

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

bool renderToPng(fuhglgraphics::scene::Scene& scn, fuhgl::FirstPersonCamera& cam,
                 fuhglgraphics::render::DeferredRenderer& renderer, int frames, int width, int height,
                 const std::string& outPath, int* outLodLevel) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = {0.88f, 0.84f, 0.74f};
        frame.view.fogDensity = 0.0003f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins);
        if (outLodLevel) *outLodLevel = frame.view.lastLodLevel;
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    fuhgl::gl::readPixels(0, 0, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    return fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), width, height);
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
    if (!window.create("fuhglgraphics_terrain_screenshot", 64, 64)) {
        std::fprintf(stderr, "terrain_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "terrain_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    scene::Node& tn = scene.root().createChild("terrain");
    scene::TerrainComponent& tc = tn.attach<scene::TerrainComponent>();
    tc.levels = 4;
    tc.fovYDeg = 50.f;
    tc.seed = 11u;
    tc.customDistances = {60.f, 150.f, 300.f}; // bring the switch distances closer for the demo
    if (!tc.create(160, 160, 160.f, 160.f, 12.f)) {
        std::fprintf(stderr, "terrain_screenshot: terrain create failed\n");
        return 1;
    }
    std::fprintf(stderr, "terrain_screenshot: %d LOD levels, switch distances:", tc.lodMesh.levelCount());
    for (float d : tc.lodMesh.distances()) std::fprintf(stderr, " %.0f", d);
    std::fprintf(stderr, "\n");

    // crates placed on the terrain via heightAt()
    fuhgl::render::Mesh box;
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    const fuhgl::math::Vec2 spots[5] = {{-8.f, -6.f}, {6.f, -10.f}, {12.f, 4.f}, {-14.f, 8.f}, {0.f, -18.f}};
    const fuhgl::math::Vec3 colors[5] = {{0.8f, 0.45f, 0.2f}, {0.25f, 0.55f, 0.8f}, {0.3f, 0.75f, 0.35f},
                                         {0.75f, 0.3f, 0.6f}, {0.85f, 0.8f, 0.3f}};
    for (int i = 0; i < 5; ++i) {
        scene::Node& n = scene.root().createChild("crate" + std::to_string(i));
        n.translation = {spots[i].x, tc.heightAt(spots[i].x, spots[i].y) + 0.8f, spots[i].y};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = colors[i];
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }

    const fuhgl::math::Vec3 sunDir{0.5f, 0.7f, 0.4f};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.92f, 0.78f}, 3.0f);

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(50.f);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);

    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.64f}, skyHorizon{0.88f, 0.84f, 0.74f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "terrain_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    // near shot: close to the crates -> LOD level 0
    cam.position = {10.f, tc.heightAt(10.f, 45.f) + 4.f, 45.f};
    aimAt(cam, {0.f, 4.f, -6.f});
    int lodNear = -2;
    const std::string nearPath = args.prefix + "_near.png";
    if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, nearPath, &lodNear)) {
        std::fprintf(stderr, "terrain_screenshot: failed to write %s\n", nearPath.c_str());
        return 1;
    }
    std::fprintf(stderr, "terrain_screenshot: wrote %s (lastLodLevel=%d)\n", nearPath.c_str(), lodNear);

    // far shot: oblique overview from (0,70,430) — dist ~436 > 300 -> LOD level 3,
    // hills silhouette against the sky
    cam.position = {0.f, 70.f, 430.f};
    aimAt(cam, {0.f, 4.f, 0.f});
    int lodFar = -2;
    const std::string farPath = args.prefix + "_far.png";
    if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, farPath, &lodFar)) {
        std::fprintf(stderr, "terrain_screenshot: failed to write %s\n", farPath.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "terrain_screenshot: wrote %s (lastLodLevel=%d, GL %d.%d %s)\n", farPath.c_str(),
                 lodFar, fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
