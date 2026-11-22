// G3 screenshot tool: renders the demo scene (heightmap terrain + PBR props + CSM
// shadows + IBL + sky) through fuhglgraphics::render::DeferredRenderer into an FBO
// via a headless surfaceless EGL context and writes a PNG. Used to produce the
// reference screenshots in docs/images/ (locally and by the CI docs job).
//
// Usage: fuhglgraphics_screenshot --out file.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--msaa 0] [--no-taa] [--no-tonemap]
//          [--no-upscale] [--no-shadows] [--no-ibl]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "png_writer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out = "screenshot.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f;
    bool taa = true, tonemap = true, upscale = true, shadows = true, ibl = true;
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
        else if (std::strcmp(argv[i], "--no-ibl") == 0) a.ibl = false;
    }
    return a;
}

// Demo scene: heightmap terrain, a gold sphere centerpiece, a ring of pillars, a
// torus, sunset sky + IBL environment, sun with CSM shadows and a few point lights.
struct DemoScene {
    fuhgl::render::HeightmapTerrain terrain;
    fuhgl::render::Mesh sphere, torus;
    std::vector<fuhgl::render::Mesh> pillars;
    fuhgl::Skybox sky;
    fuhgl::render::Environment env;
    fuhgl::render::Material groundMat, goldMat, copperMat;
    std::vector<fuhgl::render::Material> pillarMats;
    std::vector<std::uint32_t> materialIds;

    bool build() {
        const fuhgl::render::procedural::Image heightImg =
            fuhgl::render::procedural::heightmap(160, 160, 7u, 5, 0.055f);
        if (!terrain.create(heightImg, 100.f, 100.f, 6.5f)) return false;

        auto sphereSolid = fuhgl::render::makeSphere(2.2f);
        sphere = sphereSolid->buildMesh(48, {0.85f, 0.7f, 0.3f}, fuhgl::render::SolidUv::Spherical);
        auto torusSolid = fuhgl::render::makeTorus(2.6f, 0.55f);
        torus = torusSolid->buildMesh(40, {0.7f, 0.45f, 0.3f}, fuhgl::render::SolidUv::Cylindrical);

        pillars.resize(6);
        const fuhgl::math::Vec3 pillarColors[6] = {
            {0.85f, 0.25f, 0.18f}, {0.20f, 0.45f, 0.90f}, {0.90f, 0.88f, 0.82f},
            {0.25f, 0.70f, 0.35f}, {0.75f, 0.55f, 0.20f}, {0.55f, 0.30f, 0.75f}};
        for (int i = 0; i < 6; ++i) {
            const float h = 2.f + static_cast<float>(i % 3) * 1.5f;
            fuhgl::render::shapes::box(pillars[i], {0.7f, h * 0.5f, 0.7f}, pillarColors[i]);
            pillarMats.push_back(fuhgl::render::materials::pbr(pillarColors[i],
                                                                (i % 2 == 0) ? 0.85f : 0.f,
                                                                (i % 2 == 0) ? 0.3f : 0.6f));
        }

        groundMat = fuhgl::render::materials::pbr({1.f, 1.f, 1.f}, 0.f, 0.95f);
        goldMat = fuhgl::render::materials::pbr({0.83f, 0.66f, 0.24f}, 1.0f, 0.22f);
        copperMat = fuhgl::render::materials::pbr({0.72f, 0.45f, 0.30f}, 1.0f, 0.35f);

        if (!sky.create()) return false;
        sky.makeGradient({0.10f, 0.22f, 0.50f}, {0.98f, 0.80f, 0.58f}, 128);
        if (!env.createFromCubemap(sky.texture(), 16, 32, 64)) {
            std::fprintf(stderr, "screenshot: IBL bake failed, continuing without env\n");
        }
        env.intensity = 0.9f;
        return true;
    }

    void fillScene(fuhgl::render::SceneView& scene, fuhgl::Camera& cam, int w, int h) {
        scene.camera = &cam;
        scene.width = w;
        scene.height = h;
        scene.aspect = static_cast<float>(w) / static_cast<float>(h);
        scene.items.push_back({&terrain.mesh(), &groundMat, fuhgl::math::Mat4{}});
        materialIds.push_back(1);
        const float cy = terrain.heightAt(0.f, 0.f);
        scene.items.push_back({&sphere, &goldMat, fuhgl::math::translate({0.f, cy + 2.3f, 0.f})});
        materialIds.push_back(2);
        scene.items.push_back(
            {&torus, &copperMat,
             fuhgl::math::translate({-9.f, terrain.heightAt(-9.f, 7.f) + 0.6f, 7.f}) *
                 fuhgl::math::rotateX(fuhgl::math::kPi * 0.5f)});
        materialIds.push_back(3);
        for (int i = 0; i < 6; ++i) {
            const float angle = static_cast<float>(i) / 6.f * fuhgl::math::kPi * 2.f;
            const float x = std::cos(angle) * 9.f, z = std::sin(angle) * 9.f;
            const float h = 2.f + static_cast<float>(i % 3) * 1.5f;
            scene.items.push_back(
                {&pillars[i], &pillarMats[i],
                 fuhgl::math::translate({x, terrain.heightAt(x, z) + h * 0.5f, z})});
            materialIds.push_back(static_cast<std::uint32_t>(10 + i));
        }
        // low sun elevation -> long, clearly visible shadows
        scene.lights.push_back(fuhgl::render::Light::directional({-0.85f, -0.55f, -0.45f},
                                                                  {1.0f, 0.82f, 0.58f}, 3.4f));
        scene.lights.push_back(fuhgl::render::Light::point({6.f, 6.f, 6.f}, {1.f, 0.55f, 0.3f}, 40.f, 40.f));
        scene.lights.push_back(fuhgl::render::Light::point({-8.f, 5.f, -6.f}, {0.35f, 0.55f, 1.f}, 35.f, 40.f));
        scene.lights.push_back(fuhgl::render::Light::point({0.f, 8.f, -10.f}, {1.f, 0.9f, 0.7f}, 30.f, 45.f));
        scene.env = &env;
        scene.fogColor = {0.98f, 0.80f, 0.58f};
        scene.fogDensity = 0.0018f;
    }
};

} // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_screenshot", 64, 64)) {
        std::fprintf(stderr, "screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "screenshot: context below GL 3.3\n");
        return 2;
    }

    DemoScene demo;
    if (!demo.build()) {
        std::fprintf(stderr, "screenshot: demo scene build failed\n");
        return 1;
    }

    fuhglgraphics::render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    renderer.exposure = 1.15f;
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::Taa, args.taa);
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::Tonemap, args.tonemap);
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::Upscale, args.upscale);
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::Ibl, args.ibl);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, 4.f, 0.f}, 55.f);
        renderer.setShadowZRange(0.5f, 160.f);
    }
    renderer.skyTop = {0.10f, 0.22f, 0.50f};
    renderer.skyBottom = {0.98f, 0.80f, 0.58f};

    // camera: sunset view onto the gold sphere and the pillar ring
    fuhgl::FirstPersonCamera cam;
    cam.position = {-16.f, 9.f, 20.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{0.f, 3.5f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    demo.fillScene(scene, cam, args.width, args.height);

    for (int frame = 0; frame < args.frames; ++frame) renderer.render(scene, &demo.materialIds);
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "screenshot: wrote %s (%dx%d, %d frames, scale %.2f, GL %d.%d %s)\n", args.out.c_str(),
                 args.width, args.height, args.frames, renderer.renderScale(), fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
