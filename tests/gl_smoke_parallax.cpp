// Headless parallax-mapping (POM) smoke test (label "gl_context"): a vertical brick wall
// (procedural albedo texture + height map with recessed mortar grooves) viewed at a grazing
// angle is rendered twice \u2014 once with MeshEntry::heightMap set (POM on) and once without
// (POM off). The frames must differ measurably (the grooves shift at grazing angles), both
// must be non-black, and no GL error may be left over. Skips cleanly with exit code 77
// without GL >= 3.3.

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
constexpr int kTex = 128;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_parallax", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_parallax: no GL context \u2014 SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_parallax: context below GL 3.3 \u2014 SKIP\n");
        return false;
    }
    return true;
}

unsigned hash2(int x, int y) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Brick pattern shared by the albedo and the height texture: kCols x kRows bricks per
// texture tile, offset rows by half a brick, recessed mortar grooves.
constexpr int kCols = 4, kRows = 3;
struct BrickInfo {
    bool mortar;
    float height; // 0..1: 1 = brick face, ~0.1 = groove
    fuhgl::math::Vec3 albedo;
};
BrickInfo brickAt(float u, float v, float pxNoise) {
    const float bx = u * kCols, by = v * kRows;
    const int row = static_cast<int>(by);
    const float fx = bx - 0.5f * (row % 2);
    const int col = static_cast<int>(std::floor(fx));
    const float lx = fx - std::floor(fx), ly = by - row;
    const float m = 0.09f; // mortar thickness in brick-local units
    BrickInfo b{};
    if (lx < m || lx > 1.f - m || ly < m || ly > 1.f - m) {
        b.mortar = true;
        b.height = 0.1f;
        b.albedo = {0.52f, 0.50f, 0.47f};
    } else {
        b.mortar = false;
        const unsigned h = hash2(col, row);
        const float tint = 0.75f + 0.25f * ((h & 0xffu) / 255.f);
        b.albedo = {0.62f * tint, 0.28f * tint, 0.20f * tint};
        b.albedo.x *= 0.9f + 0.2f * pxNoise;
        b.height = 0.85f + 0.15f * (((h >> 8) & 0xffu) / 255.f);
    }
    return b;
}

fuhgl::Texture makeBrickAlbedo() {
    std::vector<unsigned char> data(static_cast<std::size_t>(kTex) * kTex * 4, 0);
    for (int y = 0; y < kTex; ++y)
        for (int x = 0; x < kTex; ++x) {
            const float u = static_cast<float>(x) / kTex, v = static_cast<float>(y) / kTex;
            const float n = ((hash2(x, y) & 0xffu) / 255.f - 0.5f) * 0.12f;
            const BrickInfo b = brickAt(u, v, n + 0.5f);
            unsigned char* o = &data[(static_cast<std::size_t>(y) * kTex + x) * 4];
            o[0] = static_cast<unsigned char>(std::min(1.f, b.albedo.x + n) * 255.f);
            o[1] = static_cast<unsigned char>(std::min(1.f, b.albedo.y + n) * 255.f);
            o[2] = static_cast<unsigned char>(std::min(1.f, b.albedo.z + n) * 255.f);
            o[3] = 255;
        }
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, kTex, kTex, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 data.data());
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_REPEAT); // POM offset UVs must wrap
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_REPEAT);
    return t;
}

fuhgl::Texture makeBrickHeight() {
    std::vector<unsigned char> data(static_cast<std::size_t>(kTex) * kTex * 4, 0);
    for (int y = 0; y < kTex; ++y)
        for (int x = 0; x < kTex; ++x) {
            const float u = static_cast<float>(x) / kTex, v = static_cast<float>(y) / kTex;
            const float h = brickAt(u, v, 0.f).height;
            unsigned char* o = &data[(static_cast<std::size_t>(y) * kTex + x) * 4];
            const unsigned char hv = static_cast<unsigned char>(h * 255.f);
            o[0] = hv;
            o[1] = hv;
            o[2] = hv;
            o[3] = 255;
        }
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, kTex, kTex, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 data.data());
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_REPEAT);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_REPEAT);
    return t;
}

std::vector<unsigned char> renderFrame(fuhglgraphics::scene::Scene& scene,
                                       fuhgl::FirstPersonCamera& cam,
                                       fuhglgraphics::render::DeferredRenderer& renderer) {
    using namespace fuhglgraphics;
    for (int i = 0; i < 4; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, &cam, W, H);
        frame.effects.enable(render::Effect::Tonemap);
        frame.effects.applyTo(renderer);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    renderer.output().bind();
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
    fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    fuhgl::gl::flush();
    return px;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    fuhgl::Texture albedoTex = makeBrickAlbedo();
    fuhgl::Texture heightTex = makeBrickHeight();

    scene::Scene scene;
    fuhgl::render::Mesh wall, ground;
    fuhgl::render::shapes::quad(wall, 3.f, {1, 1, 1});   // vertical XY plane, normal +Z
    fuhgl::render::shapes::quad(ground, 20.f, {0.35f, 0.42f, 0.3f});

    scene::Node& wallNode = scene.root().createChild("brickWall");
    wallNode.translation = {0.f, 3.f, 0.f};
    scene::MeshEntry& we = wallNode.attach<scene::MeshComponent>().addMesh(&wall);
    we.albedo = {1.f, 1.f, 1.f};
    we.albedoTexture = &albedoTex;
    we.heightMap = &heightTex;      // POM on (toggled below)
    we.parallaxScale = 0.09f;       // deep grooves -> clearly visible shift at grazing angles
    we.parallaxLayers = 24;
    we.roughness = 0.8f;
    we.castShadow = false;
    we.materialId = 2;

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.42f, 0.3f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.25f, -0.85f, -0.45f}, {1.f, 0.95f, 0.85f}, 2.5f);

    // grazing view along the wall (view dir nearly parallel to the wall plane)
    fuhgl::FirstPersonCamera cam;
    cam.position = {5.2f, 3.1f, 3.4f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{-0.5f, 2.9f, 0.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false); // deterministic frames
    renderer.skyTop = {0.2f, 0.4f, 0.75f};
    renderer.skyBottom = {0.9f, 0.85f, 0.72f};

    we.heightMap = &heightTex; // POM on
    const auto on = renderFrame(scene, cam, renderer);
    we.heightMap = nullptr;    // POM off
    const auto off = renderFrame(scene, cam, renderer);

    long sumOn = 0, sumOff = 0, diff = 0;
    for (std::size_t i = 0; i < on.size(); i += 4) {
        sumOn += on[i] + on[i + 1] + on[i + 2];
        sumOff += off[i] + off[i + 1] + off[i + 2];
        diff += std::labs((long)on[i] - (long)off[i]) + std::labs((long)on[i + 1] - (long)off[i + 1]) +
                std::labs((long)on[i + 2] - (long)off[i + 2]);
    }
    std::fprintf(stderr, "gl_smoke_parallax: sumOn=%ld sumOff=%ld |diff|=%ld\n", sumOn, sumOff, diff);
    if (sumOn < 1000 || sumOff < 1000) {
        std::fprintf(stderr, "gl_smoke_parallax: frame too dark\n");
        return 1;
    }
    if (diff < 2000) {
        std::fprintf(stderr, "gl_smoke_parallax: POM on/off frames nearly identical (|diff|=%ld)\n", diff);
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_parallax: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_parallax: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
