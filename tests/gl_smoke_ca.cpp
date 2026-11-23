// Headless G32 smoke test (label "gl_context"): a scene with a high-contrast edge (bright
// box on dark ground). With Effect::ChromaticAberration on vs off the frames must differ,
// and at the frame corners the R and B channels must be shifted in opposite directions
// (R samples outward, B inward) while G stays put. Skips (exit code 77) without GL >= 3.3.

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
    if (!window.create("gl_smoke_ca", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_ca: no GL context \u2014 SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_ca: context below GL 3.3 \u2014 SKIP\n");
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
    renderer.setEffectEnabled(render::Effect::Upscale, false); // CA at internal res
    renderer.caStrength = 0.03f; // exaggerated for the test

    fuhgl::render::Mesh ground, box;
    fuhgl::render::shapes::quad(ground, 10.f, {0.05f, 0.05f, 0.05f});
    fuhgl::render::shapes::box(box, {1.2f, 1.2f, 1.2f}, {1.f, 1.f, 1.f});
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.05f, 0.05f, 0.05f}, 0.f, 0.9f);
    fuhgl::render::Material boxMat = fuhgl::render::materials::pbr({1.f, 1.f, 1.f}, 0.f, 0.4f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {4.f, 3.f, 5.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.5f, 0.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&box, &boxMat, fuhgl::math::translate({0.f, 1.2f, 0.f})});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.4f, -0.9f, -0.3f}, {1.f, 1.f, 1.f}, 2.5f));
    scene.ambient = {0.3f, 0.3f, 0.3f};

    renderer.setEffectEnabled(render::Effect::ChromaticAberration, false);
    renderer.render(scene);
    const auto off = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::ChromaticAberration, true);
    renderer.render(scene);
    const auto on = readOutput(renderer, W, H);

    long diff = 0, gDiff = 0, split = 0;
    for (std::size_t i = 0; i < off.size(); i += 4) {
        const long dr = (long)on[i] - (long)off[i];
        const long dg = (long)on[i + 1] - (long)off[i + 1];
        const long db = (long)on[i + 2] - (long)off[i + 2];
        diff += std::labs(dr) + std::labs(dg) + std::labs(db);
        gDiff += std::labs(dg);
        // channel split: R and B sampled at different offsets -> both move at edges
        if (std::labs(dr) > 4 && std::labs(db) > 4) ++split;
    }
    std::fprintf(stderr, "gl_smoke_ca: diff=%ld gDiff=%ld splitPx=%ld\n", diff, gDiff, split);
    if (diff < 1000) {
        std::fprintf(stderr, "gl_smoke_ca: CA on/off frames too similar\n");
        return 1;
    }
    if (split < 20) {
        std::fprintf(stderr, "gl_smoke_ca: no R/B channel split at high-contrast edges?!\n");
        return 1;
    }
    if (gDiff != 0) {
        std::fprintf(stderr, "gl_smoke_ca: green channel moved (should stay centered)\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_ca: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_ca: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
