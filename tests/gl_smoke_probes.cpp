// Headless G7 smoke test (label "gl_context"): bakes an SH light-probe grid over a
// colored-wall corner on the CPU, uploads it, and renders through the G3 DeferredRenderer
// with Effect::LightProbes on vs off. Verifies: the grid uploaded (3D textures exist),
// the frames differ, the frame gets brighter overall with probes (indirect light in the
// shadowed corner), and no GL error occurs.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include "test_framework.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
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
    if (!window.create("gl_smoke_probes", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_probes: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_probes: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

fuhgl::math::Vec3 mdVert(const fuhgl::render::MeshData& md, unsigned idx) {
    const std::size_t o = static_cast<std::size_t>(idx) * fuhgl::render::MeshData::kFloatsPerVertex;
    return {md.vertices[o], md.vertices[o + 1], md.vertices[o + 2]};
}

// Adds a box mesh to the scene AND the matching world-space triangles to the bake soup.
fuhgl::render::Mesh& addBox(fuhglgraphics::scene::Scene& scene, std::deque<fuhgl::render::Mesh>& meshes,
                           fuhglgraphics::probes::BakeInput& bake, const char* name,
                           const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& half,
                           const fuhgl::math::Vec3& albedo) {
    meshes.emplace_back();
    fuhgl::render::Mesh& m = meshes.back();
    const fuhgl::render::MeshData md = fuhgl::render::shapes::boxData(half, albedo);
    m.create();
    m.setMeshData(md);
    auto& node = scene.root().createChild(name);
    node.translation = pos;
    auto& e = node.attach<fuhglgraphics::scene::MeshComponent>().addMesh(&m);
    e.albedo = albedo;
    e.roughness = 0.9f;
    e.castShadow = true;
    for (std::size_t i = 0; i + 2 < md.indices.size(); i += 3) {
        const fuhgl::math::Vec3 a = mdVert(md, md.indices[i]) + pos;
        const fuhgl::math::Vec3 b = mdVert(md, md.indices[i + 1]) + pos;
        const fuhgl::math::Vec3 c = mdVert(md, md.indices[i + 2]) + pos;
        bake.triangles.push_back(fuhglgraphics::probes::ProbeTriangle{a, b, c, albedo, {0.f, 0.f, 0.f}});
    }
    return m;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- scene: floor + red left wall + back wall + occluder box in the corner ---
    scene::Scene scene;
    std::deque<fuhgl::render::Mesh> meshes;
    probes::BakeInput bake;
    addBox(scene, meshes, bake, "floor", {0.f, -0.1f, 0.f}, {4.f, 0.1f, 4.f}, {0.55f, 0.55f, 0.55f});
    addBox(scene, meshes, bake, "leftWall", {-4.f, 1.5f, 0.f}, {0.1f, 1.5f, 4.f}, {0.8f, 0.15f, 0.12f});
    addBox(scene, meshes, bake, "backWall", {0.f, 1.5f, -4.f}, {4.f, 1.5f, 0.1f}, {0.5f, 0.55f, 0.6f});
    addBox(scene, meshes, bake, "occluder", {-2.6f, 0.5f, -2.6f}, {0.6f, 0.5f, 0.6f}, {0.3f, 0.3f, 0.32f});

    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.45f, -1.f, 0.35f});
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(sunDir, {1.f, 0.9f, 0.75f}, 2.5f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {3.6f, 2.4f, 5.6f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{-2.2f, 0.7f, -2.2f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // --- bake the probe grid (CPU) ---
    bake.lights.push_back(fuhgl::render::Light::directional(sunDir, {1.f, 0.9f, 0.75f}, 2.5f));
    const fuhgl::math::Vec3 skyTop{0.20f, 0.38f, 0.75f}, skyBottom{0.90f, 0.85f, 0.72f};
    bake.skyRadiance = [&](const fuhgl::math::Vec3& dir) {
        const float t = fuhgl::math::clamp(dir.y, 0.f, 1.f);
        return fuhgl::math::lerp(skyBottom, skyTop, t);
    };
    bake.raySamples = 96;

    scene::Node& probeNode = scene.root().createChild("probes");
    scene::LightProbeComponent& pc = probeNode.attach<scene::LightProbeComponent>();
    if (!pc.grid.create(6, 4, 6, {-4.f, 0.05f, -4.f}, {4.f, 3.f, 4.f})) {
        std::fprintf(stderr, "gl_smoke_probes: FAIL (grid create)\n");
        return 1;
    }
    if (!pc.grid.bake(bake)) {
        std::fprintf(stderr, "gl_smoke_probes: FAIL (bake)\n");
        return 1;
    }
    // sanity: the corner probe near the red wall sees red indirect light
    const fuhgl::math::Vec3 eRed = probes::shIrradiance(pc.grid.coeffs(0, 1, 0), {-1.f, 0.f, 0.f});
    std::fprintf(stderr, "gl_smoke_probes: corner E(-X) = %.3f %.3f %.3f\n", eRed.x, eRed.y, eRed.z);
    CHECK(eRed.x > eRed.z);

    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::LightProbes);
    scene.root().effects.params.shadowCenter = {0.f, 1.f, 0.f};
    scene.root().effects.params.shadowRadius = 7.f;
    scene.root().effects.params.lightProbeStrength = 1.f;

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_probes: renderer create failed\n");
        return 1;
    }
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.enableCsmShadows(1024, 4);
    renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 7.f);

    auto renderFrame = [&](bool withProbes) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, W, H);
        if (!withProbes) frame.effects.disable(render::Effect::LightProbes);
        frame.effects.applyTo(renderer);
        for (int i = 0; i < 2; ++i)
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                            withProbes ? frame.lightProbes : nullptr);
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
        fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    const std::vector<unsigned char> without = renderFrame(false);
    const std::vector<unsigned char> with = renderFrame(true);
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_probes: FAIL (GL error)\n");
        return 1;
    }
    CHECK(pc.grid.uploaded());
    CHECK(pc.grid.texture(0).id() != 0);

    long sumWithout = 0, sumWith = 0, diffCount = 0;
    for (std::size_t i = 0; i < without.size(); i += 4) {
        const long a = without[i] + without[i + 1] + without[i + 2];
        const long b = with[i] + with[i + 1] + with[i + 2];
        sumWithout += a;
        sumWith += b;
        if (std::labs(a - b) > 6) ++diffCount;
    }
    std::fprintf(stderr, "gl_smoke_probes: sum without=%ld with=%ld, diffPx=%ld/%d\n", sumWithout, sumWith,
                 diffCount, W * H);
    CHECK(diffCount > 500);                       // probes change the image
    CHECK(sumWith > sumWithout + W * H * 2);      // indirect light brightens shadowed areas

    std::fprintf(stderr, "gl_smoke_probes: PASS (GL %d.%d)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor);
    return fuhglgraphicstest::failures() ? 1 : 0;
}
