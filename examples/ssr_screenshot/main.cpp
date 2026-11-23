// G16 SSR screenshot tool: a showroom with a polished mirror floor (metallic 1, roughness
// 0.05) reflecting a bright emissive pillar and colored crates — the G3 "ssr" post pass
// raymarches the reflection vector against the G-buffer depth (Effect::Ssr) and adds the hit
// HDR color with an edge/roughness/fresnel mask. --no-ssr disables it for comparison.
// Renders headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_ssr_screenshot --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--no-ssr] [--no-shadows]
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
    std::string out = "ssr.png";
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f;
    bool ssr = true, shadows = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-ssr") == 0) a.ssr = false;
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
    if (!window.create("fuhglgraphics_ssr_screenshot", 64, 64)) {
        std::fprintf(stderr, "ssr_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "ssr_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    fuhgl::render::Mesh floorMesh, box, pillar;
    fuhgl::render::shapes::quad(floorMesh, 20.f, {0.55f, 0.57f, 0.60f});
    fuhgl::render::shapes::box(box, {0.7f, 0.7f, 0.7f}, {0.2f, 0.5f, 0.85f});
    fuhgl::render::shapes::box(pillar, {0.35f, 2.2f, 0.35f}, {1.f, 0.2f, 0.1f});

    scene::Node& fn = scene.root().createChild("floor");
    scene::MeshEntry& fe = fn.attach<scene::MeshComponent>().addMesh(&floorMesh);
    fe.albedo = {0.55f, 0.57f, 0.60f};
    fe.metallic = 1.f;
    fe.roughness = 0.05f; // mirror-like: strong SSR
    fe.castShadow = false;
    fe.materialId = 1;
    fe.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{0.5f, 0.7f, 0.4f};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.9f, 0.75f}, 2.5f);

    auto addBox = [&](const char* name, float x, float z, float s, const fuhgl::math::Vec3& c) {
        scene::Node& n = scene.root().createChild(name);
        n.translation = {x, 0.7f * s, z};
        n.scale = {s, s, s};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = c;
        e.metallic = 0.2f;
        e.roughness = 0.35f;
        e.materialId = 2;
    };
    addBox("crateBlue", -2.2f, -1.0f, 1.f, {0.2f, 0.5f, 0.85f});
    addBox("crateGreen", 2.0f, -1.6f, 0.8f, {0.25f, 0.75f, 0.3f});

    scene::Node& pn = scene.root().createChild("pillar");
    pn.translation = {0.3f, 2.2f, -2.6f};
    scene::MeshEntry& pe = pn.attach<scene::MeshComponent>().addMesh(&pillar);
    pe.albedo = {1.f, 0.2f, 0.1f};
    pe.emissive = {5.f, 0.6f, 0.3f}; // HDR-bright: clearly visible in the floor reflection
    pe.roughness = 0.4f;
    pe.materialId = 3;

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 2.4f, 7.0f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.6f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    if (args.ssr) scene.root().effects.enable(render::Effect::Ssr);
    if (args.shadows) scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.shadowCenter = {0.f, 1.f, 0.f};
    scene.root().effects.params.shadowRadius = 12.f;
    scene.root().effects.params.shadowZFar = 40.f;
    scene.root().effects.params.ssrMaxSteps = 48;
    scene.root().effects.params.ssrStrength = 0.9f;

    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.64f}, skyHorizon{0.88f, 0.84f, 0.74f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "ssr_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 12.f);
        renderer.setShadowZRange(0.5f, 40.f);
    }
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    for (int i = 0; i < args.frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
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
        std::fprintf(stderr, "ssr_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "ssr_screenshot: wrote %s (%dx%d, %d frames, ssr=%d, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, (int)args.ssr,
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
