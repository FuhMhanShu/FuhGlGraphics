// G15 SSAO screenshot tool: a small courtyard (ground, crates, a wall) where screen-space
// ambient occlusion darkens the contact corners/edges. The G3 "ssao" post pass (hemisphere
// kernel on the G-buffer normal+depth, resource "ssao") feeds the lighting pass
// (Effect::Ssao, unit 6). --no-ssao disables the effect for the comparison shot.
// Renders headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_ssao_screenshot --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--no-ssao] [--no-shadows] [--radius 1.5]
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
    std::string out = "ssao.png";
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f, radius = 1.5f;
    bool ssao = true, shadows = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--radius") == 0 && i + 1 < argc) a.radius = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-ssao") == 0) a.ssao = false;
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
    if (!window.create("fuhglgraphics_ssao_screenshot", 64, 64)) {
        std::fprintf(stderr, "ssao_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "ssao_screenshot: context below GL 3.3\n");
        return 2;
    }

    scene::Scene scene;
    fuhgl::render::Mesh ground, box, wall;
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
    addBox("crate3", -1.1f, 1.8f, 0.0f, 1.f); // stacked on crate1/2 gap
    scene::Node& wn = scene.root().createChild("wall");
    wn.translation = {2.8f, 1.6f, -1.2f};
    scene::MeshEntry& we = wn.attach<scene::MeshComponent>().addMesh(&wall);
    we.albedo = {0.55f, 0.53f, 0.48f};
    we.roughness = 0.9f;
    we.materialId = 3;

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 2.6f, 7.5f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{-0.5f, 0.8f, -0.5f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    if (args.ssao) scene.root().effects.enable(render::Effect::Ssao);
    if (args.shadows) scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.shadowCenter = {0.f, 1.f, 0.f};
    scene.root().effects.params.shadowRadius = 10.f;
    scene.root().effects.params.shadowZFar = 40.f;

    const fuhgl::math::Vec3 skyTop{0.18f, 0.34f, 0.66f}, skyHorizon{0.88f, 0.84f, 0.74f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "ssao_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 10.f);
        renderer.setShadowZRange(0.5f, 40.f);
    }
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    const float dt = 1.f / 60.f;
    for (int i = 0; i < args.frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        frame.effects.params.ssaoRadius = args.radius;
        frame.effects.params.ssaoIntensity = 1.6f;
        frame.effects.params.ssaoSamples = 32;
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.002f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins);
        (void)dt;
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "ssao_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "ssao_screenshot: wrote %s (%dx%d, %d frames, ssao=%d, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, (int)args.ssao,
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
