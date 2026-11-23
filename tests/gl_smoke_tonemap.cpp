// Headless G23 smoke test (label "gl_context"): renders a small HDR scene (bright lamp +
// sun) once per tonemap operator (ACES / Reinhard / Uncharted2 / linear) and verifies the
// frames differ measurably between operators, that each is in a valid range, and that no GL
// error is left over. Skips cleanly with exit code 77 without GL.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

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
    if (!window.create("gl_smoke_tonemap", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_tonemap: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_tonemap: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

std::vector<unsigned char> renderFrame(fuhglgraphics::scene::Scene& scene,
                                       fuhgl::FirstPersonCamera& cam,
                                       fuhglgraphics::render::DeferredRenderer& renderer,
                                       fuhglgraphics::render::TonemapOperator op) {
    using namespace fuhglgraphics;
    for (int i = 0; i < 4; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, W, H);
        frame.effects.enable(render::Effect::Tonemap);
        frame.effects.applyTo(renderer);
        renderer.tonemapOperator = op;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins);
    }
    renderer.output().bind();
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
    fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    fuhgl::gl::flush();
    return px;
}

long meanDiff(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    long d = 0;
    for (std::size_t i = 0; i < a.size(); i += 4)
        d += std::labs((long)a[i] - (long)b[i]) + std::labs((long)a[i + 1] - (long)b[i + 1]) +
             std::labs((long)a[i + 2] - (long)b[i + 2]);
    return d / static_cast<long>(a.size() / 4);
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    scene::Scene scene;
    fuhgl::render::Mesh ground, box, lamp;
    fuhgl::render::shapes::quad(ground, 30.f, {0.35f, 0.34f, 0.31f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});
    fuhgl::render::shapes::box(lamp, {0.25f, 0.25f, 0.25f}, {1, 1, 1});
    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.34f, 0.31f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);
    scene::Node& cn = scene.root().createChild("crate");
    cn.translation = {0.f, 0.8f, -3.f};
    scene::MeshEntry& ce = cn.attach<scene::MeshComponent>().addMesh(&box);
    ce.albedo = {0.8f, 0.4f, 0.2f};
    ce.roughness = 0.6f;
    // HDR lamp: strong emissive -> different operators show different highlight rolloff
    scene::Node& ln = scene.root().createChild("lamp");
    ln.translation = {1.5f, 1.2f, -4.f};
    scene::MeshEntry& le = ln.attach<scene::MeshComponent>().addMesh(&lamp);
    le.emissive = {12.f, 8.f, 2.f};
    le.castShadow = false;
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.5f, -0.7f, -0.4f}, {1.f, 0.92f, 0.78f}, 3.f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.f, 6.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.8f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.skyTop = {0.16f, 0.32f, 0.64f};
    renderer.skyBottom = {0.88f, 0.84f, 0.74f};
    renderer.exposure = 1.2f;

    const render::TonemapOperator ops[4] = {render::TonemapOperator::Aces, render::TonemapOperator::Reinhard,
                                            render::TonemapOperator::Uncharted2, render::TonemapOperator::Linear};
    std::vector<unsigned char> frames[4];
    long sum[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        frames[i] = renderFrame(scene, cam, renderer, ops[i]);
        for (std::size_t k = 0; k < frames[i].size(); k += 4)
            sum[i] += frames[i][k] + frames[i][k + 1] + frames[i][k + 2];
        std::fprintf(stderr, "gl_smoke_tonemap: op=%s sum=%ld\n", render::tonemapOperatorName(ops[i]), sum[i]);
        if (sum[i] < 1000) {
            std::fprintf(stderr, "gl_smoke_tonemap: frame for op %d too dark\n", i);
            return 1;
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) {
            const long d = meanDiff(frames[i], frames[j]);
            std::fprintf(stderr, "gl_smoke_tonemap: |%s - %s| = %ld\n",
                         render::tonemapOperatorName(ops[i]), render::tonemapOperatorName(ops[j]), d);
            if (d < 20) {
                std::fprintf(stderr, "gl_smoke_tonemap: operators %d/%d nearly identical (%ld)\n", i, j, d);
                return 1;
            }
        }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_tonemap: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_tonemap: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
