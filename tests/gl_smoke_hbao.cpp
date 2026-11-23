// Headless G25 smoke test (label "gl_context"): box on ground — with Effect::Hbao on vs off
// the frames must differ and the changed pixels must mostly get darker (horizon-based AO is
// multiplicative), concentrated at the contact corner. Skips (exit code 77) without GL >= 3.3.

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
    if (!window.create("gl_smoke_hbao", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_hbao: no GL context \u2014 SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_hbao: context below GL 3.3 \u2014 SKIP\n");
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
    renderer.hbaoRadius = 1.2f;
    renderer.hbaoDirections = 8;
    renderer.hbaoSteps = 4;
    renderer.hbaoPower = 2.0f;

    fuhgl::render::Mesh ground, box;
    fuhgl::render::shapes::quad(ground, 10.f, {0.5f, 0.5f, 0.5f});
    fuhgl::render::shapes::box(box, {0.7f, 0.7f, 0.7f}, {0.85f, 0.4f, 0.2f});
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.5f, 0.5f, 0.5f}, 0.f, 0.9f);
    fuhgl::render::Material boxMat = fuhgl::render::materials::pbr({0.85f, 0.4f, 0.2f}, 0.1f, 0.5f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {3.2f, 2.2f, 4.2f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.2f, 0.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&box, &boxMat, fuhgl::math::translate({0.f, 0.7f, 0.f})});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.4f, -0.9f, -0.3f}, {1.f, 0.95f, 0.9f}, 2.5f));
    scene.ambient = {0.25f, 0.25f, 0.28f};

    renderer.setEffectEnabled(render::Effect::Hbao, true);
    renderer.render(scene);
    const std::vector<unsigned char> withHbao = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::Hbao, false);
    renderer.render(scene);
    const std::vector<unsigned char> withoutHbao = readOutput(renderer, W, H);

    if (!renderer.postGraph().texture("hbao")) {
        std::fprintf(stderr, "gl_smoke_hbao: hbao resource not created\n");
        return 1;
    }

    long diff = 0;
    long hbaoDarker = 0, hbaoBrighter = 0;
    for (std::size_t i = 0; i < withHbao.size(); i += 4) {
        const int a = withHbao[i] + withHbao[i + 1] + withHbao[i + 2];
        const int b = withoutHbao[i] + withoutHbao[i + 1] + withoutHbao[i + 2];
        diff += std::abs(a - b);
        if (a < b) ++hbaoDarker;
        else if (a > b) ++hbaoBrighter;
    }
    std::fprintf(stderr, "gl_smoke_hbao: diff=%ld darkerWithHbao=%ld brighterWithHbao=%ld\n", diff,
                 hbaoDarker, hbaoBrighter);
    if (diff < 500) {
        std::fprintf(stderr, "gl_smoke_hbao: HBAO on/off frames too similar\n");
        return 1;
    }
    if (hbaoDarker < hbaoBrighter * 4) {
        std::fprintf(stderr, "gl_smoke_hbao: HBAO made more pixels brighter than darker?!\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_hbao: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_hbao: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
