// G7 light-probe demo: a courtyard with colored walls (red / green / blue), crates and a
// low warm sun. An SH (L2) light-probe grid is baked on the CPU over the courtyard (one
// bounce: sky + sun with shadow rays) and sampled trilinearly in the G3 lighting pass
// (Effect::LightProbes) — visible as color bleeding from the walls into the shadowed
// floor. Headless to PNG for docs/images/.
//
// Usage: fuhglgraphics_probes_screenshot --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--msaa 0] [--no-probes] [--no-shadows] [--no-rays]
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
#include <deque>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out = "probes.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f;
    bool rays = true, shadows = true, probes = true;
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
        else if (std::strcmp(argv[i], "--no-rays") == 0) a.rays = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-probes") == 0) a.probes = false;
    }
    return a;
}

fuhgl::math::Vec3 mdVert(const fuhgl::render::MeshData& md, unsigned idx) {
    const std::size_t o = static_cast<std::size_t>(idx) * fuhgl::render::MeshData::kFloatsPerVertex;
    return {md.vertices[o], md.vertices[o + 1], md.vertices[o + 2]};
}

// Adds a box mesh to the scene AND the matching world-space triangles to the bake soup.
fuhgl::render::Mesh& addBox(fuhglgraphics::scene::Scene& scene, std::deque<fuhgl::render::Mesh>& meshes,
                           fuhglgraphics::probes::BakeInput& bake, const char* name,
                           const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& half,
                           const fuhgl::math::Vec3& albedo, float roughness = 0.9f) {
    meshes.emplace_back();
    fuhgl::render::Mesh& m = meshes.back();
    const fuhgl::render::MeshData md = fuhgl::render::shapes::boxData(half, albedo);
    m.create();
    m.setMeshData(md);
    auto& node = scene.root().createChild(name);
    node.translation = pos;
    auto& e = node.attach<fuhglgraphics::scene::MeshComponent>().addMesh(&m);
    e.albedo = albedo;
    e.roughness = roughness;
    e.metallic = 0.05f;
    e.castShadow = true;
    for (std::size_t i = 0; i + 2 < md.indices.size(); i += 3) {
        const fuhgl::math::Vec3 a = mdVert(md, md.indices[i]) + pos;
        const fuhgl::math::Vec3 b = mdVert(md, md.indices[i + 1]) + pos;
        const fuhgl::math::Vec3 c = mdVert(md, md.indices[i + 2]) + pos;
        bake.triangles.push_back(fuhglgraphics::probes::ProbeTriangle{a, b, c, albedo, {0.f, 0.f, 0.f}});
    }
    return m;
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
    if (!window.create("fuhglgraphics_probes_screenshot", 64, 64)) {
        std::fprintf(stderr, "probes_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "probes_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- stage: courtyard floor, three colored walls, crates ---
    scene::Scene scene;
    std::deque<fuhgl::render::Mesh> meshes;
    probes::BakeInput bake;
    addBox(scene, meshes, bake, "floor", {0.f, -0.15f, 0.f}, {12.f, 0.15f, 12.f}, {0.62f, 0.60f, 0.55f});
    addBox(scene, meshes, bake, "leftWall", {-12.4f, 2.f, 0.f}, {0.4f, 2.f, 12.f}, {0.75f, 0.16f, 0.12f});
    addBox(scene, meshes, bake, "rightWall", {12.4f, 2.f, 0.f}, {0.4f, 2.f, 12.f}, {0.16f, 0.55f, 0.22f});
    addBox(scene, meshes, bake, "backWall", {0.f, 2.f, -12.4f}, {12.f, 2.f, 0.4f}, {0.18f, 0.30f, 0.68f});
    addBox(scene, meshes, bake, "crate1", {-3.2f, 0.6f, -2.4f}, {0.6f, 0.6f, 0.6f}, {0.72f, 0.55f, 0.30f}, 0.6f);
    addBox(scene, meshes, bake, "crate2", {-2.2f, 0.4f, -1.6f}, {0.4f, 0.4f, 0.4f}, {0.50f, 0.42f, 0.38f}, 0.7f);
    addBox(scene, meshes, bake, "crate3", {2.8f, 0.5f, -4.6f}, {0.5f, 0.5f, 0.5f}, {0.65f, 0.60f, 0.50f}, 0.8f);
    addBox(scene, meshes, bake, "pillar", {-6.5f, 1.6f, 3.5f}, {0.35f, 1.6f, 0.35f}, {0.80f, 0.78f, 0.74f}, 0.5f);

    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.55f, -0.75f, 0.42f});
    const fuhgl::math::Vec3 sunColor{1.f, 0.82f, 0.60f};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(sunDir, sunColor, 3.0f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {8.6f, 4.4f, 9.8f};
    cam.fovY = fuhgl::math::radians(52.f);
    const fuhgl::math::Vec3 target{-4.5f, 1.1f, -4.5f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // --- bake the probe grid (CPU; sky gradient + sun disk, sun light with shadow rays) ---
    bake.lights.push_back(fuhgl::render::Light::directional(sunDir, sunColor, 3.0f));
    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.66f}, skyHorizon{0.92f, 0.84f, 0.66f};
    bake.skyRadiance = [&](const fuhgl::math::Vec3& dir) {
        fuhgl::math::Vec3 sky = fuhgl::math::lerp(skyHorizon, skyTop, fuhgl::math::clamp(dir.y, 0.f, 1.f));
        const float sunAmount = fuhgl::math::clamp((fuhgl::math::dot(dir, -sunDir) - 0.9993f) / 0.0007f, 0.f, 1.f);
        sky += sunColor * (sunAmount * 12.f);
        return sky;
    };
    bake.raySamples = 128;

    scene::Node& probeNode = scene.root().createChild("probes");
    scene::LightProbeComponent& pc = probeNode.attach<scene::LightProbeComponent>();
    if (!pc.grid.create(9, 4, 9, {-11.5f, 0.05f, -11.5f}, {11.5f, 3.8f, 11.5f})) {
        std::fprintf(stderr, "probes_screenshot: grid create failed\n");
        return 1;
    }
    if (!pc.grid.bake(bake)) {
        std::fprintf(stderr, "probes_screenshot: bake failed\n");
        return 1;
    }

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    if (args.probes) scene.root().effects.enable(render::Effect::LightProbes);
    scene.root().effects.params.shadowCenter = {0.f, 2.f, 0.f};
    scene.root().effects.params.shadowRadius = 16.f;
    scene.root().effects.params.shadowZFar = 60.f;
    scene.root().effects.params.lightProbeStrength = 1.f;

    // --- sky + renderer ---
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "probes_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "probes_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(2048, 4);
        renderer.setShadowSceneBounds({0.f, 2.f, 0.f}, 16.f);
        renderer.setShadowZRange(0.5f, 60.f);
        renderer.setShadowFilter(fuhgl::render::ShadowFilter::Pcss);
        renderer.shadowLightSize = 0.6f;
    }
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.godRays.enabled = args.rays;
    renderer.godRays.strength = 0.22f;
    renderer.godRays.decay = 0.94f;
    renderer.godRays.samples = 48;

    for (int i = 0; i < args.frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        if (!args.shadows) {
            frame.effects.disable(render::Effect::Shadows);
            frame.effects.disable(render::Effect::CsmShadows);
        }
        if (!args.probes) frame.effects.disable(render::Effect::LightProbes);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.0012f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "probes_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "probes_screenshot: wrote %s (%dx%d, %d frames, probes=%d, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, args.probes ? 1 : 0,
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
