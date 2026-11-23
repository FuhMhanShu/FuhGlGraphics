// Headless G33 smoke test (label "gl_context"): a thin white panel backlit by the sun
// (sun behind the panel, camera in front). With Effect::Subsurface on vs off the panel's
// dark side must get measurably brighter (wrap diffuse + backlit translucency), i.e. the
// changed pixels must be brighter-dominant. The per-item factor travels via the
// `subsurface` parallel array into G-buffer pbr.a. Skips (exit code 77) without GL >= 3.3.

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
    if (!window.create("gl_smoke_sss", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_sss: no GL context \u2014 SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_sss: context below GL 3.3 \u2014 SKIP\n");
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
    renderer.sssDistortion = 0.4f;
    renderer.sssPower = 2.f;
    renderer.sssScale = 2.5f;
    renderer.sssThickness = 1.5f;

    // ground + thin panel facing the camera; sun BEHIND the panel -> its camera side is
    // backlit (dot(N, L) < 0), exactly where SSS translucency shows
    fuhgl::render::Mesh ground, panel;
    fuhgl::render::shapes::quad(ground, 10.f, {0.3f, 0.3f, 0.3f});
    fuhgl::render::shapes::box(panel, {1.5f, 2.f, 0.04f}, {1.f, 1.f, 1.f});
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.3f, 0.3f, 0.3f}, 0.f, 0.9f);
    fuhgl::render::Material panelMat = fuhgl::render::materials::pbr({0.9f, 0.88f, 0.8f}, 0.f, 0.6f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 1.6f, 6.f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 1.4f, 0.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&panel, &panelMat, fuhgl::math::translate({0.f, 1.4f, 0.f})});
    scene.lights.push_back(fuhgl::render::Light::directional({0.2f, -0.6f, 1.f}, {1.f, 0.95f, 0.85f}, 3.f));
    scene.ambient = {0.12f, 0.12f, 0.14f};
    std::vector<float> subsurface = {0.f, 1.f}; // ground: none, panel: full SSS

    renderer.setEffectEnabled(render::Effect::Subsurface, false);
    renderer.render(scene, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &subsurface);
    const auto off = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::Subsurface, true);
    renderer.render(scene, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &subsurface);
    const auto on = readOutput(renderer, W, H);

    long diff = 0, brighter = 0, darker = 0;
    for (std::size_t i = 0; i < off.size(); i += 4) {
        const int a = on[i] + on[i + 1] + on[i + 2];
        const int b = off[i] + off[i + 1] + off[i + 2];
        diff += std::abs(a - b);
        if (a > b) ++brighter;
        else if (a < b) ++darker;
    }
    std::fprintf(stderr, "gl_smoke_sss: diff=%ld brighterWithSss=%ld darkerWithSss=%ld\n", diff, brighter,
                 darker);
    if (diff < 500) {
        std::fprintf(stderr, "gl_smoke_sss: SSS on/off frames too similar\n");
        return 1;
    }
    // SSS only ADDS light on the backlit panel: changed pixels must be brighter-dominant
    if (brighter < darker * 4) {
        std::fprintf(stderr, "gl_smoke_sss: SSS did not brighten the backlit panel?!\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_sss: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_sss: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
