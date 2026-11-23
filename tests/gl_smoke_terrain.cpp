// Headless G20 smoke test (label "gl_context"): builds a TerrainComponent (F38/F41 LOD
// terrain), renders it with the G3 DeferredRenderer from a near and a far camera and checks
// that SceneView::lastLodLevel reflects the resolved LOD level (0 near, > 0 far, -1 with
// view.lodEnabled = false). Skips cleanly with exit code 77 without GL.

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
    if (!window.create("gl_smoke_terrain", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_terrain: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_terrain: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

long renderAndSum(fuhglgraphics::scene::Scene& scene, fuhgl::FirstPersonCamera& cam,
                  fuhglgraphics::render::DeferredRenderer& renderer, bool lodEnabled,
                  int* outLodLevel = nullptr) {
    fuhglgraphics::scene::FrameData frame;
    scene.fillFrame(frame, &cam, W, H);
    frame.view.lodEnabled = lodEnabled;
    frame.effects.enable(fuhglgraphics::render::Effect::Tonemap);
    frame.effects.applyTo(renderer);
    renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                    frame.lightProbes, &frame.decals, frame.water, &frame.skins);
    if (outLodLevel) *outLodLevel = frame.view.lastLodLevel;
    renderer.output().bind();
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
    fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    fuhgl::gl::flush();
    long sum = 0;
    for (std::size_t i = 0; i < px.size(); i += 4) sum += px[i] + px[i + 1] + px[i + 2];
    std::fprintf(stderr, "gl_smoke_terrain: lastLodLevel=%d (lodEnabled=%d) sum=%ld\n",
                 frame.view.lastLodLevel, (int)lodEnabled, sum);
    return sum;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    scene::Scene scene;
    scene::Node& tn = scene.root().createChild("terrain");
    scene::TerrainComponent& tc = tn.attach<scene::TerrainComponent>();
    tc.levels = 4;
    tc.fovYDeg = 55.f;
    if (!tc.create(96, 96, 160.f, 160.f, 7.f)) {
        std::fprintf(stderr, "gl_smoke_terrain: terrain create failed\n");
        return 1;
    }
    if (tc.lodMesh.levelCount() != 4) {
        std::fprintf(stderr, "gl_smoke_terrain: expected 4 LOD levels, got %d\n",
                     tc.lodMesh.levelCount());
        return 1;
    }
    // heightAt places objects on the terrain
    const float h0 = tc.heightAt(0.f, 0.f);
    std::fprintf(stderr, "gl_smoke_terrain: heightAt(0,0)=%.3f (0..7)\n", h0);
    if (h0 < 0.f || h0 > 7.f) {
        std::fprintf(stderr, "gl_smoke_terrain: heightAt out of range\n");
        return 1;
    }

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.4f, -0.9f, -0.3f}, {1.f, 0.95f, 0.9f}, 2.5f);

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(55.f);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);

    // near camera: LOD level 0, and the frame item carries the LodMesh
    cam.position = {0.f, 8.f, 30.f};
    aimAt(cam, {0.f, 3.f, 0.f});
    int levelNear = -2;
    long sumNear = renderAndSum(scene, cam, renderer, true, &levelNear);
    {
        scene::FrameData probe;
        scene.fillFrame(probe, &cam, W, H);
        if (probe.view.items.empty() || probe.view.items[0].lod != &tc.lodMesh) {
            std::fprintf(stderr, "gl_smoke_terrain: frame item does not carry the LodMesh\n");
            return 1;
        }
    }
    if (levelNear != 0) {
        std::fprintf(stderr, "gl_smoke_terrain: near camera -> lastLodLevel=%d, expected 0\n",
                     levelNear);
        return 1;
    }

    // far camera: higher LOD level (dist ~862 > switch distances 419/839, within zFar=1000)
    cam.position = {0.f, 150.f, 850.f};
    aimAt(cam, {0.f, 0.f, 0.f});
    int levelFar = -2;
    long sumFar = renderAndSum(scene, cam, renderer, true, &levelFar);
    if (levelFar <= 0) {
        std::fprintf(stderr, "gl_smoke_terrain: far camera -> lastLodLevel=%d, expected > 0\n",
                     levelFar);
        return 1;
    }

    // LOD disabled: level -1 (plain mesh path)
    int levelOff = -2;
    renderAndSum(scene, cam, renderer, false, &levelOff);
    if (levelOff != -1) {
        std::fprintf(stderr, "gl_smoke_terrain: lod off -> lastLodLevel=%d, expected -1\n", levelOff);
        return 1;
    }

    if (sumNear < 1000 || sumFar < 1000) {
        std::fprintf(stderr, "gl_smoke_terrain: rendered frames too dark (near=%ld far=%ld)\n",
                     sumNear, sumFar);
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_terrain: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_terrain: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
