// Headless G21 smoke test (label "gl_context"): renders a small scene (ground + crates + sun)
// twice — Effect::VolumetricFog on (dense) and off — and verifies the frames differ
// measurably (fog attenuates geometry and tints the sky), that the fog-on frame is not black,
// and that no GL error is left over. Skips cleanly with exit code 77 without GL.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    if (!window.create("gl_smoke_volumetric", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_volumetric: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_volumetric: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

std::vector<unsigned char> renderFrame(fuhglgraphics::scene::Scene& scene,
                                       fuhgl::FirstPersonCamera& cam,
                                       fuhglgraphics::render::DeferredRenderer& renderer, bool fogOn) {
    using namespace fuhglgraphics;
    for (int i = 0; i < 4; ++i) { // a few frames so TAA settles
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, W, H);
        frame.effects.enable(render::Effect::Tonemap);
        frame.effects.enable(render::Effect::VolumetricFog); // list; renderer flag decides
        frame.effects.applyTo(renderer);
        renderer.setEffectEnabled(render::Effect::VolumetricFog, fogOn);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    renderer.output().bind();
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
    fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    fuhgl::gl::flush();
    return px;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    scene::Scene scene;
    fuhgl::render::Mesh ground, box;
    fuhgl::render::shapes::quad(ground, 60.f, {0.35f, 0.34f, 0.31f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.34f, 0.31f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("crate" + std::to_string(i));
        n.translation = {-2.f + 2.f * i, 0.8f, -3.f - 2.f * i};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = {0.8f, 0.4f + 0.2f * i, 0.2f};
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.5f, -0.7f, -0.4f}, {1.f, 0.92f, 0.78f}, 3.f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.5f, 8.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.5f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.skyTop = {0.16f, 0.32f, 0.64f};
    renderer.skyBottom = {0.88f, 0.84f, 0.74f};
    renderer.volumetricDensity = 0.06f;   // dense fog for a clear signal
    renderer.volumetricCloudiness = 0.5f;
    renderer.volumetricSteps = 24;
    renderer.volumetricMaxDist = 80.f;

    const auto off = renderFrame(scene, cam, renderer, false);
    const auto on = renderFrame(scene, cam, renderer, true);

    long sumOff = 0, sumOn = 0, diff = 0;
    for (std::size_t i = 0; i < off.size(); i += 4) {
        sumOff += off[i] + off[i + 1] + off[i + 2];
        sumOn += on[i] + on[i + 1] + on[i + 2];
        diff += std::labs((long)on[i] - (long)off[i]) + std::labs((long)on[i + 1] - (long)off[i + 1]) +
                std::labs((long)on[i + 2] - (long)off[i + 2]);
    }
    std::fprintf(stderr, "gl_smoke_volumetric: sumOff=%ld sumOn=%ld |diff|=%ld\n", sumOff, sumOn, diff);
    if (sumOn < 1000) {
        std::fprintf(stderr, "gl_smoke_volumetric: fog-on frame too dark\n");
        return 1;
    }
    if (diff < 1000) {
        std::fprintf(stderr, "gl_smoke_volumetric: fog on/off frames nearly identical (|diff|=%ld)\n", diff);
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_volumetric: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_volumetric: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
