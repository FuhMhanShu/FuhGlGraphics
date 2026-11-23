// Headless G34 smoke test (label "gl_context"): a white box standing next to a bright red
// wall (sunlit). The box face away from the sun is in shadow — with Effect::Gi on it must
// pick up red bounce light (the GI cones hit the red wall's lit surface), so the frames
// differ and the changed pixels are brighter-dominant with a red tint. Skips (77) no GL.

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
    if (!window.create("gl_smoke_gi", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_gi: no GL context \u2014 SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_gi: context below GL 3.3 \u2014 SKIP\n");
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
    renderer.giStrength = 1.2f;
    renderer.giCones = 4;
    renderer.giSteps = 16;
    renderer.giMaxDist = 6.f;

    fuhgl::render::Mesh ground, box, wall;
    fuhgl::render::shapes::quad(ground, 12.f, {0.4f, 0.4f, 0.4f});
    fuhgl::render::shapes::box(box, {0.7f, 0.7f, 0.7f}, {0.85f, 0.85f, 0.85f});
    fuhgl::render::shapes::box(wall, {0.2f, 1.5f, 3.f}, {1.f, 0.15f, 0.1f}); // red wall behind the box
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.4f, 0.4f, 0.4f}, 0.f, 0.9f);
    fuhgl::render::Material boxMat = fuhgl::render::materials::pbr({0.85f, 0.85f, 0.85f}, 0.f, 0.6f);
    fuhgl::render::Material wallMat = fuhgl::render::materials::pbr({1.f, 0.15f, 0.1f}, 0.f, 0.7f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {3.5f, 2.0f, 4.5f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.6f, -0.5f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&box, &boxMat, fuhgl::math::translate({0.f, 0.7f, 0.f})});
    scene.items.push_back({&wall, &wallMat, fuhgl::math::translate({0.f, 1.5f, -2.2f})});
    // sun from +X/+Z: the wall's +Z face and the box's +X face are lit; the box's -X face
    // (toward the wall... we look at the box from +X+Z, so its -Z/-X sides face the wall)
    scene.lights.push_back(fuhgl::render::Light::directional({-0.7f, -0.6f, -0.4f}, {1.f, 0.95f, 0.9f}, 3.f));
    scene.ambient = {0.1f, 0.1f, 0.12f};

    renderer.setEffectEnabled(render::Effect::Gi, false);
    renderer.render(scene);
    const auto off = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::Gi, true);
    renderer.render(scene);
    const auto on = readOutput(renderer, W, H);

    long diff = 0, brighter = 0, darker = 0;
    long redGain = 0;
    for (std::size_t i = 0; i < off.size(); i += 4) {
        const int a = on[i] + on[i + 1] + on[i + 2];
        const int b = off[i] + off[i + 1] + off[i + 2];
        diff += std::abs(a - b);
        if (a > b) ++brighter;
        else if (a < b) ++darker;
        redGain += (long)on[i] - (long)off[i]; // GI bounce from the red wall should add red
    }
    std::fprintf(stderr, "gl_smoke_gi: diff=%ld brighter=%ld darker=%ld redGain=%ld\n", diff, brighter,
                 darker, redGain);
    if (diff < 500) {
        std::fprintf(stderr, "gl_smoke_gi: GI on/off frames too similar\n");
        return 1;
    }
    if (brighter < darker * 3) {
        std::fprintf(stderr, "gl_smoke_gi: GI did not mostly add light?!\n");
        return 1;
    }
    if (redGain <= 0) {
        std::fprintf(stderr, "gl_smoke_gi: no red bounce from the wall (redGain=%ld)\n", redGain);
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_gi: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_gi: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
