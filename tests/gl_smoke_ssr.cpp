// Headless G16 smoke test (label "gl_context"): a mirror-like floor (metallic 1, roughness
// 0.05) with a bright emissive box above it, camera looking at the floor at an angle — with
// Effect::Ssr on, the box's reflection appears in the floor (the floor region gets brighter);
// with it off the floor is plain PBR. Skips cleanly with exit code 77 without GL >= 3.3.

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
    if (!window.create("gl_smoke_ssr", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_ssr: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_ssr: context below GL 3.3 — SKIP\n");
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
    renderer.ssrMaxSteps = 48;
    renderer.ssrMaxDist = 40.f;
    renderer.ssrStrength = 1.0f;

    // mirror floor + bright emissive box above it
    fuhgl::render::Mesh floorMesh, box;
    fuhgl::render::shapes::quad(floorMesh, 14.f, {0.6f, 0.62f, 0.65f});
    fuhgl::render::shapes::box(box, {0.5f, 0.5f, 0.5f}, {1.f, 0.1f, 0.1f});
    fuhgl::render::Material floorMat = fuhgl::render::materials::pbr({0.6f, 0.62f, 0.65f}, 1.f, 0.05f);
    fuhgl::render::Material boxMat = fuhgl::render::materials::pbr({1.f, 0.1f, 0.1f}, 0.f, 0.4f);
    boxMat.emissive = {6.f, 0.4f, 0.4f}; // HDR-bright -> visible in the reflection

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.2f, 6.5f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.4f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&floorMesh, &floorMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&box, &boxMat, fuhgl::math::translate({0.f, 1.6f, -1.5f})});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.3f, -0.9f, -0.2f}, {1.f, 0.95f, 0.9f}, 2.f));
    scene.ambient = {0.15f, 0.15f, 0.17f};

    renderer.setEffectEnabled(render::Effect::Ssr, true);
    renderer.render(scene);
    const std::vector<unsigned char> withSsr = readOutput(renderer, W, H);

    renderer.setEffectEnabled(render::Effect::Ssr, false);
    renderer.render(scene);
    const std::vector<unsigned char> withoutSsr = readOutput(renderer, W, H);

    // floor region = lower 2/3 of the image: with SSR it must be brighter (the red box is
    // reflected in the mirror floor)
    long diff = 0, sumOn = 0, sumOff = 0;
    for (int y = H / 3; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
            const int a = withSsr[i] + withSsr[i + 1] + withSsr[i + 2];
            const int b = withoutSsr[i] + withoutSsr[i + 1] + withoutSsr[i + 2];
            diff += std::abs(a - b);
            sumOn += a;
            sumOff += b;
        }
    std::fprintf(stderr, "gl_smoke_ssr: floorRegion diff=%ld sumOn=%ld sumOff=%ld\n", diff, sumOn, sumOff);
    if (diff < 300) {
        std::fprintf(stderr, "gl_smoke_ssr: SSR on/off too similar in the floor region\n");
        return 1;
    }
    if (sumOn <= sumOff) {
        std::fprintf(stderr, "gl_smoke_ssr: floor not brighter with SSR on (no reflection?)\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_ssr: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_ssr: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
