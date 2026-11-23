// Headless G18 smoke test (label "gl_context"): builds a scene with a box (GL mesh + CPU
// meshData), picks the center pixel via scene::pick(), expects a hit on the box node at the
// right distance, then places a small emissive marker at the hit point and verifies the
// rendered image changed around the projected hit position (the marker is visible).
// Skips cleanly with exit code 77 without GL >= 3.3.

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
    if (!window.create("gl_smoke_picking", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_picking: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_picking: context below GL 3.3 — SKIP\n");
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

    scene::Scene scene;
    const fuhgl::render::MeshData boxData = fuhgl::render::shapes::boxData({0.8f, 0.8f, 0.8f}, {0.7f, 0.3f, 0.2f});
    fuhgl::render::Mesh box;
    if (!box.create()) return 1;
    box.setMeshData(boxData);
    scene::Node& boxNode = scene.root().createChild("pickBox");
    boxNode.translation = {0.f, 0.f, -2.f};
    scene::MeshEntry& be = boxNode.attach<scene::MeshComponent>().addMesh(&box);
    be.albedo = {0.7f, 0.3f, 0.2f};
    be.roughness = 0.6f;
    be.meshData = &boxData; // CPU triangles for picking
    be.materialId = 2;

    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 20.f, {0.3f, 0.3f, 0.3f});
    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.3f, 0.3f, 0.3f};
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.4f, -0.9f, -0.3f}, {1.f, 0.95f, 0.9f}, 2.5f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 0.5f, 3.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.f, -2.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    // --- pick the center pixel: must hit the box ---
    scene::PickHit hit;
    if (!scene::pick(scene, &cam, W, H, W / 2.f, H / 2.f, hit)) {
        std::fprintf(stderr, "gl_smoke_picking: center pick missed the box\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_picking: hit node=%s entry=%d dist=%.3f point=(%.2f,%.2f,%.2f) tri=%u\n",
                 hit.node->name().c_str(), hit.meshEntry, hit.distance, hit.point.x, hit.point.y,
                 hit.point.z, hit.triangle);
    if (hit.node != &boxNode) {
        std::fprintf(stderr, "gl_smoke_picking: hit the wrong node (%s)\n",
                     hit.node ? hit.node->name().c_str() : "null");
        return 1;
    }
    // eye (0,0.5,3), box front face at z = -1.2 -> distance ~ 4.2 along -Z (slightly tilted)
    if (hit.distance < 3.5f || hit.distance > 5.5f) {
        std::fprintf(stderr, "gl_smoke_picking: unexpected hit distance %.3f\n", hit.distance);
        return 1;
    }

    // --- render without and with a marker at the hit point; the marker region must change ---
    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);

    auto renderFrame = [&]() {
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, W, H);
        frame.effects.enable(render::Effect::Tonemap);
        frame.effects.applyTo(renderer);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
        return readOutput(renderer, W, H);
    };
    const std::vector<unsigned char> before = renderFrame();

    fuhgl::render::Mesh marker;
    fuhgl::render::shapes::box(marker, {0.12f, 0.12f, 0.12f}, {1.f, 1.f, 0.f});
    scene::Node& markerNode = scene.root().createChild("marker");
    markerNode.translation = hit.point;
    scene::MeshEntry& me = markerNode.attach<scene::MeshComponent>().addMesh(&marker);
    me.emissive = {4.f, 4.f, 0.f}; // bright yellow marker
    me.castShadow = false;
    me.materialId = 9;
    const std::vector<unsigned char> after = renderFrame();

    // project the hit point to screen and compare a small neighborhood
    const fuhgl::math::Vec4 clip = cam.viewProj(1.f) * fuhgl::math::Vec4{hit.point.x, hit.point.y, hit.point.z, 1.f};
    const int px = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * W);
    const int py = static_cast<int>((1.f - (clip.y / clip.w * 0.5f + 0.5f)) * H);
    long diff = 0;
    for (int y = std::max(0, py - 12); y < std::min(H, py + 12); ++y)
        for (int x = std::max(0, px - 12); x < std::min(W, px + 12); ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
            diff += std::abs(static_cast<int>(after[i]) - static_cast<int>(before[i])) +
                    std::abs(static_cast<int>(after[i + 1]) - static_cast<int>(before[i + 1])) +
                    std::abs(static_cast<int>(after[i + 2]) - static_cast<int>(before[i + 2]));
        }
    std::fprintf(stderr, "gl_smoke_picking: marker region diff=%ld at (%d,%d)\n", diff, px, py);
    if (diff < 300) {
        std::fprintf(stderr, "gl_smoke_picking: marker at the hit point not visible in the render\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_picking: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_picking: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
