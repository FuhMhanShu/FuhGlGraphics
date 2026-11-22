// G6 animation demo: a skeletal "robot arm" (2 joints, CPU skinning with the joint matrix
// palette via anim::skinMeshCpu into a dynamic mesh) bending via an anim::AnimationPlayer
// clip, plus a waving flag node (TRS channel) — rendered headless through the G3
// DeferredRenderer (PCSS shadows from the F45 shadow kit, sky cubemap) to PNG for
// docs/images/.
//
// Usage: fuhglgraphics_animation_screenshot --out shot.png [--time 0.7] [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0] [--msaa 0] [--no-shadows] [--no-rays]
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
    std::string out = "animation.png";
    float time = 0.7f; // pose time in seconds (clip loops pingpong over 1.2 s)
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f;
    bool shadows = true, rays = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--time") == 0 && i + 1 < argc) a.time = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) a.msaa = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-rays") == 0) a.rays = false;
    }
    return a;
}

// A skinned box strip along +Y (4 segments, 8 verts per ring), weights: y < 1 -> joint 0,
// y >= 1 -> joint 1. Bind pose == rest pose, IBMs = identity.
fuhgl::render::MeshData makeArmMesh(fuhglgraphics::anim::SkinWeights& skin) {
    fuhgl::render::MeshData m;
    const int rings = 5;
    const float half = 0.16f;
    skin.joints.clear();
    skin.weights.clear();
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
            const int a = r * 4 + k, b = r * 4 + (k + 1) % 4, c = (r + 1) * 4 + k, d = (r + 1) * 4 + (k + 1) % 4;
            m.indices.insert(m.indices.end(), {static_cast<unsigned>(a), static_cast<unsigned>(c),
                                               static_cast<unsigned>(b), static_cast<unsigned>(b),
                                               static_cast<unsigned>(c), static_cast<unsigned>(d)});
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
    if (!window.create("fuhglgraphics_animation_screenshot", 64, 64)) {
        std::fprintf(stderr, "animation_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "animation_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- stage ---
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
    const fuhgl::math::Vec3 target{0.f, 1.4f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.shadowCenter = {0.f, 1.5f, 0.f};
    scene.root().effects.params.shadowRadius = 8.f;
    scene.root().effects.params.shadowZFar = 40.f;

    // --- skeletal arm: joint0 -> joint1, skinned strip mesh ---
    scene::Node& joint0 = scene.root().createChild("joint0");
    joint0.translation = {0.f, 0.4f, 0.f};
    scene::Node& joint1 = joint0.createChild("joint1");
    joint1.translation = {0.f, 1.0f, 0.f};

    anim::SkinWeights armSkin;
    fuhgl::render::MeshData armBase = makeArmMesh(armSkin);
    fuhgl::render::Mesh armMesh;
    if (!armMesh.create()) {
        std::fprintf(stderr, "animation_screenshot: arm mesh create failed\n");
        return 1;
    }
    armMesh.setMeshData(armBase);
    scene::Node& armNode = scene.root().createChild("armMesh");
    scene::MeshEntry& ae = armNode.attach<scene::MeshComponent>().addMesh(&armMesh);
    ae.albedo = {0.75f, 0.55f, 0.30f};
    ae.metallic = 0.6f;
    ae.roughness = 0.35f;
    ae.castShadow = true;
    ae.materialId = 2;

    anim::Skeleton skeleton;
    skeleton.jointNodes = {0, 1}; // indices into the joint nodes collected below
    skeleton.inverseBindMatrices = {fuhgl::math::Mat4{}, fuhgl::math::Mat4{}};

    // --- flag on a pole: TRS channel animation (wave around Y) ---
    fuhgl::render::Mesh pole;
    fuhgl::render::shapes::box(pole, {0.06f, 2.6f, 0.06f}, {0.25f, 0.22f, 0.20f});
    scene::Node& poleNode = scene.root().createChild("pole");
    poleNode.translation = {-2.2f, 1.3f, -0.5f};
    scene::MeshEntry& pe = poleNode.attach<scene::MeshComponent>().addMesh(&pole);
    pe.albedo = {0.25f, 0.22f, 0.20f};
    pe.castShadow = true;
    pe.materialId = 3;

    fuhgl::render::Mesh flag;
    fuhgl::render::shapes::box(flag, {0.9f, 0.55f, 0.03f}, {0.85f, 0.15f, 0.12f});
    scene::Node& flagNode = poleNode.createChild("flag");
    flagNode.translation = {0.48f, 1.05f, 0.f};
    scene::MeshEntry& fe = flagNode.attach<scene::MeshComponent>().addMesh(&flag);
    fe.albedo = {0.85f, 0.15f, 0.12f};
    fe.castShadow = true;
    fe.materialId = 4;

    // --- clips: arm bends (joint1 rotates around Z), flag waves (flag rotates around Y) ---
    anim::AnimationPlayer player;
    {
        anim::AnimationClip bend;
        bend.name = "bend";
        bend.duration = 1.2f;
        anim::TrsChannel ch;
        ch.nodeName = "joint1";
        ch.times = {0.f, 0.6f, 1.2f};
        ch.rotations = {fuhgl::math::Quat{0, 0, 0, 1},
                        fuhgl::math::fromAxisAngle(-1.05f, {0, 0, 1}),
                        fuhgl::math::fromAxisAngle(0.f, {0, 0, 1})};
        bend.channels.push_back(ch);
        player.addClip(bend);

        anim::AnimationClip wave;
        wave.name = "wave";
        wave.duration = 0.8f;
        anim::TrsChannel fc;
        fc.nodeName = "flag";
        fc.times = {0.f, 0.4f, 0.8f};
        fc.rotations = {fuhgl::math::Quat{0, 0, 0, 1},
                        fuhgl::math::fromAxisAngle(0.5f, {0, 1, 0}),
                        fuhgl::math::fromAxisAngle(0.f, {0, 1, 0})};
        wave.channels.push_back(fc);
        player.addClip(wave);
    }
    player.play("bend", anim::LoopMode::PingPong);
    player.play("wave", anim::LoopMode::Loop); // "wave" is now current; play both via two players
    anim::AnimationPlayer flagPlayer;
    flagPlayer.addClip(*player.clip("wave"));
    flagPlayer.play("wave", anim::LoopMode::Loop);
    player.play("bend", anim::LoopMode::PingPong);

    // jump both players to the requested pose time
    player.update(args.time);
    flagPlayer.update(args.time);

    // --- sky + renderer ---
    const fuhgl::math::Vec3 skyTop{0.15f, 0.30f, 0.62f}, skyHorizon{0.90f, 0.82f, 0.66f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "animation_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "animation_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, 1.5f, 0.f}, 8.f);
        renderer.setShadowZRange(0.5f, 40.f);
        renderer.setShadowFilter(fuhgl::render::ShadowFilter::Pcss);
        renderer.shadowLightSize = 0.4f;
    }
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.godRays.enabled = args.rays;
    renderer.godRays.strength = 0.25f;
    renderer.godRays.decay = 0.94f;
    renderer.godRays.samples = 48;

    // per frame: advance players, apply poses, update the palette, CPU-skin the arm, render
    const std::vector<const scene::Node*> jointPtrs = {&joint0, &joint1};
    fuhgl::render::MeshData skinned;
    const float dt = 1.f / 60.f;
    for (int i = 0; i < args.frames; ++i) {
        player.update(dt);
        flagPlayer.update(dt);
        player.applyTo(scene);
        flagPlayer.applyTo(scene);
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        if (!args.shadows) {
            frame.effects.disable(render::Effect::Shadows);
            frame.effects.disable(render::Effect::CsmShadows);
        }
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.0015f;
        skeleton.updatePalette(jointPtrs); // joint world matrices * IBM
        anim::skinMeshCpu(armBase, armSkin, skeleton.palette, skinned);
        armMesh.setMeshData(skinned);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "animation_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "animation_screenshot: wrote %s (%dx%d, %d frames, time %.2f, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, args.time, fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
