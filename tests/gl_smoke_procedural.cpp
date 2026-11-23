// Headless GL smoke test for the G24/G25 procedural module (label "gl_context"): samples a
// G24-noise metaball volume into a VolumeGrid, extracts the surface with G25 marching cubes,
// uploads the MeshData into a render::Mesh, renders it through the G3 DeferredRenderer and
// verifies that the blob is visible (center pixel lit, differs from a mesh-less frame).
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kSkip = 77;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_procedural", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_procedural: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_procedural: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    const int W = 256, H = 256;
    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_procedural: renderer create failed\n");
        return 1;
    }

    // G24 noise volume -> G25 marching cubes blob
    const procedural::Noise noise(777u);
    const procedural::VolumeGrid grid = procedural::sampleField(
        [&](const fuhgl::math::Vec3& p) {
            return fuhgl::math::length(p) - 1.1f +
                   0.30f * noise.fbm3(p.x * 2.4f, p.y * 2.4f, p.z * 2.4f, 4);
        },
        {-1.6f, -1.6f, -1.6f}, {1.6f, 1.6f, 1.6f}, 40, 40, 40);
    const fuhgl::render::MeshData blob = procedural::marchingCubes(grid, 0.f, {0.9f, 0.55f, 0.2f});
    if (blob.indices.empty()) {
        std::fprintf(stderr, "gl_smoke_procedural: FAIL (empty marching-cubes mesh)\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_procedural: blob %zu vertices, %zu triangles\n", blob.vertexCount(),
                 blob.indices.size() / 3);

    fuhgl::render::Mesh blobMesh, ground;
    blobMesh.setMeshData(blob);
    fuhgl::render::shapes::quad(ground, 8.f, {0.35f, 0.4f, 0.38f});
    fuhgl::render::Material blobMat = fuhgl::render::materials::pbr({0.9f, 0.55f, 0.2f}, 0.15f, 0.45f);
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.4f, 0.45f, 0.4f}, 0.f, 0.9f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 0.6f, 4.2f};
    cam.pitch = -0.12f;

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.items.push_back({&blobMesh, &blobMat, fuhgl::math::translate({0.f, 1.1f, 0.f})});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.4f, -1.f, -0.3f}, {1.f, 0.9f, 0.75f}, 3.f));
    const std::vector<std::uint32_t> materialIds = {0u, 7u};

    auto readCenter = [&]() {
        renderer.output().bind();
        std::array<unsigned char, 4> px = {0, 0, 0, 0};
        fuhgl::gl::readPixels(W / 2, H / 2, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    for (int frame = 0; frame < 4; ++frame) renderer.render(scene, &materialIds);
    fuhgl::gl::flush();
    const auto withBlob = readCenter();
    std::fprintf(stderr, "gl_smoke_procedural: center with blob = (%d,%d,%d,%d)\n", withBlob[0], withBlob[1],
                 withBlob[2], withBlob[3]);

    // render without the blob -> the center must change (blob was visible)
    scene.items.pop_back();
    for (int frame = 0; frame < 4; ++frame) renderer.render(scene, &materialIds);
    fuhgl::gl::flush();
    const auto withoutBlob = readCenter();
    std::fprintf(stderr, "gl_smoke_procedural: center without blob = (%d,%d,%d,%d)\n", withoutBlob[0],
                 withoutBlob[1], withoutBlob[2], withoutBlob[3]);

    const bool lit = withBlob[3] == 255 && (withBlob[0] + withBlob[1] + withBlob[2]) > 60;
    const bool differs = std::abs(int(withBlob[0]) - int(withoutBlob[0])) +
                             std::abs(int(withBlob[1]) - int(withoutBlob[1])) +
                             std::abs(int(withBlob[2]) - int(withoutBlob[2])) > 10;
    if (!lit || !differs) {
        std::fprintf(stderr, "gl_smoke_procedural: FAIL (lit=%d differs=%d)\n", lit ? 1 : 0, differs ? 1 : 0);
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_procedural: OK\n");
    return 0;
}
