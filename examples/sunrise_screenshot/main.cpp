// Sunrise showcase: one landscape scene with as many G3/G9 features as possible at once —
// G20 terrain+LOD (carved lake basin), G13 Gerstner water (reflection/refraction/foam/glint),
// trees/rocks/reeds/lily pads (G9 nodes), a wooden dock with a waving flag (G6 animation),
// a wear decal on the dock (G8), pollen + lake-mist particles (G5), sunrise sky cubemap + IBL,
// low warm sun with CSM shadows (long sunrise shadows), god rays, SSAO (G15), SSR (G16),
// bloom on the sun disk (G17), morning-dew wetness with SSR boost (G22), volumetric mist
// (G21), TAA + FSR-style upscale + ACES tonemap (G23). Renders two shots (wide across the
// lake toward the sun / dock close-up) headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_sunrise_screenshot --prefix sunrise [--width 1280] [--height 720]
//          [--frames 12] [--scale 0.72]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/math/noise.hpp>
#include <fuhgl/render.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string prefix = "sunrise";
    int width = 1280, height = 720, frames = 12;
    float scale = 0.72f;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
    }
    return a;
}

float smoothstep01(float e0, float e1, float x) {
    const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
    return t * t * (3.f - 2.f * t);
}

fuhgl::math::Vec3 cubeDir(int face, int i, int j, int size) {
    const float u = 2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(size) - 1.f;
    const float v = 2.f * (static_cast<float>(j) + 0.5f) / static_cast<float>(size) - 1.f;
    fuhgl::math::Vec3 d;
    switch (face) {
        case 0: d = {1.f, -v, -u}; break;
        case 1: d = {-1.f, -v, u}; break;
        case 2: d = {u, 1.f, v}; break;
        case 3: d = {u, -1.f, -v}; break;
        case 4: d = {u, -v, 1.f}; break;
        default: d = {-u, -v, -1.f}; break;
    }
    return fuhgl::math::normalize(d);
}

// Sunrise sky cubemap: deep-blue zenith -> violet mid -> hot orange at the sun azimuth,
// pink cirrus streaks, strong sun glow. LDR (RGBA8) — the HDR sun disk comes from the
// lighting pass (and blooms).
fuhgl::Texture makeSunriseSky(int size, unsigned seed, const fuhgl::math::Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.045f, 0.085f, 0.27f};
    const fuhgl::math::Vec3 horizonAway{0.42f, 0.22f, 0.36f}; // violet-pink away from the sun
    const fuhgl::math::Vec3 horizonSun{1.0f, 0.40f, 0.10f};   // hot orange at the sun
    const fuhgl::math::Vec3 ground{0.07f, 0.045f, 0.075f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float sunAmt = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 6.f);
                const fuhgl::math::Vec3 horizon = fuhgl::math::lerp(horizonAway, horizonSun, sunAmt);
                fuhgl::math::Vec3 col =
                    fuhgl::math::lerp(horizon, zenith, std::pow(smoothstep01(-0.05f, 0.6f, dir.y), 0.8f));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 4.f));
                // cirrus streaks, pinker near the sun
                const float n = fuhgl::math::fbm3(dir.x * 4.f + 3.1f, dir.y * 9.f, dir.z * 4.f, 4, seed);
                const float cl = smoothstep01(0.52f, 0.78f, n) * smoothstep01(-0.02f, 0.22f, dir.y);
                col += fuhgl::math::Vec3{1.0f, 0.45f, 0.32f} * (cl * (0.35f + 0.65f * sunAmt));
                // sun glow
                col += fuhgl::math::Vec3{1.0f, 0.55f, 0.22f} * std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 300.f) * 8.f;
                col += fuhgl::math::Vec3{1.0f, 0.5f, 0.25f} * std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 24.f) * 0.7f;
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
    return cube;
}

// Wear/dirt texture for the dock decal (G8): brown noise streaks with alpha.
fuhgl::Texture makeWearTexture(int size, unsigned seed) {
    fuhgl::Texture t;
    if (!t.create(fuhgl::Texture::Target::Tex2D)) return t;
    t.bind();
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const float u = static_cast<float>(i) / size, v = static_cast<float>(j) / size;
            const float grain = fuhgl::math::fbm3(u * 9.f, v * 9.f, 0.5f, 4, seed);
            const float streak = smoothstep01(0.42f, 0.62f, fuhgl::math::fbm3(u * 3.f, v * 24.f, 7.7f, 3, seed + 1));
            const float a = streak * (0.55f + 0.45f * grain);
            unsigned char* p = px.data() + (static_cast<std::size_t>(j) * size + i) * 4;
            p[0] = static_cast<unsigned char>(0.38f * 255.f * (0.6f + 0.4f * grain));
            p[1] = static_cast<unsigned char>(0.30f * 255.f * (0.6f + 0.4f * grain));
            p[2] = static_cast<unsigned char>(0.21f * 255.f * (0.6f + 0.4f * grain));
            p[3] = static_cast<unsigned char>(std::min(255.f, a * 255.f));
        }
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, size, size, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 px.data());
    return t;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
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
    if (!window.create("fuhglgraphics_sunrise_screenshot", 64, 64)) {
        std::fprintf(stderr, "sunrise_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "sunrise_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- layout constants ---
    const fuhgl::math::Vec2 basin{20.f, -50.f}; // lake basin center (world xz)
    const float basinR = 58.f;
    const float waterLevel = 2.2f;
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.30f, 0.10f, -0.95f});

    // --- G20 terrain with LOD, basin carved into the heightmap ---
    scene::Scene scene;
    scene::Node& terrainNode = scene.root().createChild("terrain");
    scene::TerrainComponent& tc = terrainNode.attach<scene::TerrainComponent>();
    tc.levels = 4;
    tc.fovYDeg = 55.f;
    tc.seed = 11u;
    if (!tc.create(200, 200, 360.f, 360.f, 26.f)) {
        std::fprintf(stderr, "sunrise_screenshot: terrain create failed\n");
        return 1;
    }
    { // carve the lake basin (deep inside, shore ring above the water line)
        for (int j = 0; j < tc.gridH; ++j)
            for (int i = 0; i < tc.gridW; ++i) {
                const float x = (static_cast<float>(i) / (tc.gridW - 1) - 0.5f) * tc.sizeX;
                const float z = (static_cast<float>(j) / (tc.gridH - 1) - 0.5f) * tc.sizeZ;
                const float d = std::hypot(x - basin.x, z - basin.y);
                const float t = smoothstep01(0.8f * basinR, basinR, d);
                float& h = tc.heights[static_cast<std::size_t>(j) * tc.gridW + i];
                h = 0.05f + (std::max(h, 0.12f) - 0.05f) * t;
            }
        if (!tc.rebuildLod()) {
            std::fprintf(stderr, "sunrise_screenshot: terrain rebuildLod failed\n");
            return 1;
        }
    }
    tc.albedo = {0.28f, 0.44f, 0.17f};
    tc.roughness = 0.95f;
    tc.materialId = 1;

    // --- G13 water surface in the basin ---
    scene::Node& waterNode = scene.root().createChild("water");
    waterNode.translation = {0.f, waterLevel, 0.f};
    scene::WaterComponent& wc = waterNode.attach<scene::WaterComponent>();
    if (!wc.surface.create(180, 320.f)) {
        std::fprintf(stderr, "sunrise_screenshot: water create failed\n");
        return 1;
    }
    wc.surface.waves = {
        {{1.f, 0.12f}, 0.16f, 14.f, 2.0f, 0.7f},
        {{0.7f, 0.7f}, 0.10f, 7.f, 1.4f, 0.5f},
        {{-0.5f, 0.86f}, 0.06f, 3.5f, 1.0f, 0.4f},
    };
    wc.surface.reflectionStrength = 0.95f;
    wc.surface.foamStrength = 1.1f;
    wc.surface.shoreFoamWidth = 1.6f;
    wc.surface.deepColor = {0.008f, 0.02f, 0.07f};
    wc.surface.shallowColor = {0.10f, 0.22f, 0.24f};

    // --- shared meshes ---
    fuhgl::render::Mesh trunk, foliage, foliage2, reed, plank, post, pole, flagMesh, pad;
    fuhgl::render::shapes::box(trunk, {0.16f, 1.1f, 0.16f}, {1, 1, 1});
    foliage = fuhgl::render::makeSphere(1.25f)->buildMesh(9, {1, 1, 1}, fuhgl::render::SolidUv::Spherical);
    foliage2 = fuhgl::render::makeSphere(0.95f)->buildMesh(8, {1, 1, 1}, fuhgl::render::SolidUv::Spherical);
    fuhgl::render::shapes::box(reed, {0.05f, 1.4f, 0.05f}, {1, 1, 1});
    fuhgl::render::shapes::box(plank, {1.3f, 0.07f, 0.55f}, {1, 1, 1});
    fuhgl::render::shapes::box(post, {0.12f, 0.8f, 0.12f}, {1, 1, 1});
    fuhgl::render::shapes::box(pole, {0.06f, 2.6f, 0.06f}, {1, 1, 1});
    fuhgl::render::shapes::box(flagMesh, {0.02f, 0.45f, 0.7f}, {1, 1, 1});
    fuhgl::render::shapes::quad(pad, 0.55f, {1, 1, 1});
    std::vector<fuhgl::render::Mesh> rocks;
    for (int i = 0; i < 4; ++i)
        rocks.push_back(fuhgl::render::makeSphere(1.f + 0.4f * i)->buildMesh(
            16, {1, 1, 1}, fuhgl::render::SolidUv::Spherical));

    std::mt19937 rng(42u);
    std::uniform_real_distribution<float> pos(-165.f, 165.f);
    std::uniform_real_distribution<float> uni(0.f, 1.f);

    // --- vegetation: trees on the hills, rocks, reeds at the shore, lily pads on the lake ---
    int trees = 0;
    for (int i = 0; i < 2600 && trees < 230; ++i) {
        const float x = pos(rng), z = pos(rng);
        const float h = tc.heightAt(x, z);
        if (h < (waterLevel + 1.6f) / tc.heightScale) continue;      // not in the lake
        if (std::hypot(x - basin.x, z - basin.y) < basinR + 6.f) continue; // not at the shore
        const float s = 0.9f + uni(rng) * 1.3f;
        scene::Node& tree = scene.root().createChild("tree" + std::to_string(trees));
        tree.translation = {x, h + 0.2f, z};
        tree.scale = {s, s, s};
        tree.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
        scene::MeshComponent& mc = tree.attach<scene::MeshComponent>();
        scene::MeshEntry& tr = mc.addMesh(&trunk);
        tr.albedo = {0.30f, 0.20f, 0.13f};
        tr.roughness = 1.f;
        tr.castShadow = true;
        tr.materialId = 4;
        scene::MeshEntry& fo = mc.addMesh(uni(rng) < 0.5f ? &foliage : &foliage2);
        fo.localOffset = fuhgl::math::translate({0.f, 1.8f, 0.f});
        // sunrise-lit foliage: green with a warm rim tint on the sun side
        const float g = 0.7f + uni(rng) * 0.55f;
        const float sunSide = std::max(0.f, (x - basin.x) * sunDir.x + (z - basin.y) * sunDir.z);
        fo.albedo = {0.10f * g + 0.10f * sunSide, 0.34f * g, 0.11f * g};
        fo.roughness = 0.9f;
        fo.castShadow = true;
        fo.materialId = 8;
        ++trees;
    }
    int rocksN = 0;
    for (int i = 0; i < 900 && rocksN < 46; ++i) {
        const float x = pos(rng), z = pos(rng);
        const float h = tc.heightAt(x, z);
        const bool inLake = std::hypot(x - basin.x, z - basin.y) < basinR;
        // heightAt is in world units: half-submerged rocks sit around the water line
        if (inLake ? (h > waterLevel + 0.4f || h < waterLevel - 1.2f) : (h < waterLevel + 1.0f)) continue;
        const float s = 0.6f + uni(rng) * 1.8f;
        scene::Node& rock = scene.root().createChild("rock" + std::to_string(rocksN));
        rock.translation = {x, h + 0.1f * s, z};
        rock.scale = {s, s * (0.6f + 0.7f * uni(rng)), s};
        rock.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 3.f, {0.f, 1.f, 0.f});
        scene::MeshEntry& re = rock.attach<scene::MeshComponent>().addMesh(&rocks[rocksN % 4]);
        re.albedo = {0.40f, 0.38f, 0.36f};
        re.roughness = 0.9f;
        re.castShadow = !inLake;
        re.materialId = 2;
        ++rocksN;
    }
    int reeds = 0;
    for (int i = 0; i < 4000 && reeds < 110; ++i) {
        const float a = uni(rng) * 2.f * fuhgl::math::kPi;
        const float r = basinR * (0.72f + 0.3f * uni(rng));
        const float x = basin.x + std::cos(a) * r, z = basin.y + std::sin(a) * r;
        const float h = tc.heightAt(x, z);
        if (h < waterLevel - 0.9f || h > waterLevel + 1.4f) continue; // right at the waterline
        scene::Node& rd = scene.root().createChild("reed" + std::to_string(reeds));
        rd.translation = {x, h + 0.5f, z};
        rd.scale = {0.8f + uni(rng) * 0.7f, 0.7f + uni(rng) * 0.9f, 0.8f + uni(rng) * 0.7f};
        rd.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
        scene::MeshEntry& re = rd.attach<scene::MeshComponent>().addMesh(&reed);
        const float g = 0.7f + uni(rng) * 0.5f;
        re.albedo = {0.16f * g, 0.38f * g, 0.14f * g};
        re.roughness = 1.f;
        re.castShadow = false;
        re.materialId = 16;
        ++reeds;
    }
    int pads = 0;
    for (int i = 0; i < 300 && pads < 16; ++i) {
        const float a = uni(rng) * 2.f * fuhgl::math::kPi;
        const float r = basinR * 0.62f * std::sqrt(uni(rng));
        const float x = basin.x + std::cos(a) * r, z = basin.y + std::sin(a) * r;
        scene::Node& lp = scene.root().createChild("pad" + std::to_string(pads));
        lp.translation = {x, waterLevel + 0.05f, z};
        lp.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
        const float s = 0.7f + uni(rng) * 0.9f;
        lp.scale = {s, 1.f, s};
        scene::MeshEntry& pe = lp.attach<scene::MeshComponent>().addMesh(&pad);
        pe.albedo = {0.10f, 0.35f, 0.12f};
        pe.roughness = 0.55f; // wet lily pads reflect the sky (SSR)
        pe.castShadow = false;
        pe.materialId = 64;
        pe.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);
        ++pads;
    }
    std::fprintf(stderr, "sunrise_screenshot: %d trees, %d rocks, %d reeds, %d pads\n", trees, rocksN,
                 reeds, pads);

    // --- dock into the lake + waving flag (G6) ---
    // dockDir points from the shore (-16,-2) toward the basin center; the dock starts at
    // the shore-side rim and extends INTO the lake (along dockDir)
    const fuhgl::math::Vec2 dockDir =
        fuhgl::math::normalize(fuhgl::math::Vec2{basin.x, basin.y} - fuhgl::math::Vec2{-16.f, -2.f});
    const fuhgl::math::Vec2 dockStart{basin.x - dockDir.x * (basinR + 2.f),
                                      basin.y - dockDir.y * (basinR + 2.f)};
    const fuhgl::math::Vec2 intoLake = dockDir;
    const float dockY = waterLevel + 0.38f;
    const float dockAngle = std::atan2(intoLake.x, intoLake.y); // yaw of the dock axis
    for (int p = 0; p < 12; ++p) { // planks
        scene::Node& pl = scene.root().createChild("plank" + std::to_string(p));
        pl.translation = {dockStart.x + intoLake.x * (1.0f + 1.1f * p), dockY,
                          dockStart.y + intoLake.y * (1.0f + 1.1f * p)};
        pl.rotation = fuhgl::math::fromAxisAngle(dockAngle, {0.f, 1.f, 0.f});
        scene::MeshEntry& pe = pl.attach<scene::MeshComponent>().addMesh(&plank);
        pe.albedo = {0.45f, 0.33f, 0.22f};
        pe.roughness = 0.8f; // wet wood (G22 dew)
        pe.castShadow = false;
        pe.materialId = 32;
    }
    for (int p = 0; p < 6; ++p) { // posts
        scene::Node& po = scene.root().createChild("post" + std::to_string(p));
        const float off = (p % 2 == 0 ? 1.15f : -1.15f);
        const float along = 1.0f + 2.2f * p;
        po.translation = {dockStart.x + intoLake.x * along + std::cos(dockAngle) * off, waterLevel + 0.3f,
                          dockStart.y + intoLake.y * along - std::sin(dockAngle) * off};
        scene::MeshEntry& pe = po.attach<scene::MeshComponent>().addMesh(&post);
        pe.albedo = {0.35f, 0.26f, 0.17f};
        pe.roughness = 0.9f;
        pe.castShadow = false;
        pe.materialId = 128;
    }
    { // flag pole at the lake end + flag (animated below)
        scene::Node& poleNode = scene.root().createChild("pole");
        poleNode.translation = {dockStart.x + intoLake.x * 13.2f, waterLevel + 0.1f,
                                dockStart.y + intoLake.y * 13.2f};
        scene::MeshEntry& pe = poleNode.attach<scene::MeshComponent>().addMesh(&pole);
        pe.albedo = {0.35f, 0.26f, 0.17f};
        pe.castShadow = false;
        pe.materialId = 128;
        scene::Node& flagNode = scene.root().createChild("flag");
        flagNode.translation = {dockStart.x + intoLake.x * 13.2f, waterLevel + 2.45f,
                                dockStart.y + intoLake.y * 13.2f};
        scene::MeshEntry& fe = flagNode.attach<scene::MeshComponent>().addMesh(&flagMesh);
        fe.albedo = {0.95f, 0.25f, 0.08f}; // signal-orange flag catching the sunrise
        fe.roughness = 0.8f;
        fe.castShadow = false;
        fe.materialId = 512;
        fe.localOffset = fuhgl::math::translate({0.02f, 0.f, 0.36f});
    }
    anim::AnimationPlayer flagPlayer;
    {
        anim::AnimationClip wave;
        wave.name = "wave";
        wave.duration = 1.6f;
        anim::TrsChannel ch;
        ch.nodeName = "flag";
        ch.times = {0.f, 0.4f, 0.8f, 1.2f, 1.6f};
        ch.rotations = {fuhgl::math::Quat{0, 0, 0, 1},
                        fuhgl::math::fromAxisAngle(0.55f, {0, 1, 0}),
                        fuhgl::math::fromAxisAngle(0.f, {0, 1, 0}),
                        fuhgl::math::fromAxisAngle(-0.55f, {0, 1, 0}),
                        fuhgl::math::Quat{0, 0, 0, 1}};
        wave.channels.push_back(ch);
        flagPlayer.addClip(wave);
        flagPlayer.play("wave", anim::LoopMode::Loop);
    }

    // --- G8 wear decal on the dock planks (material-ID gated to the planks) ---
    fuhgl::Texture wearTex = makeWearTexture(128, 9u);
    {
        scene::Node& dn = scene.root().createChild("dockWear");
        dn.translation = {dockStart.x + intoLake.x * 6.5f, dockY + 0.08f, dockStart.y + intoLake.y * 6.5f};
        dn.rotation = fuhgl::math::fromAxisAngle(dockAngle, {0.f, 1.f, 0.f});
        scene::DecalComponent& dc = dn.attach<scene::DecalComponent>();
        dc.decal.halfExtents = {7.0f, 0.05f, 0.75f};
        dc.decal.albedo = {1.f, 1.f, 1.f};
        dc.decal.albedoTexture = wearTex.id() != 0 ? &wearTex : nullptr;
        dc.decal.opacity = 0.6f;
        dc.decal.materialIdMask = 32u; // bit 5 = dock planks only
    }

    // --- G5 particles: sunlit pollen over the meadow + mist wisps over the lake ---
    scene::ParticleEmitterComponent* pollen = nullptr;
    scene::ParticleEmitterComponent* mist = nullptr;
    {
        scene::Node& n = scene.root().createChild("pollen");
        n.translation = {-30.f, tc.heightAt(-30.f, 25.f) + 1.6f, 25.f};
        pollen = &n.attach<scene::ParticleEmitterComponent>();
        pollen->config.position = {0.f, 0.f, 0.f};
        pollen->config.direction = {0.25f, 0.55f, 0.15f};
        pollen->config.speed = 0.5f;
        pollen->config.spread = 0.7f;
        pollen->config.rate = 160.f;
        pollen->config.lifetime = 5.f;
        pollen->config.size = 0.045f;
        pollen->config.grow = 0.012f;
        pollen->config.color = {1.0f, 0.88f, 0.55f};
        pollen->config.alpha = 0.55f;
        pollen->config.gravity = -0.03f;
        pollen->config.drag = 0.6f;
        pollen->config.blend = particles::BlendMode::Alpha;
        if (!pollen->system.create(4096, pollen->config))
            std::fprintf(stderr, "sunrise_screenshot: pollen emitter create failed\n");
    }
    {
        scene::Node& n = scene.root().createChild("mist");
        n.translation = {basin.x, waterLevel + 0.4f, basin.y};
        mist = &n.attach<scene::ParticleEmitterComponent>();
        mist->config.position = {0.f, 0.f, 0.f};
        mist->config.direction = {0.f, 1.f, 0.f};
        mist->config.speed = 0.14f;
        mist->config.spread = 1.0f;
        mist->config.rate = 26.f;
        mist->config.lifetime = 7.f;
        mist->config.size = 3.2f;
        mist->config.grow = 2.2f;
        mist->config.color = {0.98f, 0.82f, 0.72f};
        mist->config.alpha = 0.07f;
        mist->config.gravity = 0.02f;
        mist->config.drag = 0.25f;
        mist->config.blend = particles::BlendMode::Alpha;
        if (!mist->system.create(2048, mist->config))
            std::fprintf(stderr, "sunrise_screenshot: mist emitter create failed\n");
    }

    // --- sun + sky + IBL ---
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.42f, 0.18f}, 2.8f);
    scene.root().createChild("skyFill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.2f, -0.5f, 0.6f}, {0.18f, 0.16f, 0.30f}, 0.5f);

    fuhgl::Texture skyCube = makeSunriseSky(256, 17u, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "sunrise_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.5f;

    // --- root effects: everything visual at once ---
    scene::Effects& fx = scene.root().effects;
    fx.enable(render::Effect::Taa);
    fx.enable(render::Effect::Upscale);
    fx.enable(render::Effect::Tonemap);
    fx.enable(render::Effect::Ibl);
    fx.enable(render::Effect::CsmShadows);
    fx.enable(render::Effect::Water);
    fx.enable(render::Effect::Ssao);
    fx.enable(render::Effect::Ssr);
    fx.enable(render::Effect::Bloom);
    fx.enable(render::Effect::VolumetricFog);
    fx.enable(render::Effect::Wetness);
    fx.enable(render::Effect::Particles);
    fx.enable(render::Effect::Decals);
    scene::EffectParams& prm = fx.params;
    prm.exposure = 1.15f;
    prm.taaBlend = 0.08f;
    prm.shadowCenter = {0.f, 8.f, -20.f};
    prm.shadowRadius = 150.f;
    prm.shadowZNear = 0.5f;
    prm.shadowZFar = 350.f;
    prm.ssaoRadius = 1.2f;
    prm.ssaoIntensity = 1.2f;
    prm.ssrMaxSteps = 32;
    prm.ssrMaxDist = 40.f;
    prm.ssrStrength = 0.7f;
    prm.bloomThreshold = 1.0f;
    prm.bloomStrength = 0.85f;
    prm.bloomRadius = 1.3f;
    prm.volumetricDensity = 0.0055f; // light morning mist over the lake
    prm.volumetricCloudiness = 0.45f;
    prm.volumetricHeightFalloff = 7.f;
    prm.volumetricNoiseScale = 9.f;
    prm.volumetricAnisotropy = 0.65f;
    prm.volumetricMaxDist = 240.f;
    prm.volumetricSteps = 24;
    prm.volumetricStrength = 1.0f;
    prm.wetnessAmount = 0.35f; // morning dew on grass/wood -> glossy sunrise reflections
    prm.wetnessPuddle = 0.5f;
    prm.wetnessNoiseScale = 3.f;
    prm.tonemapOperator = 0; // ACES

    // --- renderer ---
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.05f, 0.09f, 0.27f};
    renderer.skyBottom = {1.0f, 0.45f, 0.15f};
    renderer.enableCsmShadows(1024, 4);
    renderer.setWaterReflectionScale(0.5f);
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.32f;
    renderer.godRays.decay = 0.955f;
    renderer.godRays.samples = 48;

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(55.f);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // --- two shots ---
    struct Shot {
        const char* name;
        fuhgl::math::Vec3 pos, target;
    };
    const fuhgl::math::Vec3 camWidePos{-38.f, tc.heightAt(-38.f, 30.f) + 6.5f, 30.f};
    const Shot shots[2] = {
        {"wide", camWidePos, {15.f, 4.f, -55.f}}, // across the lake toward the sun
        // standing at the dock's shore end, looking along the planks into the lake
        // (dockStart is on land — raise the camera above the terrain there)
        {"dock", {dockStart.x, tc.heightAt(dockStart.x, dockStart.y) + 3.2f, dockStart.y},
         {dockStart.x + intoLake.x * 10.f, dockY + 1.0f, dockStart.y + intoLake.y * 10.f}},
    };

    const float dt = 4.f / static_cast<float>(std::max(args.frames, 1));
    bool ok = true;
    for (const Shot& shot : shots) {
        cam.position = shot.pos;
        aimAt(cam, shot.target);
        for (int i = 0; i < args.frames; ++i) {
            wc.surface.update(dt);
            flagPlayer.update(dt);
            flagPlayer.applyTo(scene);
            if (pollen && pollen->system.isReady()) pollen->system.update(dt);
            if (mist && mist->system.isReady()) mist->system.update(dt);
            scene::FrameData frame;
            scene.fillFrame(frame, &cam, args.width, args.height);
            frame.view.env = &env;
            frame.view.fogColor = {0.90f, 0.52f, 0.32f}; // warm haze toward the horizon
            frame.view.fogDensity = 0.0007f;
            frame.effects.applyTo(renderer);
            renderer.setRenderScale(args.scale);
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                            frame.lightProbes, &frame.decals, frame.water, &frame.skins);
        }
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
        fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA,
                              fuhgl::gl::GL_UNSIGNED_BYTE, pixels.data());
        const std::string path = args.prefix + "_" + shot.name + ".png";
        if (!screenshot::writePng(path.c_str(), pixels.data(), args.width, args.height)) {
            std::fprintf(stderr, "sunrise_screenshot: failed to write %s\n", path.c_str());
            ok = false;
            continue;
        }
        std::fprintf(stderr, "sunrise_screenshot: wrote %s (%dx%d, %d frames, scale %.2f, GL %d.%d %s)\n",
                     path.c_str(), args.width, args.height, args.frames, args.scale,
                     fuhgl::gl::version().major, fuhgl::gl::version().minor,
                     fuhgl::gl::queryDeviceInfo().renderer.c_str());
    }
    return ok ? 0 : 1;
}
