// G36 ice-cave screenshot tool: a procedural cave (G29 SDF -> G25 marching cubes rock with
// fbm strata colors) with a campfire burning INSIDE the cave (G35 realistic flame particles
// + additive embers + smoke), clusters of ice crystals (bipyramid meshes, metallic/low
// roughness + subsurface G33) that pick up G16 screen-space reflections, a still water
// puddle (G13 WaterComponent, planar reflection) on the cave floor, a cool entrance glow
// through the tunnel, HBAO (G31), bloom (G17), volumetric fog (G21), CSM + point shadows
// and ACES tonemapping. Renders headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_cave_screenshot --prefix cave [--shot 0|1|2] [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0] [--sim 2.5] [--msaa 0]
//          [--no-ssr] [--no-particles] [--no-shadows] [--no-water] [--no-volumetric]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>
#include <fuhglgraphics/procedural/sdf.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/math/noise.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using fuhgl::math::Vec3;

struct Args {
    std::string prefix = "cave";
    int shot = 0; // 0 = wide chamber, 1 = crystal close-up, 2 = tunnel toward the entrance
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f, sim = 2.5f;
    bool ssr = true, particles = true, shadows = true, water = true, volumetric = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) a.shot = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) a.msaa = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--sim") == 0 && i + 1 < argc) a.sim = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-ssr") == 0) a.ssr = false;
        else if (std::strcmp(argv[i], "--no-particles") == 0) a.particles = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-water") == 0) a.water = false;
        else if (std::strcmp(argv[i], "--no-volumetric") == 0) a.volumetric = false;
    }
    return a;
}

// --- cave layout constants (shared by the SDF, the placement and the cameras) ----------
constexpr Vec3 kChamberC{0.f, 0.f, -1.f};
constexpr float kChamberR = 3.9f;
constexpr float kFloorY = -3.3f;  // flat floor cut through the chamber sphere
constexpr Vec3 kFirePos{0.8f, -3.05f, 0.2f}; // flame emitter origin (on the log pile)

float floorY(float x, float z) { // cave floor height at (x, z): flat cut, sphere rim above
    const float d2 = x * x + (z - kChamberC.z) * (z - kChamberC.z);
    const float rim = kChamberR * kChamberR - d2;
    if (rim <= 0.f) return kFloorY - 5.f; // outside the chamber footprint: deep rock
    return std::max(kFloorY, -std::sqrt(rim) + kChamberC.y);
}
float ceilY(float x, float z) { // ceiling height at (x, z)
    const float d2 = x * x + (z - kChamberC.z) * (z - kChamberC.z);
    const float rim = kChamberR * kChamberR - d2;
    return rim <= 0.f ? 0.f : std::sqrt(rim) + kChamberC.y;
}

// The cave SDF (negative inside the hollow): chamber sphere with a flat floor cut,
// a round tunnel to the entrance (+Z) and a side pocket, inside a rounded rock block.
float caveField(const Vec3& p) {
    using namespace fuhglgraphics::procedural::sdf;
    const float rock = roundedBox(p, {8.f, 4.f, 9.f}, 1.2f);
    const float chamber = sphere(p - kChamberC, kChamberR);
    const float floorCut = -(p.y - kFloorY); // solid below the flat floor
    const Vec3 ta{0.f, -1.2f, 1.5f}, tb{0.f, -1.2f, 9.5f}; // tunnel axis
    const Vec3 pa = p - ta, ba = tb - ta;
    const float th = std::clamp(fuhgl::math::dot(pa, ba) / fuhgl::math::dot(ba, ba), 0.f, 1.f);
    const float tunnel = std::sqrt(fuhgl::math::dot(pa - ba * th, pa - ba * th)) - 2.0f;
    const float pocket = sphere(p - Vec3{-3.2f, -1.0f, -3.6f}, 1.7f);
    float hollow = smoothMin(chamber, tunnel, 1.1f);
    hollow = smoothMin(hollow, pocket, 0.9f);
    hollow = std::max(hollow, floorCut); // flat floor
    return std::max(rock, -hollow);
}

// Hexagonal bipyramid (ice crystal) as MeshData with flat face normals.
fuhgl::render::MeshData crystalData(float radius, float height, const Vec3& color) {
    fuhgl::render::MeshData md;
    const int kSides = 6;
    const float yTop = height * 0.72f, yBot = -height * 0.28f;
    auto pushTri = [&](Vec3 a, Vec3 b, Vec3 c) {
        Vec3 n = fuhgl::math::normalize(fuhgl::math::cross(b - a, c - a));
        const Vec3 cen = (a + b + c) * (1.f / 3.f);
        if (fuhgl::math::dot(n, cen) < 0.f) { n = -n; std::swap(b, c); } // outward, CCW
        const Vec3* vs[3] = {&a, &b, &c};
        for (const Vec3* v : vs) {
            md.vertices.insert(md.vertices.end(), {v->x, v->y, v->z, n.x, n.y, n.z, color.x, color.y,
                                                  color.z, 0.5f, 0.5f});
        }
        const unsigned base = static_cast<unsigned>(md.vertexCount()) - 3;
        md.indices.insert(md.indices.end(), {base, base + 1, base + 2});
    };
    for (int i = 0; i < kSides; ++i) {
        const float a0 = 2.f * fuhgl::math::kPi * i / kSides;
        const float a1 = 2.f * fuhgl::math::kPi * (i + 1) / kSides;
        const Vec3 v0{radius * std::cos(a0), 0.f, radius * std::sin(a0)};
        const Vec3 v1{radius * std::cos(a1), 0.f, radius * std::sin(a1)};
        const Vec3 top{0.f, yTop, 0.f}, bot{0.f, yBot, 0.f};
        pushTri(top, v0, v1);
        pushTri(bot, v1, v0);
    }
    return md;
}

// Dark blue cave sky cubemap with a soft cool glow at the tunnel entrance (+Z).
fuhgl::Texture makeCaveSky(int size) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const Vec3 zenith{0.012f, 0.035f, 0.09f}, horizon{0.10f, 0.17f, 0.30f}, ground{0.02f, 0.03f, 0.05f};
    const Vec3 entranceDir = fuhgl::math::normalize(Vec3{0.f, 0.06f, 1.f});
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const float u = 2.f * (i + 0.5f) / size - 1.f;
                const float v = 2.f * (j + 0.5f) / size - 1.f;
                Vec3 dir;
                switch (face) {
                    case 0: dir = {1.f, -v, -u}; break;
                    case 1: dir = {-1.f, -v, u}; break;
                    case 2: dir = {u, 1.f, v}; break;
                    case 3: dir = {u, -1.f, -v}; break;
                    case 4: dir = {u, -v, 1.f}; break;
                    default: dir = {-u, -v, -1.f}; break;
                }
                dir = fuhgl::math::normalize(dir);
                Vec3 col = fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                const float glow = std::pow(std::max(fuhgl::math::dot(dir, entranceDir), 0.f), 18.f);
                col += Vec3{0.45f, 0.62f, 0.95f} * glow * 1.4f; // moonlit entrance
                unsigned char* p = px.data() + (static_cast<std::size_t>(j) * size + i) * 4;
                p[0] = static_cast<unsigned char>(std::min(255.f, col.x * 255.f));
                p[1] = static_cast<unsigned char>(std::min(255.f, col.y * 255.f));
                p[2] = static_cast<unsigned char>(std::min(255.f, col.z * 255.f));
                p[3] = 255;
            }
        }
        cube.setImageCubeFace(order[face], 0, fuhgl::gl::GL_RGBA8, size, fuhgl::gl::GL_RGBA,
                              fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    }
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_R, fuhgl::gl::GL_CLAMP_TO_EDGE);
    return cube;
}

} // namespace

int main(int argc, char** argv) {
    using namespace fuhglgraphics;
    const Args args = parseArgs(argc, argv);
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_cave_screenshot", 64, 64)) {
        std::fprintf(stderr, "cave_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "cave_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- G36 cave rock: SDF -> marching cubes, vertex colors modulated by fbm strata -----
    fuhgl::render::MeshData caveData = procedural::meshFromSdf(caveField, {-8.f, -4.f, -8.f},
                                                               {8.f, 4.f, 10.f}, 120, {0.16f, 0.18f, 0.23f});
    for (std::size_t i = 0; i + 10 < caveData.vertices.size(); i += 11) {
        const float x = caveData.vertices[i], y = caveData.vertices[i + 1], z = caveData.vertices[i + 2];
        const float n = fuhgl::math::fbm3(x * 1.7f + 3.1f, y * 2.3f, z * 1.7f - 7.7f, 4, 11u);
        const float band = 0.55f + 0.25f * std::sin(y * 6.0f + n * 4.f); // sediment layers
        const float m = (0.62f + 0.55f * n) * band;
        caveData.vertices[i + 6] *= m;
        caveData.vertices[i + 7] *= m;
        caveData.vertices[i + 8] *= m;
    }
    fuhgl::render::Mesh caveMesh;
    caveMesh.setMeshData(caveData);
    std::fprintf(stderr, "cave_screenshot: cave mesh %zu verts / %zu tris\n", caveMesh.vertexCount(),
                 caveData.indices.size() / 3);

    // ice crystal + stalactite geometry (shared meshes, placed per node)
    fuhgl::render::Mesh crystalMesh, stalactiteMesh, logMesh;
    crystalMesh.setMeshData(crystalData(0.16f, 1.0f, {0.70f, 0.83f, 0.95f}));
    stalactiteMesh = fuhgl::render::makeCone(0.30f, 1.5f)->buildMesh(18, {0.66f, 0.80f, 0.94f},
                                                                     fuhgl::render::SolidUv::Cylindrical);
    fuhgl::render::shapes::box(logMesh, {0.55f, 0.09f, 0.09f}, {0.22f, 0.14f, 0.09f});

    // --- G9 scene graph ---
    scene::Scene scene;
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::PointShadows);
    scene.root().effects.enable(render::Effect::Ssr);
    scene.root().effects.enable(render::Effect::Hbao);
    scene.root().effects.enable(render::Effect::Bloom);
    scene.root().effects.enable(render::Effect::VolumetricFog);
    scene.root().effects.enable(render::Effect::Particles);
    scene.root().effects.enable(render::Effect::Water);
    scene.root().effects.params.exposure = 1.25f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.bloomThreshold = 1.1f;
    scene.root().effects.params.bloomStrength = 0.8f;
    scene.root().effects.params.ssrMaxSteps = 64;
    scene.root().effects.params.ssrStrength = 0.85f;
    scene.root().effects.params.ssrMaxDist = 14.f;
    scene.root().effects.params.hbaoRadius = 1.2f;
    scene.root().effects.params.hbaoDirections = 8;
    scene.root().effects.params.hbaoSteps = 4;
    scene.root().effects.params.volumetricDensity = 0.016f;
    scene.root().effects.params.volumetricCloudiness = 0.35f;
    scene.root().effects.params.volumetricHeightFalloff = 3.f;
    scene.root().effects.params.volumetricNoiseScale = 3.f;
    scene.root().effects.params.volumetricMaxDist = 25.f;
    scene.root().effects.params.volumetricStrength = 0.7f;
    scene.root().effects.params.volumetricSteps = 24;
    scene.root().effects.params.shadowCenter = {0.f, -1.5f, 0.f};
    scene.root().effects.params.shadowRadius = 8.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 25.f;

    // cave rock
    scene::Node& caveNode = scene.root().createChild("cave");
    scene::MeshEntry& ce = caveNode.attach<scene::MeshComponent>().addMesh(&caveMesh);
    ce.albedo = {1.f, 1.f, 1.f};
    ce.roughness = 0.92f;
    ce.metallic = 0.05f;
    ce.castShadow = false;
    ce.materialId = 1;

    // campfire: log pile + G35 flame + embers + smoke, all inside the chamber
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("log" + std::to_string(i));
        n.translation = {kFirePos.x, kFloorY + 0.10f + 0.18f * i, kFirePos.z};
        n.rotation = fuhgl::math::fromAxisAngle(i * 1.15f + 0.3f, {0.f, 1.f, 0.f});
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&logMesh);
        e.albedo = {0.24f, 0.15f, 0.08f};
        e.roughness = 1.f;
        e.castShadow = true;
        e.materialId = 10 + i;
    }
    { // flame: teardrop billboards with fbm flicker (G35)
        scene::Node& n = scene.root().createChild("fire");
        auto& em = n.attach<scene::ParticleEmitterComponent>();
        em.config.position = kFirePos;
        em.config.direction = {0.f, 1.f, 0.f};
        em.config.speed = 0.5f;
        em.config.spread = 0.22f;
        em.config.rate = 170.f;
        em.config.lifetime = 0.95f;
        em.config.size = 0.50f;
        em.config.grow = 0.18f;
        em.config.color = {1.0f, 0.72f, 0.30f};
        em.config.alpha = 0.6f;
        em.config.gravity = 0.45f;
        em.config.drag = 1.2f;
        em.config.blend = particles::BlendMode::Additive;
        em.config.flame = true;
        if (!em.system.create(8192, em.config)) {
            std::fprintf(stderr, "cave_screenshot: flame emitter create failed\n");
            return 1;
        }
    }
    { // embers: few fast additive sparks
        scene::Node& n = scene.root().createChild("embers");
        auto& em = n.attach<scene::ParticleEmitterComponent>();
        em.config.position = {kFirePos.x, kFirePos.y + 0.08f, kFirePos.z};
        em.config.direction = {0.f, 1.f, 0.f};
        em.config.speed = 1.9f;
        em.config.spread = 0.5f;
        em.config.rate = 40.f;
        em.config.lifetime = 1.1f;
        em.config.size = 0.04f;
        em.config.grow = -0.01f;
        em.config.color = {1.0f, 0.55f, 0.12f};
        em.config.alpha = 1.0f;
        em.config.gravity = 1.0f;
        em.config.drag = 0.8f;
        em.config.blend = particles::BlendMode::Additive;
        if (!em.system.create(2048, em.config)) {
            std::fprintf(stderr, "cave_screenshot: ember emitter create failed\n");
            return 1;
        }
    }
    { // smoke: dark, slow, alpha-blended plume drifting to the ceiling
        scene::Node& n = scene.root().createChild("smoke");
        auto& em = n.attach<scene::ParticleEmitterComponent>();
        em.config.position = {kFirePos.x, kFirePos.y + 0.55f, kFirePos.z};
        em.config.direction = {0.f, 1.f, 0.f};
        em.config.speed = 0.45f;
        em.config.spread = 0.3f;
        em.config.rate = 50.f;
        em.config.lifetime = 3.0f;
        em.config.size = 0.3f;
        em.config.grow = 0.8f;
        em.config.color = {0.06f, 0.07f, 0.09f};
        em.config.alpha = 0.30f;
        em.config.gravity = 0.3f;
        em.config.drag = 0.7f;
        em.config.blend = particles::BlendMode::Alpha;
        if (!em.system.create(4096, em.config)) {
            std::fprintf(stderr, "cave_screenshot: smoke emitter create failed\n");
            return 1;
        }
    }

    // ice crystal clusters on the floor (ring around the fire + a few in the tunnel)
    std::mt19937 rng(23u);
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    std::uniform_real_distribution<float> ang(0.f, 2.f * fuhgl::math::kPi);
    std::uniform_real_distribution<float> rad(0.7f, 2.0f);
    int placed = 0;
    for (int i = 0; i < 200 && placed < 22; ++i) {
        const float a = ang(rng), r = rad(rng);
        const float x = kFirePos.x + std::cos(a) * r, z = kFirePos.z + std::sin(a) * r * 0.8f;
        if (x * x + (z - kChamberC.z) * (z - kChamberC.z) > 3.1f * 3.1f) continue; // stay in the chamber
        const float y = floorY(x, z);
        scene::Node& n = scene.root().createChild("crystal" + std::to_string(placed));
        n.translation = {x, y + 0.02f, z};
        const float s = 0.35f + uni(rng) * 1.1f;
        n.scale = {s * (0.7f + 0.6f * uni(rng)), s * (0.9f + 0.9f * uni(rng)), s * (0.7f + 0.6f * uni(rng))};
        n.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&crystalMesh);
        e.albedo = {0.72f, 0.84f, 0.96f};
        e.metallic = 0.85f;
        e.roughness = 0.07f;
        e.emissive = {0.05f, 0.13f, 0.18f}; // faint inner glow
        e.subsurface = 0.7f;                 // G33: ice is translucent
        e.castShadow = true;
        e.materialId = 40;
        ++placed;
    }
    // a few crystals inside the entrance tunnel
    for (int i = 0; i < 5; ++i) {
        const float z = 3.5f + 1.2f * i;
        const float x = (uni(rng) - 0.5f) * 2.4f;
        scene::Node& n = scene.root().createChild("tcrystal" + std::to_string(i));
        n.translation = {x, kFloorY + 0.02f, z};
        const float s = 0.4f + uni(rng) * 0.8f;
        n.scale = {s, s * 1.4f, s};
        n.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 6.28f, {0.f, 1.f, 0.f});
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&crystalMesh);
        e.albedo = {0.72f, 0.84f, 0.96f};
        e.metallic = 0.85f;
        e.roughness = 0.08f;
        e.emissive = {0.04f, 0.11f, 0.16f};
        e.subsurface = 0.7f;
        e.castShadow = true;
        e.materialId = 41;
    }
    // ice stalactites hanging from the ceiling
    for (int i = 0; i < 7; ++i) {
        const float a = ang(rng), r = 0.8f + 2.2f * uni(rng);
        const float x = std::cos(a) * r, z = kChamberC.z + std::sin(a) * r;
        const float cy = ceilY(x, z);
        if (cy < 1.2f) continue;
        const float len = 0.5f + 0.9f * uni(rng);
        scene::Node& n = scene.root().createChild("stalactite" + std::to_string(i));
        n.translation = {x, cy - 0.02f, z};
        n.scale = {1.f, len / 1.5f, 1.f};
        n.rotation = fuhgl::math::fromAxisAngle(fuhgl::math::kPi, {1.f, 0.f, 0.f}); // cone tip down
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&stalactiteMesh);
        e.albedo = {0.70f, 0.82f, 0.95f};
        e.metallic = 0.8f;
        e.roughness = 0.1f;
        e.emissive = {0.03f, 0.09f, 0.13f};
        e.subsurface = 0.6f;
        e.castShadow = true;
        e.materialId = 42;
    }

    // water puddle on the cave floor (planar reflection of fire + crystals)
    if (args.water) {
        scene::Node& wn = scene.root().createChild("puddle");
        wn.translation = {0.f, kFloorY + 0.08f, -1.f};
        scene::WaterComponent& wc = wn.attach<scene::WaterComponent>();
        if (!wc.surface.create(96, 9.f)) {
            std::fprintf(stderr, "cave_screenshot: water create failed\n");
            return 1;
        }
        wc.surface.waves = {
            {{1.f, 0.2f}, 0.008f, 5.f, 1.2f, 0.5f},
            {{-0.6f, 0.8f}, 0.006f, 3.f, 0.9f, 0.4f},
        };
        wc.surface.reflectionStrength = 0.95f;
        wc.surface.foamStrength = 0.f;
        wc.surface.deepColor = {0.008f, 0.03f, 0.06f};
        wc.surface.shallowColor = {0.04f, 0.10f, 0.16f};
    }

    // lights: warm campfire point light (gets the cube shadow map, F46), cool entrance
    // point light in the tunnel, faint blue "moonlight" directional through the entrance
    // (LightComponent::fill transforms light.position by the node world matrix, so the
    //  light position stays local {0,0,0} and the node translation places it in the world)
    scene::Node& fireLight = scene.root().createChild("fireLight");
    fireLight.translation = {kFirePos.x, kFirePos.y + 0.9f, kFirePos.z};
    fireLight.attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({0.f, 0.f, 0.f}, {1.0f, 0.52f, 0.22f}, 140.f, 18.f);
    scene::Node& entranceLight = scene.root().createChild("entranceLight");
    entranceLight.translation = {0.f, -0.8f, 8.f};
    entranceLight.attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({0.f, 0.f, 0.f}, {0.45f, 0.65f, 1.0f}, 40.f, 14.f);
    const Vec3 entranceSunDir = fuhgl::math::normalize(Vec3{0.f, 0.18f, 1.f});
    scene::Node& moon = scene.root().createChild("moon");
    moon.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-entranceSunDir, {0.35f, 0.50f, 0.85f}, 0.9f);

    // camera presets
    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(55.f);
    Vec3 camPos, camTarget;
    switch (args.shot) {
        case 1: camPos = {1.8f, -2.0f, 1.6f}; camTarget = {-1.2f, -3.2f, -2.2f}; break; // crystals
        case 2: camPos = {-2.8f, -1.0f, -3.2f}; camTarget = {0.6f, -0.6f, 8.0f}; break; // entrance
        default: camPos = {-2.0f, 0.4f, 1.8f}; camTarget = {0.8f, -2.8f, 0.2f}; break; // wide: across the chamber at the fire
    }
    cam.position = camPos;
    {
        const Vec3 d = fuhgl::math::normalize(camTarget - camPos);
        cam.yaw = std::atan2(d.x, -d.z);
        cam.pitch = std::asin(d.y);
    }
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // pre-simulate the fire so the first frame already has a developed flame
    for (auto& c : scene.root().children())
        if (auto* em = c->find<scene::ParticleEmitterComponent>())
            for (int i = 0; i < static_cast<int>(args.sim * 60.f); ++i) em->system.update(1.f / 60.f);

    // --- sky + IBL environment (dark blue, entrance glow baked in) ---
    fuhgl::Texture caveSky = makeCaveSky(128);
    fuhgl::render::Environment env;
    if (caveSky.id() != 0 && !env.createFromCubemap(caveSky, 16, 32, 64))
        std::fprintf(stderr, "cave_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.35f;

    // --- renderer ---
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "cave_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    renderer.setWaterReflectionScale(0.5f);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, -1.5f, 0.f}, 8.f);
        renderer.setShadowZRange(0.5f, 25.f);
    }
    if (caveSky.id() != 0) renderer.setSkyCubemap(&caveSky);
    renderer.skyTop = {0.012f, 0.035f, 0.09f};
    renderer.skyBottom = {0.10f, 0.17f, 0.30f};

    const char* shotName = args.shot == 1 ? "crystals" : (args.shot == 2 ? "entrance" : "wide");
    const std::string out = args.prefix + "_" + shotName + ".png";

    const float dt = 1.f / 60.f;
    for (int i = 0; i < args.frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        if (!args.shadows) {
            frame.effects.disable(render::Effect::Shadows);
            frame.effects.disable(render::Effect::CsmShadows);
            frame.effects.disable(render::Effect::PointShadows);
        }
        if (!args.ssr) frame.effects.disable(render::Effect::Ssr);
        if (!args.particles) frame.effects.disable(render::Effect::Particles);
        if (!args.water) frame.effects.disable(render::Effect::Water);
        if (!args.volumetric) frame.effects.disable(render::Effect::VolumetricFog);
        frame.effects.applyTo(renderer);
        frame.view.env = &env;
        frame.view.fogColor = {0.03f, 0.05f, 0.10f};
        frame.view.fogDensity = 0.008f;
        for (auto& c : scene.root().children())
            if (auto* em = c->find<scene::ParticleEmitterComponent>()) em->system.update(dt);
        if (args.water)
            if (auto* wc = scene.root().find<scene::WaterComponent>()) wc->surface.update(dt);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "cave_screenshot: failed to write %s\n", out.c_str());
        return 1;
    }
    std::fprintf(stderr, "cave_screenshot: wrote %s (%dx%d, %d frames, shot=%s, GL %d.%d %s)\n",
                 out.c_str(), args.width, args.height, args.frames, shotName, fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
