// Headless G6 smoke test (label "gl_context"): loads the generated glTF fixture (which now
// carries an animation clip "spin" and a skin), instantiates it into a G9 scene, plays the
// clip, and renders two frames at different times through the G3 DeferredRenderer — the
// animated node's rotation must change the image. Also CPU-skins the fixture's skinned
// primitive with a moved joint and checks the deformed vertex moved.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include "gltf_fixture.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

constexpr int kSkip = 77;
constexpr int W = 256, H = 256;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_anim", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_anim: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_anim: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- parse + instantiate the fixture (animation "spin" + skin on node "quad") ---
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "fgg-gl-smoke-anim";
    fuhglgraphicstest::writeGltfFixture(dir);
    io::GltfAsset asset;
    std::string err;
    if (!io::loadGltf((dir / "quad.glb").string(), asset, &err)) {
        std::fprintf(stderr, "gl_smoke_anim: loadGltf failed: %s\n", err.c_str());
        return 1;
    }
    CHECK(asset.animations.size() == 1 && asset.skins.size() == 1);

    scene::Scene scene;
    io::GltfGpuResources gpu;
    if (!io::instantiateGltf(asset, scene, gpu, nullptr, &err)) {
        std::fprintf(stderr, "gl_smoke_anim: instantiateGltf failed: %s\n", err.c_str());
        return 1;
    }
    CHECK(gpu.player.clip("spin") != nullptr);
    CHECK(gpu.skeletons.size() == 1 && gpu.skinnedMeshes.size() == 1);

    // --- CPU skinning: move joint 1 by (1,0,0); the skinned vertex weighted to joint 1 moves ---
    if (!gpu.skinnedMeshes.empty()) {
        const auto& sm = gpu.skinnedMeshes[0];
        std::vector<const scene::Node*> jointPtrs;
        for (int j : gpu.skeletons[0].jointNodes)
            jointPtrs.push_back(j >= 0 && j < static_cast<int>(gpu.nodePtrs.size()) ? gpu.nodePtrs[static_cast<std::size_t>(j)]
                                                                                     : nullptr);
        // palette with joint 1 translated +x
        std::vector<fuhgl::math::Mat4> palette = gpu.skeletons[0].inverseBindMatrices;
        for (std::size_t i = 0; i < palette.size(); ++i)
            palette[i] = fuhgl::math::translate({i == 1 ? 1.f : 0.f, 0.f, 0.f}) * palette[i];
        fuhgl::render::MeshData skinned;
        anim::skinMeshCpu(sm.base, sm.weights, palette, skinned);
        // vertex 1 (joint 1 in the fixture) must have moved +x by ~1
        CHECK(skinned.vertices.size() >= 11 * 2);
        CHECK_NEAR(skinned.vertices[11 + 0], sm.base.vertices[11 + 0] + 2.f, 1e-4); // IBM +1 and palette +1
    }

    // --- play the clip and render two frames at different times ---
    // ground for context + camera aimed at the animated quad (world center ~(2,3,4))
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 12.f, {0.35f, 0.4f, 0.38f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.4f, 0.38f};
    ge.castShadow = false;
    ge.materialId = 99;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {2.f, 5.5f, 9.5f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{2.f, 3.f, 4.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.3f, -1.f, -0.4f}, {1.f, 0.9f, 0.8f}, 3.f);
    scene.root().effects.enable(render::Effect::Tonemap);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_anim: renderer create failed\n");
        return 1;
    }
    renderer.setEffectEnabled(render::Effect::Taa, false);

    auto renderAt = [&](float t) {
        gpu.player.play("spin", anim::LoopMode::Once);
        gpu.player.update(t); // jump to time t
        gpu.player.applyTo(scene);
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, W, H);
        frame.effects.applyTo(renderer);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
        fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    const scene::Node* quad = gpu.nodePtrs.size() > 1 ? gpu.nodePtrs[1] : nullptr;
    const std::vector<unsigned char> f0 = renderAt(0.f);
    const std::vector<unsigned char> f1 = renderAt(1.f);
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_anim: FAIL (GL error)\n");
        return 1;
    }
    long diff = 0;
    for (std::size_t i = 0; i < f0.size(); i += 4)
        diff += std::abs(static_cast<int>(f0[i]) - static_cast<int>(f1[i])) +
                std::abs(static_cast<int>(f0[i + 1]) - static_cast<int>(f1[i + 1])) +
                std::abs(static_cast<int>(f0[i + 2]) - static_cast<int>(f1[i + 2]));
    std::fprintf(stderr, "gl_smoke_anim: frame diff t=0 vs t=1: %ld\n", diff);
    CHECK(diff > 500); // the spinning quad changes the image

    // the animated node's rotation must actually be the clip pose at t=1 (90 deg around Y)
    CHECK(quad != nullptr);
    if (quad) CHECK_NEAR(quad->rotation.x, 0.7071068f, 1e-3); // clip pose at t=1: 90 deg around X

    std::fprintf(stderr, "gl_smoke_anim: PASS (GL %d.%d)\n", fuhgl::gl::version().major, fuhgl::gl::version().minor);
    return fuhglgraphicstest::failures() ? 1 : 0;
}
