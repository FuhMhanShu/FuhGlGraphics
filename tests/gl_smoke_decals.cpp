// Headless G8 smoke test (label "gl_context"): a ground quad (material ID 1) + a crate
// (material ID 2) with a red decal box over the ground, rendered through the G3
// DeferredRenderer with Effect::Decals on vs off. Verifies: the mark appears (center
// pixels get red) only when enabled, the material-ID mask gates it (mask for ID 2 leaves
// the ground unchanged), and no GL error occurs.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include "test_framework.hpp"

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
    if (!window.create("gl_smoke_decals", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_decals: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_decals: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- scene: ground (materialId 1) + crate (materialId 2) ---
    scene::Scene scene;
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 8.f, {0.5f, 0.5f, 0.52f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.5f, 0.5f, 0.52f};
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    fuhgl::render::Mesh crate;
    fuhgl::render::shapes::box(crate, {0.5f, 0.5f, 0.5f}, {0.6f, 0.45f, 0.3f});
    scene::Node& crateNode = scene.root().createChild("crate");
    crateNode.translation = {1.6f, 0.5f, 0.5f};
    scene::MeshEntry& ce = crateNode.attach<scene::MeshComponent>().addMesh(&crate);
    ce.albedo = {0.6f, 0.45f, 0.3f};
    ce.castShadow = false;
    ce.materialId = 2;

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.3f, -1.f, -0.4f}, {1.f, 0.95f, 0.85f}, 2.f);
    scene.root().createChild("fill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({-2.f, 3.f, 2.f}, {0.4f, 0.5f, 0.7f}, 6.f, 20.f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {3.2f, 3.4f, 5.4f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{0.f, 0.2f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // --- red decal box over the ground center, gated to material ID 1 ---
    scene::Node& decalNode = scene.root().createChild("mark");
    decalNode.translation = {0.f, 0.02f, 0.f};
    scene::DecalComponent& dc = decalNode.attach<scene::DecalComponent>();
    dc.decal.halfExtents = {1.2f, 0.05f, 1.2f};
    dc.decal.albedo = {0.9f, 0.08f, 0.05f};
    dc.decal.opacity = 1.f;
    dc.decal.materialIdMask = 1u; // ground only

    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Decals);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_decals: renderer create failed\n");
        return 1;
    }
    renderer.setEffectEnabled(render::Effect::Taa, false);

    auto renderFrame = [&](bool withDecals, std::uint32_t mask) {
        dc.decal.materialIdMask = mask;
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, W, H);
        if (!withDecals) frame.effects.disable(render::Effect::Decals);
        frame.effects.applyTo(renderer);
        for (int i = 0; i < 2; ++i)
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                            frame.lightProbes, &frame.decals);
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
        fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    const std::vector<unsigned char> off = renderFrame(false, 1u);
    const std::vector<unsigned char> on = renderFrame(true, 1u);
    const std::vector<unsigned char> wrongMask = renderFrame(true, 2u); // crate only: ground untouched
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_decals: FAIL (GL error)\n");
        return 1;
    }

    auto regionStats = [&](const std::vector<unsigned char>& px, int x0, int x1, int y0, int y1,
                           long& sum, long& r, long& g, long& b, long& diffVsOff) {
        sum = r = g = b = diffVsOff = 0;
        long n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * W + x) * 4;
                const long pr = px[o] + px[o + 1] + px[o + 2];
                const long po = off[o] + off[o + 1] + off[o + 2];
                sum += pr;
                r += px[o];
                g += px[o + 1];
                b += px[o + 2];
                if (std::labs(pr - po) > 12) ++diffVsOff;
                ++n;
            }
        if (n > 0) {
            r /= n;
            g /= n;
            b /= n;
            sum /= n;
        }
    };

    // the decal projects onto the ground near the frame center
    long sumOff = 0, rOff = 0, gOff = 0, bOff = 0, dOff = 0;
    long sumOn = 0, rOn = 0, gOn = 0, bOn = 0, dOn = 0;
    long sumMask = 0, rMask = 0, gMask = 0, bMask = 0, dMask = 0;
    regionStats(off, W / 2 - 40, W / 2 + 40, H / 2 - 30, H / 2 + 30, sumOff, rOff, gOff, bOff, dOff);
    regionStats(on, W / 2 - 40, W / 2 + 40, H / 2 - 30, H / 2 + 30, sumOn, rOn, gOn, bOn, dOn);
    regionStats(wrongMask, W / 2 - 40, W / 2 + 40, H / 2 - 30, H / 2 + 30, sumMask, rMask, gMask, bMask,
                dMask);
    std::fprintf(stderr,
                 "gl_smoke_decals: center off=(%ld,%ld,%ld) on=(%ld,%ld,%ld) mask2=(%ld,%ld,%ld) diffOn=%ld "
                 "diffMask=%ld\n",
                 rOff, gOff, bOff, rOn, gOn, bOn, rMask, gMask, bMask, dOn, dMask);
    CHECK(dOn > 200);                    // decals visibly change the center
    CHECK(rOn > gOn + 30 && rOn > bOn + 30); // the mark is red
    CHECK(dMask < 20);                   // wrong material mask -> ground unchanged

    std::fprintf(stderr, "gl_smoke_decals: PASS (GL %d.%d)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor);
    return fuhglgraphicstest::failures() ? 1 : 0;
}
