// Headless G4 smoke test (label "gl_context"): parses a generated .glb fixture with
// io::loadGltf, instantiates it into a fuhglgraphics::scene::Scene via io::instantiateGltf
// (GL meshes/materials + G9 node tree), renders it on a ground quad through the G3
// DeferredRenderer and verifies: lit pixels, per-item material IDs, the node world
// transform applied to draw items, and the parsed material preset fields.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include "gltf_fixture.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

constexpr int kSkip = 77;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_gltf", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_gltf: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_gltf: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- parse the generated .glb fixture ---
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "fgg-gl-smoke-gltf";
    fuhglgraphicstest::writeGltfFixture(dir);
    io::GltfAsset asset;
    std::string err;
    if (!io::loadGltf((dir / "quad.glb").string(), asset, &err)) {
        std::fprintf(stderr, "gl_smoke_gltf: loadGltf failed: %s\n", err.c_str());
        return 1;
    }
    if (asset.meshes.size() != 1 || asset.meshes[0].primitives.size() != 1 || asset.nodes.size() != 2) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (asset shape: meshes %zu nodes %zu)\n", asset.meshes.size(),
                     asset.nodes.size());
        return 1;
    }

    // --- instantiate into a G9 scene graph (needs the GL context) ---
    scene::Scene scene;
    io::GltfGpuResources gpu;
    if (!io::instantiateGltf(asset, scene, gpu, nullptr, &err)) {
        std::fprintf(stderr, "gl_smoke_gltf: instantiateGltf failed: %s\n", err.c_str());
        return 1;
    }
    if (gpu.meshes.size() != 1 || gpu.materials.size() != 1) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (gpu resources meshes %zu materials %zu)\n", gpu.meshes.size(),
                     gpu.materials.size());
        return 1;
    }
    // the parsed material preset must carry the glTF factors
    const fuhgl::render::Material& m = gpu.materials[0];
    if (!(m.builtinShader && std::fabs(m.albedo.x - 0.5f) < 1e-5 && std::fabs(m.metallic - 0.9f) < 1e-5 &&
          std::fabs(m.roughness - 0.3f) < 1e-5)) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (material preset fields)\n");
        return 1;
    }

    // --- add ground + sun + camera component, fill frame, render ---
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 8.f, {0.35f, 0.4f, 0.38f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.4f, 0.38f};
    ge.roughness = 0.9f;
    ge.castShadow = false;
    ge.materialId = 99;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 4.f, 9.f};
    cam.pitch = -0.35f;
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.3f, -1.f, -0.4f}, {1.f, 0.9f, 0.8f}, 3.f);
    scene.root().effects.enable(render::Effect::Tonemap);

    scene::FrameData frame;
    scene.fillFrame(frame, nullptr, 256, 256);
    // 2 items: ground + the instantiated quad; the quad node sits at world (2,3,4) * rot * scale
    if (frame.view.items.size() != 2 || frame.materialIds.size() != 2 || frame.castShadow.size() != 2) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (items %zu)\n", frame.view.items.size());
        return 1;
    }
    // the gltf quad is the item that uses the instantiated gpu material (ground was added after)
    std::size_t quadIdx = frame.view.items.size();
    for (std::size_t i = 0; i < frame.view.items.size(); ++i)
        if (frame.view.items[i].material == &gpu.materials[0]) quadIdx = i;
    if (quadIdx == frame.view.items.size()) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (gltf item does not use the gpu material)\n");
        return 1;
    }
    const fuhgl::render::DrawItem& quadItem = frame.view.items[quadIdx];
    // world matrix of the "quad" node = parent(translation 2,3,4) * child(rot 90deg Y, scale 2)
    CHECK_NEAR(quadItem.model.m[12], 2.f, 1e-4); // translation x
    CHECK_NEAR(quadItem.model.m[13], 3.f, 1e-4); // translation y
    CHECK_NEAR(quadItem.model.m[14], 4.f, 1e-4); // translation z
    if (frame.materialIds[quadIdx] == 0 || !frame.castShadow[quadIdx]) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (materialId/castShadow of the gltf item)\n");
        return 1;
    }

    render::DeferredRenderer renderer;
    if (!renderer.create(256, 256, 0)) {
        std::fprintf(stderr, "gl_smoke_gltf: renderer create failed\n");
        return 1;
    }
    frame.effects.applyTo(renderer);
    for (int i = 0; i < 4; ++i) renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
    fuhgl::gl::flush();

    renderer.output().bind();
    unsigned char center[4] = {0, 0, 0, 0};
    fuhgl::gl::readPixels(128, 128, 1, 1, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, center);
    std::fprintf(stderr, "gl_smoke_gltf: center=(%d,%d,%d,%d)\n", center[0], center[1], center[2], center[3]);
    if (!(center[3] == 255 && center[0] + center[1] + center[2] > 60)) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (center not lit)\n");
        return 1;
    }

    // material-ID attachment: the quad's running id at a pixel where the quad is
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0);
    fuhgl::gl::functions().glBindTexture(fuhgl::gl::GL_TEXTURE_2D, renderer.texMaterialId().id());
    std::vector<unsigned char> matId(256 * 256 * 4, 0);
    fuhgl::gl::getTexImage(fuhgl::gl::GL_TEXTURE_2D, 0, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, matId.data());
    const std::uint32_t id = render::unpackMaterialId(&matId[(128 * 256 + 128) * 4]);
    std::fprintf(stderr, "gl_smoke_gltf: center materialId=%u (ground=99)\n", id);
    if (id != 99u && id != frame.materialIds[quadIdx]) {
        std::fprintf(stderr, "gl_smoke_gltf: FAIL (unexpected material id %u)\n", id);
        return 1;
    }

    std::fprintf(stderr, "gl_smoke_gltf: PASS (GL %d.%d)\n", fuhgl::gl::version().major, fuhgl::gl::version().minor);
    return 0;
}
