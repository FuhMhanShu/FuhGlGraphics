// G14 GPU-skinning screenshot tool: the same 2-joint skeletal arm as the G6 animation
// demo, but skinned ON THE GPU — an anim::GpuSkinner (VAO + JOINTS/WEIGHTS attributes +
// matrix-palette SSBO) drawn by the G3 G-buffer pass's skinning vertex shader
// (Effect::GpuSkinning, GL >= 4.3). No per-frame CPU skinMeshCpu/upload. With --cpu
// (or Effect::GpuSkinning off) the plain bind-pose mesh is drawn instead (G6 fallback).
// Renders headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_gpu_skinning_screenshot --out shot.png [--bend 1.05] [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0] [--cpu]
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
    std::string out = "gpu_skinning.png";
    float bend = 1.05f; // joint1 rotation around Z (rad)
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f;
    bool cpu = false; // draw the bind-pose mesh (GpuSkinning off) instead
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--bend") == 0 && i + 1 < argc) a.bend = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--cpu") == 0) a.cpu = true;
    }
    return a;
}

// skinned box strip along +Y (5 rings x 4 verts); y < 1 -> joint 0, else joint 1
fuhgl::render::MeshData makeArmMesh(fuhglgraphics::anim::SkinWeights& skin) {
    fuhgl::render::MeshData m;
    const int rings = 5;
    const float half = 0.16f;
    for (int r = 0; r < rings; ++r) {
        const float y = 2.f * r / (rings - 1);
        const std::uint8_t joint = y < 1.f ? 0 : 1;
        for (int k = 0; k < 4; ++k) {
            const float x = (k == 0 || k == 3) ? -half : half;
            const float z = (k < 2) ? -half : half;
            m.vertices.insert(m.vertices.end(), {x, y, z, 0, 1, 0, 0.75f, 0.55f, 0.30f, 0, 0});
            skin.joints.insert(skin.joints.end(), {joint, 0, 0, 0});
            skin.weights.insert(skin.weights.end(), {1.f, 0.f, 0.f, 0.f});
        }
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int k = 0; k < 4; ++k) {
            const unsigned a = static_cast<unsigned>(r * 4 + k), b = static_cast<unsigned>(r * 4 + (k + 1) % 4),
                           c = static_cast<unsigned>((r + 1) * 4 + k), d = static_cast<unsigned>((r + 1) * 4 + (k + 1) % 4);
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
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
    if (!window.create("fuhglgraphics_gpu_skinning_screenshot", 64, 64)) {
        std::fprintf(stderr, "gpu_skinning_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gpu_skinning_screenshot: context below GL 3.3\n");
        return 2;
    }
    const bool gpuSkin = anim::GpuSkinner::supported() && !args.cpu;
    if (!args.cpu && !anim::GpuSkinner::supported())
        std::fprintf(stderr, "gpu_skinning_screenshot: GL < 4.3 — falling back to the bind-pose mesh\n");

    // --- stage: ground + sun + camera ---
    scene::Scene scene;
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 24.f, {0.32f, 0.33f, 0.30f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.32f, 0.33f, 0.30f};
    ge.roughness = 1.f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{std::cos(fuhgl::math::radians(150.f)) * std::cos(fuhgl::math::radians(30.f)),
                                   std::sin(fuhgl::math::radians(30.f)),
                                   std::sin(fuhgl::math::radians(150.f)) * std::cos(fuhgl::math::radians(30.f))};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.85f, 0.65f}, 3.0f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {4.6f, 3.4f, 6.4f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 1.4f, 0.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::GpuSkinning);
    scene.root().effects.params.shadowCenter = {0.f, 1.5f, 0.f};
    scene.root().effects.params.shadowRadius = 8.f;
    scene.root().effects.params.shadowZFar = 40.f;

    // --- skinned arm: joint nodes + palette ---
    scene::Node& joint0 = scene.root().createChild("joint0");
    joint0.translation = {0.f, 0.4f, 0.f};
    scene::Node& joint1 = joint0.createChild("joint1");
    joint1.translation = {0.f, 1.0f, 0.f};

    anim::SkinWeights armSkin;
    const fuhgl::render::MeshData armBase = makeArmMesh(armSkin);
    fuhgl::render::Mesh armMesh;
    if (!armMesh.create()) return 1;
    armMesh.setMeshData(armBase); // bind pose — the fallback when GpuSkinning is off
    anim::GpuSkinner armSkinner;
    if (gpuSkin && !armSkinner.create(armBase, armSkin)) {
        std::fprintf(stderr, "gpu_skinning_screenshot: GpuSkinner create failed\n");
        return 1;
    }
    scene::Node& armNode = scene.root().createChild("arm");
    scene::MeshEntry& ae = armNode.attach<scene::MeshComponent>().addMesh(&armMesh);
    ae.albedo = {0.75f, 0.55f, 0.30f};
    ae.metallic = 0.6f;
    ae.roughness = 0.35f;
    ae.materialId = 2;
    ae.meshData = &armBase;
    if (gpuSkin) ae.gpuSkin = &armSkinner;

    anim::Skeleton skeleton;
    skeleton.jointNodes = {0, 1};
    skeleton.inverseBindMatrices = {fuhgl::math::Mat4{}, fuhgl::math::Mat4{}};

    // --- sky + renderer ---
    const fuhgl::math::Vec3 skyTop{0.15f, 0.30f, 0.62f}, skyHorizon{0.90f, 0.82f, 0.66f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "gpu_skinning_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    renderer.setShadowSceneBounds({0.f, 1.5f, 0.f}, 8.f);
    renderer.setShadowZRange(0.5f, 40.f);
    renderer.setShadowFilter(fuhgl::render::ShadowFilter::Pcss);
    renderer.shadowLightSize = 0.4f;
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    const std::vector<const scene::Node*> jointPtrs = {&joint0, &joint1};
    const float dt = 1.f / 60.f;
    for (int i = 0; i < args.frames; ++i) {
        // bend the arm: joint1 rotates around Z over the first frames, then holds
        const float t = std::min(1.f, static_cast<float>(i) * dt * 2.f);
        joint1.rotation = fuhgl::math::fromAxisAngle(-args.bend * t, {0.f, 0.f, 1.f});
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.0015f;
        skeleton.updatePalette(jointPtrs);
        if (gpuSkin) armSkinner.uploadPalette(skeleton.palette); // GPU path: palette -> SSBO
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "gpu_skinning_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "gpu_skinning_screenshot: wrote %s (%dx%d, %d frames, gpuSkin=%d, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, (int)gpuSkin,
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
