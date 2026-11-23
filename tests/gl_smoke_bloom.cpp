// Headless G17 smoke test (label "gl_context"): a dark scene with one HDR-bright emissive
// quad. With Effect::Bloom on (threshold 1.0) the pixels around the quad get brighter
// (bloom halo); with the effect off they don't. With a very high threshold (100) even with
// the effect on there is no bloom (nothing passes the bright-pass) — the adjustable
// threshold works. Skips cleanly with exit code 77 without GL >= 3.3.

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
    if (!window.create("gl_smoke_bloom", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_bloom: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_bloom: context below GL 3.3 — SKIP\n");
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

long regionSum(const std::vector<unsigned char>& px, int x0, int y0, int x1, int y1) {
    long sum = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
            sum += px[i] + px[i + 1] + px[i + 2];
        }
    return sum;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.bloomThreshold = 1.0f;
    renderer.bloomStrength = 1.2f;
    renderer.bloomRadius = 2.0f;

    fuhgl::render::Mesh ground, lamp;
    fuhgl::render::shapes::quad(ground, 10.f, {0.08f, 0.08f, 0.09f});
    fuhgl::render::shapes::box(lamp, {0.4f, 0.4f, 0.4f}, {1.f, 0.9f, 0.6f});
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.08f, 0.08f, 0.09f}, 0.f, 0.9f);
    fuhgl::render::Material lampMat = fuhgl::render::materials::pbr({1.f, 0.9f, 0.6f}, 0.f, 0.5f);
    lampMat.emissive = {8.f, 6.f, 3.f}; // HDR-bright lamp -> blooms

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 1.6f, 4.5f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.8f, 0.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&lamp, &lampMat, fuhgl::math::translate({0.f, 0.8f, 0.f})});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.4f, -0.9f, -0.3f}, {1.f, 0.95f, 0.9f}, 1.5f));
    scene.ambient = {0.05f, 0.05f, 0.06f};
    scene.clearColor = {0.02f, 0.02f, 0.03f};

    // ring around the lamp (the halo region), excluding the lamp itself
    const int ringX0 = W / 2 - 70, ringX1 = W / 2 + 70, ringY0 = H / 2 - 70, ringY1 = H / 2 + 70;
    const int coreX0 = W / 2 - 18, coreX1 = W / 2 + 18, coreY0 = H / 2 - 18, coreY1 = H / 2 + 18;

    renderer.setEffectEnabled(render::Effect::Bloom, false);
    renderer.render(scene);
    const std::vector<unsigned char> off = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::Bloom, true);
    renderer.render(scene);
    const std::vector<unsigned char> on = readOutput(renderer, W, H);

    long ringOff = 0, ringOn = 0;
    for (int y = ringY0; y < ringY1; ++y)
        for (int x = ringX0; x < ringX1; ++x) {
            if (x >= coreX0 && x < coreX1 && y >= coreY0 && y < coreY1) continue; // skip the lamp core
            const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
            ringOff += off[i] + off[i + 1] + off[i + 2];
            ringOn += on[i] + on[i + 1] + on[i + 2];
        }
    std::fprintf(stderr, "gl_smoke_bloom: halo ring sum off=%ld on=%ld\n", ringOff, ringOn);
    if (ringOn <= ringOff + 200) {
        std::fprintf(stderr, "gl_smoke_bloom: no bloom halo around the bright lamp\n");
        return 1;
    }

    // adjustable threshold: threshold 100 -> nothing passes the bright-pass -> no bloom
    renderer.bloomThreshold = 100.f;
    renderer.render(scene);
    const std::vector<unsigned char> highThr = readOutput(renderer, W, H);
    long ringHigh = 0;
    for (int y = ringY0; y < ringY1; ++y)
        for (int x = ringX0; x < ringX1; ++x) {
            if (x >= coreX0 && x < coreX1 && y >= coreY0 && y < coreY1) continue;
            const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
            ringHigh += highThr[i] + highThr[i + 1] + highThr[i + 2];
        }
    std::fprintf(stderr, "gl_smoke_bloom: halo ring sum threshold=100: %ld (off was %ld)\n", ringHigh, ringOff);
    if (std::abs(ringHigh - ringOff) > 200) {
        std::fprintf(stderr, "gl_smoke_bloom: high threshold still blooms?!\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_bloom: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_bloom: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
