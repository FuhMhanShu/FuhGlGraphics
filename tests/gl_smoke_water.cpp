// Headless G13 smoke test (label "gl_context"): renders a small scene (ground plane, a box
// above the water line) plus a water::WaterSurface through the G3 DeferredRenderer with
// Effect::Water on vs off via a surfaceless EGL context, and verifies that the frames
// differ in the water region and that the planar-reflection texture is alive.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_water", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_water: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_water: context below GL 3.3 — SKIP\n");
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
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    const int W = 256, H = 256;
    fuhglgraphics::render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_water: renderer create failed\n");
        return 1;
    }

    // scene: seabed plane below the water line, a floating box, the water surface at y = 0
    fuhgl::render::Mesh ground, box;
    fuhgl::render::shapes::quad(ground, 12.f, {0.25f, 0.3f, 0.28f});
    fuhgl::render::shapes::box(box, {0.6f, 0.6f, 0.6f}, {0.9f, 0.5f, 0.2f});
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.25f, 0.3f, 0.28f}, 0.f, 0.9f);
    fuhgl::render::Material boxMat = fuhgl::render::materials::pbr({0.9f, 0.5f, 0.2f}, 0.2f, 0.4f);

    fuhglgraphics::water::WaterSurface water;
    water.setDefaultWaves();
    if (!water.create(64, 24.f)) {
        std::fprintf(stderr, "gl_smoke_water: water create failed\n");
        return 1;
    }

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 3.f, 9.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{0.f, 0.f, -2.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f) *
                                                        fuhgl::math::translate({0.f, -1.5f, 0.f})});
    scene.items.push_back({&box, &boxMat, fuhgl::math::translate({0.f, 0.9f, -1.f})});
    scene.items.push_back({&water.mesh(), nullptr, fuhgl::math::Mat4{}});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.4f, -0.8f, -0.3f}, {1.f, 0.95f, 0.85f}, 3.f));
    scene.fogColor = {0.55f, 0.65f, 0.75f};
    scene.fogDensity = 0.01f;

    std::vector<std::uint32_t> materialIds = {1u, 2u, 0u};
    std::vector<bool> castShadow = {true, true, false};

    // water ON: two animated frames
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::Water, true);
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::Taa, false);
    water.update(0.0f);
    renderer.render(scene, &materialIds, &castShadow, nullptr, nullptr, &water);
    const std::vector<unsigned char> withWater = readOutput(renderer, W, H);
    water.update(0.35f);
    renderer.render(scene, &materialIds, &castShadow, nullptr, nullptr, &water);
    const std::vector<unsigned char> withWater2 = readOutput(renderer, W, H);

    // water OFF: the mesh falls back to plain G-buffer PBR rendering
    renderer.setEffectEnabled(fuhglgraphics::render::Effect::Water, false);
    renderer.render(scene, &materialIds, &castShadow, nullptr, nullptr, &water);
    const std::vector<unsigned char> withoutWater = readOutput(renderer, W, H);

    if (renderer.reflectionTexture().id() == 0) {
        std::fprintf(stderr, "gl_smoke_water: reflection texture not created\n");
        return 1;
    }

    // water region = lower half of the image; frames must differ there (waves + reflection
    // vs plain PBR), and the two animated water frames must differ as well (time advances)
    auto regionDiff = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        long sum = 0;
        for (int y = 0; y < H / 2; ++y)
            for (int x = 0; x < W; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
                sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) +
                       std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1])) +
                       std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2]));
            }
        return sum;
    };
    const long diffOnOff = regionDiff(withWater, withoutWater);
    const long diffAnim = regionDiff(withWater, withWater2);
    std::fprintf(stderr, "gl_smoke_water: regionDiff(on,off)=%ld (anim)=%ld\n", diffOnOff, diffAnim);
    if (diffOnOff < 500) {
        std::fprintf(stderr, "gl_smoke_water: water-on vs water-off frames too similar\n");
        return 1;
    }
    if (diffAnim < 50) {
        std::fprintf(stderr, "gl_smoke_water: animated water frames identical (time not applied?)\n");
        return 1;
    }
    // the lower half (water area) must contain non-sky pixels (the sky is bright everywhere)
    {
        int darkest = 255;
        for (int y = 0; y < H / 2; ++y)
            for (int x = 0; x < W; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
                const int mn = std::min(withWater[i], std::min(withWater[i + 1], withWater[i + 2]));
                darkest = std::min(darkest, mn);
            }
        std::fprintf(stderr, "gl_smoke_water: darkest water-region channel = %d\n", darkest);
        if (darkest > 200) {
            std::fprintf(stderr, "gl_smoke_water: water region looks like sky, water not rendered?\n");
            return 1;
        }
    }
    std::fprintf(stderr, "gl_smoke_water: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
