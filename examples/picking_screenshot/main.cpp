// G18 picking screenshot tool: a plaza with several boxes — picks a pixel (--px/--py,
// default: the center) via scene::pick() (ray + AABB culling + triangle tests against the
// entries' CPU meshData), prints the hit (node, distance, point, triangle) and renders the
// scene with a bright yellow marker cube at the hit point. Headless (surfaceless EGL) to PNG
// for docs/images/.
//
// Usage: fuhglgraphics_picking_screenshot --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--px 640] [--py 380] [--no-marker]
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
    std::string out = "picking.png";
    int width = 1280, height = 720, frames = 16;
    float px = -1.f, py = -1.f; // < 0: use the image center
    bool marker = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--px") == 0 && i + 1 < argc) a.px = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--py") == 0 && i + 1 < argc) a.py = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-marker") == 0) a.marker = false;
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
    if (!window.create("fuhglgraphics_picking_screenshot", 64, 64)) {
        std::fprintf(stderr, "picking_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "picking_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    fuhgl::render::Mesh ground, box, marker;
    fuhgl::render::shapes::quad(ground, 30.f, {0.35f, 0.34f, 0.31f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    fuhgl::render::shapes::box(marker, {0.14f, 0.14f, 0.14f}, {1.f, 0.9f, 0.1f});
    const fuhgl::render::MeshData boxData = fuhgl::render::shapes::boxData({0.8f, 0.8f, 0.8f}, {1, 1, 1});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.34f, 0.31f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{0.5f, 0.7f, 0.4f};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.92f, 0.78f}, 3.0f);

    const fuhgl::math::Vec3 crateColors[4] = {{0.8f, 0.45f, 0.2f}, {0.25f, 0.55f, 0.8f},
                                               {0.3f, 0.75f, 0.35f}, {0.75f, 0.3f, 0.6f}};
    const char* names[4] = {"crateA", "crateB", "crateC", "crateD"};
    const fuhgl::math::Vec3 pos[4] = {{-2.2f, 0.8f, -1.0f}, {0.4f, 0.8f, -2.8f},
                                      {2.4f, 0.8f, -0.6f}, {-0.6f, 0.8f, 0.8f}};
    for (int i = 0; i < 4; ++i) {
        scene::Node& n = scene.root().createChild(names[i]);
        n.translation = pos[i];
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = crateColors[i];
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
        e.meshData = &boxData; // CPU triangles for G18 picking
    }

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 3.2f, 7.5f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.6f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);

    // --- pick ---
    const float px = args.px >= 0.f ? args.px : args.width * 0.5f - 0.5f;
    const float py = args.py >= 0.f ? args.py : args.height * 0.52f;
    scene::PickHit hit;
    const bool got = scene::pick(scene, &cam, args.width, args.height, px, py, hit);
    std::fprintf(stderr, "picking_screenshot: pick(%.0f,%.0f) -> %s", px, py,
                 got ? "HIT" : "miss");
    if (got)
        std::fprintf(stderr, " node=%s entry=%d dist=%.3f point=(%.2f, %.2f, %.2f) tri=%u",
                     hit.node->name().c_str(), hit.meshEntry, hit.distance, hit.point.x, hit.point.y,
                     hit.point.z, hit.triangle);
    std::fprintf(stderr, "\n");

    scene::Node* markerNode = nullptr;
    if (got && args.marker) {
        markerNode = &scene.root().createChild("pickMarker");
        markerNode->translation = hit.point;
        scene::MeshEntry& me = markerNode->attach<scene::MeshComponent>().addMesh(&marker);
        me.emissive = {5.f, 4.f, 0.2f};
        me.castShadow = false;
        me.materialId = 99;
    }

    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.64f}, skyHorizon{0.88f, 0.84f, 0.74f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "picking_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.enableCsmShadows(1024, 4);
    renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 12.f);
    renderer.setShadowZRange(0.5f, 40.f);
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    for (int i = 0; i < args.frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, args.width, args.height);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.0015f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "picking_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "picking_screenshot: wrote %s (%dx%d, marker=%d, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, (int)(markerNode != nullptr),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
