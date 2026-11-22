// G10 demo: depth of field with a hexagonal bokeh kernel (Effect::Dof, post pass after
// the upscale) + the debug HUD (Effect::DebugHud, fps / frame ms / effect toggles on
// FuhGLCore's TextRenderer). Scene: a near pillar in the foreground, crates and a wall in
// the background — with the focus on the background the pillar is blurred.
// Headless to PNG for docs/images/.
//
// Usage: fuhglgraphics_dof_screenshot --out shot.png [--focus 8.0] [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0] [--msaa 0] [--no-dof] [--hud]
//          [--no-shadows] [--no-rays]
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
    std::string out = "dof.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f, focus = 9.f;
    bool rays = true, shadows = true, dof = true, hud = false;
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
        else if (std::strcmp(argv[i], "--focus") == 0 && i + 1 < argc) a.focus = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-rays") == 0) a.rays = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-dof") == 0) a.dof = false;
        else if (std::strcmp(argv[i], "--hud") == 0) a.hud = true;
    }
    return a;
}

fuhgl::render::Mesh& addBox(fuhglgraphics::scene::Scene& scene, std::vector<fuhgl::render::Mesh>& meshes,
                           const char* name, const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& half,
                           const fuhgl::math::Vec3& albedo) {
    meshes.emplace_back();
    fuhgl::render::Mesh& m = meshes.back();
    m.create();
    m.setMeshData(fuhgl::render::shapes::boxData(half, albedo));
    auto& node = scene.root().createChild(name);
    node.translation = pos;
    auto& e = node.attach<fuhglgraphics::scene::MeshComponent>().addMesh(&m);
    e.albedo = albedo;
    e.roughness = 0.8f;
    e.castShadow = true;
    e.materialId = 1;
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
    if (!window.create("fuhglgraphics_dof_screenshot", 64, 64)) {
        std::fprintf(stderr, "dof_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "dof_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- stage: ground, near foreground pillar, background crates + wall ---
    scene::Scene scene;
    std::vector<fuhgl::render::Mesh> meshes;
    meshes.reserve(6);
    addBox(scene, meshes, "ground", {0.f, -0.1f, 0.f}, {14.f, 0.1f, 14.f}, {0.55f, 0.54f, 0.50f});
    addBox(scene, meshes, "pillar", {0.4f, 1.3f, -2.2f}, {0.35f, 1.3f, 0.35f}, {0.78f, 0.30f, 0.22f}); // near
    addBox(scene, meshes, "crate1", {-3.0f, 0.7f, -7.5f}, {0.7f, 0.7f, 0.7f}, {0.62f, 0.47f, 0.28f});
    addBox(scene, meshes, "crate2", {2.6f, 0.5f, -8.5f}, {0.5f, 0.5f, 0.5f}, {0.50f, 0.42f, 0.38f});
    addBox(scene, meshes, "backWall", {0.f, 3.f, -13.5f}, {14.f, 3.f, 0.5f}, {0.64f, 0.62f, 0.58f});

    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.5f, -0.8f, 0.35f});
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(sunDir, {1.f, 0.85f, 0.62f}, 3.0f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {2.6f, 2.0f, 2.8f}; // near the pillar -> pillar is foreground
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 target{-1.0f, 1.2f, -6.0f}; // focus direction: background
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    if (args.dof) scene.root().effects.enable(render::Effect::Dof);
    if (args.hud) scene.root().effects.enable(render::Effect::DebugHud);
    scene.root().effects.params.shadowCenter = {0.f, 2.f, -4.f};
    scene.root().effects.params.shadowRadius = 18.f;
    scene.root().effects.params.shadowZFar = 60.f;
    scene.root().effects.params.dofFocusDist = args.focus; // far background in focus
    scene.root().effects.params.dofStrength = 14.f;
    scene.root().effects.params.dofMaxCoC = 26.f;

    // --- sky + renderer ---
    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.66f}, skyHorizon{0.92f, 0.84f, 0.66f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "dof_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "dof_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(2048, 4);
        renderer.setShadowSceneBounds({0.f, 2.f, -4.f}, 18.f);
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
        if (!args.dof) frame.effects.disable(render::Effect::Dof);
        if (!args.hud) frame.effects.disable(render::Effect::DebugHud);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.0012f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "dof_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "dof_screenshot: wrote %s (%dx%d, %d frames, dof=%d hud=%d, %.1f fps, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, args.dof ? 1 : 0, args.hud ? 1 : 0,
                 renderer.hud().fps(), fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
