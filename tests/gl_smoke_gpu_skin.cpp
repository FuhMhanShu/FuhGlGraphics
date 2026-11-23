// Headless G14 smoke test (label "gl_context"): builds a small skinned ribbon (2 joints),
// creates an anim::GpuSkinner (GL >= 4.3 SSBO palette), renders two frames through the G3
// DeferredRenderer with different palettes and Effect::GpuSkinning ON — the images must
// differ (the GPU skinning deforms the mesh). With the effect OFF both frames must be
// identical (the plain bind-pose mesh is drawn — the CPU-skinning fallback path).
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

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
    if (!window.create("gl_smoke_gpu_skin", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: context below GL 3.3 — SKIP\n");
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

long regionDiff(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    long sum = 0;
    for (std::size_t i = 0; i < a.size(); i += 4)
        sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) +
               std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1])) +
               std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2]));
    return sum;
}

// ribbon quad in the XY plane: 4 vertices, 6 indices, standard 11-floats layout
fuhgl::render::MeshData ribbonData() {
    fuhgl::render::MeshData md;
    const float xs[2] = {0.f, 2.f};
    for (int i = 0; i < 2; ++i)
        for (float y : {-0.5f, 0.5f}) {
            md.vertices.push_back(xs[i]); // pos
            md.vertices.push_back(y);
            md.vertices.push_back(0.f);
            md.vertices.push_back(0.f); // normal
            md.vertices.push_back(0.f);
            md.vertices.push_back(1.f);
            md.vertices.push_back(0.8f); // color
            md.vertices.push_back(0.3f);
            md.vertices.push_back(0.2f);
            md.vertices.push_back(static_cast<float>(i)); // uv
            md.vertices.push_back(y + 0.5f);
        }
    md.indices = {1, 0, 2, 1, 2, 3}; // CCW against +Z (the camera looks from +Z)
    return md;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // capability gate: GpuSkinning needs GL >= 4.3
    if (!render::effectSupported(render::Effect::GpuSkinning, fuhgl::gl::version().major,
                                 fuhgl::gl::version().minor)) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: GL < 4.3 — GpuSkinning unsupported, SKIP\n");
        return kSkip;
    }
    if (!anim::GpuSkinner::supported()) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: GpuSkinner::supported() false on GL %d.%d — SKIP\n",
                     fuhgl::gl::version().major, fuhgl::gl::version().minor);
        return kSkip;
    }

    const fuhgl::render::MeshData base = ribbonData();
    anim::SkinWeights skin;
    // vertices 0,1 -> joint 0; vertices 2,3 -> joint 1
    skin.joints = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
    skin.weights = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

    fuhgl::render::Mesh mesh;
    if (!mesh.create()) return 1;
    mesh.setMeshData(base);

    anim::GpuSkinner skinner;
    if (!skinner.create(base, skin)) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: GpuSkinner::create failed\n");
        return 1;
    }
    if (skinner.vertexCount() != 4 || skinner.indexCount() != 6 || skinner.jointCount() != 2) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: unexpected skinner counts %d/%d/%d\n",
                     skinner.vertexCount(), skinner.indexCount(), skinner.jointCount());
        return 1;
    }

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);
    renderer.setEffectEnabled(render::Effect::GpuSkinning, true);
    if (!renderer.effectEnabled(render::Effect::GpuSkinning)) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: Effect::GpuSkinning not enabled on GL %d.%d\n",
                     fuhgl::gl::version().major, fuhgl::gl::version().minor);
        return 1;
    }

    fuhgl::render::Material mat = fuhgl::render::materials::pbr({0.8f, 0.3f, 0.2f}, 0.1f, 0.6f);
    fuhgl::FirstPersonCamera cam;
    cam.position = {1.f, 0.5f, 5.f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{1.f, 0.f, 0.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&mesh, &mat, fuhgl::math::Mat4{}});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.3f, -0.8f, -0.4f}, {1.f, 0.95f, 0.9f}, 3.f));
    std::vector<std::uint32_t> materialIds = {1u};
    std::vector<bool> castShadow = {false};
    std::vector<const anim::GpuSkinner*> skins = {&skinner};

    // palette A: identity; palette B: joint 1 rotated 90° around Z
    std::vector<fuhgl::math::Mat4> palA(2, fuhgl::math::Mat4{});
    std::vector<fuhgl::math::Mat4> palB = palA;
    palB[1] = fuhgl::math::rotateZ(fuhgl::math::kPi * 0.5f);

    skinner.uploadPalette(palA);
    renderer.render(scene, &materialIds, &castShadow, nullptr, nullptr, nullptr, nullptr, &skins);
    const std::vector<unsigned char> frameA = readOutput(renderer, W, H);

    skinner.uploadPalette(palB);
    renderer.render(scene, &materialIds, &castShadow, nullptr, nullptr, nullptr, nullptr, &skins);
    const std::vector<unsigned char> frameB = readOutput(renderer, W, H);

    const long diffSkin = regionDiff(frameA, frameB);
    std::fprintf(stderr, "gl_smoke_gpu_skin: diff(gpu-skinned pose A vs B) = %ld\n", diffSkin);
    if (diffSkin < 500) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: GPU-skinned frames identical — palette not applied?\n");
        return 1;
    }

    // effect OFF: the plain bind-pose mesh is drawn — both palettes must give identical frames
    renderer.setEffectEnabled(render::Effect::GpuSkinning, false);
    skinner.uploadPalette(palA);
    renderer.render(scene, &materialIds, &castShadow, nullptr, nullptr, nullptr, nullptr, &skins);
    const std::vector<unsigned char> frameOffA = readOutput(renderer, W, H);
    skinner.uploadPalette(palB);
    renderer.render(scene, &materialIds, &castShadow, nullptr, nullptr, nullptr, nullptr, &skins);
    const std::vector<unsigned char> frameOffB = readOutput(renderer, W, H);
    const long diffOff = regionDiff(frameOffA, frameOffB);
    std::fprintf(stderr, "gl_smoke_gpu_skin: diff(effect off, A vs B) = %ld\n", diffOff);
    if (diffOff > 50) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: effect-off frames differ — fallback path broken?\n");
        return 1;
    }
    // and the fallback frame must differ from the skinned frame B (deformation visible)
    if (regionDiff(frameOffA, frameB) < 500) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: skinned vs fallback frames too similar\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_gpu_skin: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_gpu_skin: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
