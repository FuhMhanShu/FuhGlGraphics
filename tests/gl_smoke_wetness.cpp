// Headless G22 smoke test (label "gl_context"): renders a plaza (ground + crates + sun) with
// Effect::Ssr on, once dry and once wet (Effect::Wetness, amount 1, puddles) — the wet frame
// must differ measurably (darker albedo + glossy floor reflecting more via the SSR boost),
// and no GL error may be left over. Skips cleanly with exit code 77 without GL.

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
    if (!window.create("gl_smoke_wetness", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_wetness: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_wetness: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

std::vector<unsigned char> renderFrame(fuhglgraphics::scene::Scene& scene,
                                       fuhgl::FirstPersonCamera& cam,
                                       fuhglgraphics::render::DeferredRenderer& renderer, bool wet) {
    using namespace fuhglgraphics;
    for (int i = 0; i < 4; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, W, H);
        frame.effects.enable(render::Effect::Tonemap);
        frame.effects.enable(render::Effect::Ssr);
        frame.effects.applyTo(renderer);
        renderer.setEffectEnabled(render::Effect::Wetness, wet);
        renderer.wetnessAmount = wet ? 1.f : 0.f;
        renderer.wetnessPuddle = 0.8f;
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
    fuhgl::render::shapes::quad(ground, 30.f, {0.4f, 0.4f, 0.42f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.4f, 0.4f, 0.42f};
    ge.roughness = 0.85f; // dry asphalt-ish: SSR mask is weak when rough
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("crate" + std::to_string(i));
        n.translation = {-2.f + 2.f * i, 0.8f, -3.f - i};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = {0.8f, 0.5f - 0.1f * i, 0.2f};
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }
    // an emissive pillar so SSR has something bright to reflect on the wet ground
    fuhgl::render::Mesh pillar;
    fuhgl::render::shapes::box(pillar, {0.4f, 2.5f, 0.4f}, {1, 1, 1});
    scene::Node& pn = scene.root().createChild("pillar");
    pn.translation = {0.f, 2.5f, -8.f};
    scene::MeshEntry& pe = pn.attach<scene::MeshComponent>().addMesh(&pillar);
    pe.emissive = {6.f, 4.f, 1.f};
    pe.castShadow = false;
    pe.materialId = 20;

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.5f, -0.7f, -0.4f}, {1.f, 0.92f, 0.78f}, 2.5f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.5f, 8.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 1.f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.skyTop = {0.16f, 0.32f, 0.64f};
    renderer.skyBottom = {0.88f, 0.84f, 0.74f};
    renderer.ssrMaxSteps = 24;
    renderer.ssrMaxDist = 25.f;
    renderer.ssrStrength = 0.9f;

    const auto dry = renderFrame(scene, cam, renderer, false);
    const auto wet = renderFrame(scene, cam, renderer, true);

    long sumDry = 0, sumWet = 0, diff = 0;
    for (std::size_t i = 0; i < dry.size(); i += 4) {
        sumDry += dry[i] + dry[i + 1] + dry[i + 2];
        sumWet += wet[i] + wet[i + 1] + wet[i + 2];
        diff += std::labs((long)wet[i] - (long)dry[i]) + std::labs((long)wet[i + 1] - (long)dry[i + 1]) +
                std::labs((long)wet[i + 2] - (long)dry[i + 2]);
    }
    std::fprintf(stderr, "gl_smoke_wetness: sumDry=%ld sumWet=%ld |diff|=%ld\n", sumDry, sumWet, diff);
    if (sumWet < 1000) {
        std::fprintf(stderr, "gl_smoke_wetness: wet frame too dark\n");
        return 1;
    }
    if (diff < 1000) {
        std::fprintf(stderr, "gl_smoke_wetness: dry/wet frames nearly identical (|diff|=%ld)\n", diff);
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_wetness: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_wetness: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
