// Headless G30 smoke test (label "gl_context"): a scene with the sun low in front of the
// camera. Effect::LensFlare + Effect::Anamorphic on vs off must change the frame measurably,
// and the added light must concentrate on the sun's screen row (anamorphic = horizontal
// streak through the sun). Skips (exit code 77) without GL >= 3.3.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <algorithm>
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
    if (!window.create("gl_smoke_flare", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_flare: no GL context \u2014 SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_flare: context below GL 3.3 \u2014 SKIP\n");
        return false;
    }
    return true;
}

std::vector<unsigned char> readOutput(fuhglgraphics::render::DeferredRenderer& renderer, int w, int h) {
    renderer.output().bind();
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4, 0);
    fuhgl::gl::readPixels(0, 0, w, h, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    fuhgl::gl::flush();
    return px;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.setEffectEnabled(render::Effect::Bloom, true);
    renderer.bloomThreshold = 1.0f;
    renderer.bloomStrength = 0.6f;
    renderer.lensFlareStrength = 1.2f;
    renderer.anamorphicStrength = 0.8f;
    renderer.anamorphicStretch = 8.f;
    renderer.anamorphicThreshold = 0.8f;

    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 20.f, {0.3f, 0.32f, 0.3f});
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.3f, 0.32f, 0.3f}, 0.f, 0.95f);

    // sun low in front of the camera (-Z), HDR-bright disk
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.15f, 0.22f, -1.f});
    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 1.5f, 6.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = sunDir; // look straight at the sun
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.lights.push_back(fuhgl::render::Light::directional(-sunDir, {1.f, 0.9f, 0.75f}, 3.f));
    scene.ambient = {0.2f, 0.22f, 0.28f};
    scene.sunDirection = sunDir;
    scene.sunColor = {4.f, 3.f, 2.f}; // HDR sun disk (bloom source + flare center)
    scene.sunCos = 0.9995f;

    // sun screen position (same math as drawFlare)
    const fuhgl::math::Mat4 vp = cam.viewProj(1.f);
    const fuhgl::math::Vec4 clip = vp * fuhgl::math::Vec4(sunDir, 0.f);
    if (clip.w <= 1e-3f) {
        std::fprintf(stderr, "gl_smoke_flare: sun not in front of the camera?!\n");
        return 1;
    }
    const int sunX = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * W);
    const int sunY = static_cast<int>((clip.y / clip.w * 0.5f + 0.5f) * H);
    std::fprintf(stderr, "gl_smoke_flare: sun screen pos ~ (%d, %d)\n", sunX, sunY);

    renderer.setEffectEnabled(render::Effect::LensFlare, false);
    renderer.setEffectEnabled(render::Effect::Anamorphic, false);
    renderer.render(scene);
    const auto off = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::LensFlare, true);
    renderer.setEffectEnabled(render::Effect::Anamorphic, true);
    renderer.render(scene);
    const auto both = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::LensFlare, false);
    renderer.setEffectEnabled(render::Effect::Anamorphic, true);
    renderer.render(scene);
    const auto anaOnly = readOutput(renderer, W, H);

    long diffBoth = 0, diffAna = 0;
    for (std::size_t i = 0; i < off.size(); i += 4) {
        diffBoth += std::labs((long)both[i] - off[i]) + std::labs((long)both[i + 1] - off[i + 1]) +
                    std::labs((long)both[i + 2] - off[i + 2]);
        diffAna += std::labs((long)anaOnly[i] - off[i]) + std::labs((long)anaOnly[i + 1] - off[i + 1]) +
                   std::labs((long)anaOnly[i + 2] - off[i + 2]);
    }
    // anamorphic streak: the sun's row must gain more light than a row far above the sun
    long rowSun = 0, rowFar = 0;
    for (int x = 0; x < W; ++x) {
        rowSun += std::labs((long)anaOnly[(static_cast<std::size_t>(sunY) * W + x) * 4] -
                            (long)off[(static_cast<std::size_t>(sunY) * W + x) * 4]) +
                  std::labs((long)anaOnly[(static_cast<std::size_t>(sunY) * W + x) * 4 + 1] -
                            (long)off[(static_cast<std::size_t>(sunY) * W + x) * 4 + 1]);
        const int yFar = std::max(0, sunY - H / 4);
        rowFar += std::labs((long)anaOnly[(static_cast<std::size_t>(yFar) * W + x) * 4] -
                            (long)off[(static_cast<std::size_t>(yFar) * W + x) * 4]) +
                  std::labs((long)anaOnly[(static_cast<std::size_t>(yFar) * W + x) * 4 + 1] -
                            (long)off[(static_cast<std::size_t>(yFar) * W + x) * 4 + 1]);
    }
    std::fprintf(stderr, "gl_smoke_flare: diffBoth=%ld diffAna=%ld rowSun=%ld rowFar=%ld\n", diffBoth,
                 diffAna, rowSun, rowFar);
    if (diffBoth < 2000) {
        std::fprintf(stderr, "gl_smoke_flare: flare on/off frames too similar\n");
        return 1;
    }
    if (diffAna < 1000) {
        std::fprintf(stderr, "gl_smoke_flare: anamorphic-only on/off frames too similar\n");
        return 1;
    }
    if (rowSun < rowFar * 4) {
        std::fprintf(stderr, "gl_smoke_flare: anamorphic streak not concentrated on the sun row "
                             "(rowSun=%ld rowFar=%ld)\n", rowSun, rowFar);
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_flare: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_flare: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
