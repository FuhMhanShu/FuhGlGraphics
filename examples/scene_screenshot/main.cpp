// G9 scene-screenshot tool: builds the demo scene as a fuhglgraphics::scene::Scene
// graph — camera node, textured ground/plaza (albedo + normal map per mesh entry),
// a hero node with TWO meshes, an instanced pillar grid (instancing property),
// emissive lamp nodes with light components, a sun node and per-node effects —
// fills a FrameData, applies the merged effects to the G3 DeferredRenderer and
// writes a PNG headless (surfaceless EGL). Produces the G9 tutorial images in
// docs/images/.
//
// Usage: fuhglgraphics_scene_screenshot --out file.png [--width 1280] [--height 720]
//          [--frames 24] [--scale 1.0] [--msaa 0] [--no-taa] [--no-tonemap]
//          [--no-upscale] [--no-shadows] [--no-csm] [--no-ibl]
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
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out = "scene.png";
    int width = 1280, height = 720, frames = 24, msaa = 0;
    float scale = 1.f;
    bool taa = true, tonemap = true, upscale = true, shadows = true, csm = true, ibl = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) a.msaa = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-taa") == 0) a.taa = false;
        else if (std::strcmp(argv[i], "--no-tonemap") == 0) a.tonemap = false;
        else if (std::strcmp(argv[i], "--no-upscale") == 0) a.upscale = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-csm") == 0) a.csm = false;
        else if (std::strcmp(argv[i], "--no-ibl") == 0) a.ibl = false;
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
    if (!window.create("fuhglgraphics_scene_screenshot", 64, 64)) {
        std::fprintf(stderr, "scene_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "scene_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- procedural textures for the plaza (albedo + normal map per mesh entry) ---
    const fuhgl::render::procedural::Image heightImg =
        fuhgl::render::procedural::heightmap(256, 256, 7u, 5, 0.06f);
    fuhgl::Texture albedoTex = fuhgl::render::procedural::makeTexture(
        fuhgl::render::procedural::voronoi(256, 256, 24, 3u, {0.16f, 0.17f, 0.20f}, {0.72f, 0.70f, 0.64f}));
    fuhgl::Texture normalTex = fuhgl::render::procedural::makeNormalMap(heightImg, 2.5f);

    // --- meshes ---
    fuhgl::render::Mesh ground, plaza, sphere, torus, pillar, lamp;
    fuhgl::render::shapes::quad(ground, 40.f, {0.32f, 0.34f, 0.36f});
    fuhgl::render::shapes::quad(plaza, 7.f, {1.f, 1.f, 1.f});
    sphere = fuhgl::render::makeSphere(1.9f)->buildMesh(48, {0.85f, 0.7f, 0.3f},
                                                         fuhgl::render::SolidUv::Spherical);
    torus = fuhgl::render::makeTorus(2.4f, 0.5f)->buildMesh(40, {0.7f, 0.45f, 0.3f},
                                                          fuhgl::render::SolidUv::Cylindrical);
    fuhgl::render::shapes::box(pillar, {0.35f, 1.2f, 0.35f}, {0.8f, 0.8f, 0.85f});
    fuhgl::render::shapes::box(lamp, {0.3f, 0.3f, 0.3f}, {1.f, 0.8f, 0.5f});

    // --- sky + IBL environment ---
    fuhgl::Skybox sky;
    if (!sky.create()) return 1;
    sky.makeGradient({0.10f, 0.22f, 0.50f}, {0.98f, 0.80f, 0.58f}, 128);
    fuhgl::render::Environment env;
    if (!env.createFromCubemap(sky.texture(), 16, 32, 64))
        std::fprintf(stderr, "scene_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.9f;

    // --- camera ---
    fuhgl::FirstPersonCamera cam;
    cam.position = {-16.f, 9.f, 20.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{0.f, 3.0f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    // --- G9 scene graph ---
    scene::Scene scene;
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.enable(args.csm ? render::Effect::CsmShadows : render::Effect::Shadows);
    scene.root().effects.params.exposure = 1.15f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.upscaleSharpness = 0.6f;
    scene.root().effects.params.shadowStrength = 1.f;
    scene.root().effects.params.shadowCenter = {0.f, 2.f, 0.f};
    scene.root().effects.params.shadowRadius = 45.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 160.f;

    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.32f, 0.34f, 0.36f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f); // quad XY -> XZ

    scene::Node& plazaNode = scene.root().createChild("plaza"); // textured: albedo + normal map
    plazaNode.translation = {0.f, 0.02f, 0.f};
    scene::MeshEntry& pe = plazaNode.attach<scene::MeshComponent>().addMesh(&plaza);
    pe.albedo = {1.f, 1.f, 1.f};
    pe.albedoTexture = &albedoTex;
    pe.normalMap = &normalTex;
    pe.roughness = 0.6f;
    pe.castShadow = false;
    pe.materialId = 4;
    pe.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    scene::Node& hero = scene.root().createChild("hero"); // TWO meshes on one node
    hero.translation = {0.f, 1.9f, 0.f};
    scene::MeshComponent& heroMc = hero.attach<scene::MeshComponent>();
    scene::MeshEntry& se = heroMc.addMesh(&sphere);
    se.albedo = {0.83f, 0.66f, 0.24f};
    se.metallic = 1.0f;
    se.roughness = 0.22f;
    se.castShadow = true;
    se.materialId = 2;
    scene::MeshEntry& te = heroMc.addMesh(&torus);
    te.localOffset = fuhgl::math::translate({-6.5f, -1.3f, 4.5f}) * fuhgl::math::rotateX(fuhgl::math::kPi * 0.5f);
    te.albedo = {0.72f, 0.45f, 0.30f};
    te.metallic = 1.0f;
    te.roughness = 0.35f;
    te.castShadow = true;
    te.materialId = 3;

    scene::Node& pillars = scene.root().createChild("pillars"); // instancing property
    pillars.translation = {0.f, 1.2f, 0.f};
    pillars.instancing.enabled = true;
    pillars.instancing.count = 36; // 6x6 grid
    pillars.instancing.layout = scene::InstanceLayout::Grid;
    pillars.instancing.extent = {26.f, 0.f, 26.f};
    scene::MeshEntry& pie = pillars.attach<scene::MeshComponent>().addMesh(&pillar);
    pie.albedo = {0.80f, 0.80f, 0.85f};
    pie.metallic = 0.3f;
    pie.roughness = 0.5f;
    pie.castShadow = true;
    pie.materialId = 10;

    const fuhgl::math::Vec3 lampPos[3] = {{5.f, 2.5f, 5.f}, {-6.f, 2.5f, -4.f}, {0.5f, 3.f, -8.f}};
    const fuhgl::math::Vec3 lampColor[3] = {{1.f, 0.55f, 0.30f}, {0.35f, 0.55f, 1.f}, {1.f, 0.9f, 0.7f}};
    for (int i = 0; i < 3; ++i) {
        scene::Node& lampNode = scene.root().createChild("lamp" + std::to_string(i));
        lampNode.translation = lampPos[i];
        scene::MeshEntry& le = lampNode.attach<scene::MeshComponent>().addMesh(&lamp);
        le.emissive = lampColor[i] * 4.f;
        le.albedo = lampColor[i];
        le.roughness = 0.4f;
        le.castShadow = false;
        le.materialId = static_cast<std::uint32_t>(20 + i);
        scene::LightComponent& lc = lampNode.attach<scene::LightComponent>();
        lc.light = fuhgl::render::Light::point({0.f, 0.f, 0.f}, lampColor[i], 30.f, 30.f); // moved by the node
    }

    scene::Node& sunNode = scene.root().createChild("sun");
    sunNode.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.85f, -0.55f, -0.45f}, {1.0f, 0.82f, 0.58f}, 3.4f);

    // --- renderer ---
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "scene_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        if (args.csm) renderer.enableCsmShadows(1024, 4);
        else renderer.enableShadows(1024);
    }
    renderer.skyTop = {0.10f, 0.22f, 0.50f};
    renderer.skyBottom = {0.98f, 0.80f, 0.58f};

    // --- one frame data, CLI effect switches edit the merged effect list ---
    scene::FrameData frame;
    scene.fillFrame(frame, nullptr, args.width, args.height);
    if (!args.taa) frame.effects.disable(render::Effect::Taa);
    if (!args.tonemap) frame.effects.disable(render::Effect::Tonemap);
    if (!args.upscale) frame.effects.disable(render::Effect::Upscale);
    if (!args.ibl) frame.effects.disable(render::Effect::Ibl);
    if (!args.shadows) {
        frame.effects.disable(render::Effect::Shadows);
        frame.effects.disable(render::Effect::CsmShadows);
    }
    frame.effects.applyTo(renderer);
    frame.view.env = args.ibl ? &env : nullptr;
    frame.view.fogColor = {0.98f, 0.80f, 0.58f};
    frame.view.fogDensity = 0.0018f;

    for (int i = 0; i < args.frames; ++i)
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "scene_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "scene_screenshot: wrote %s (%dx%d, %d frames, scale %.2f, items %zu, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, renderer.renderScale(),
                 frame.view.items.size(), fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
