// G31 world-generator screenshot tool: builds a LANDSCAPE and a CITY with
// procedural::generateWorld() (WorldGenParams) / procedural::generateCityWorld() and attaches
// the result to a scene::Scene. Landscape: heightmap terrain (macro relief + hydraulic/thermal
// erosion) with the biome splatmap (R grass / G rock / B sand / A snow) as albedo texture,
// water surface, GPU-instanced grass weighted by the splatmap, L-system trees/bushes and
// scattered rocks. City: ground, sidewalk/asphalt roads, voronoi district plates, procedural
// houses with per-facade POM walls (Backstein/Rauhputz/Stein), reflective window glass,
// shingle roofs, park lawns with bushes. Roads use the selectable surface (Asphalt /
// Kopfsteinpflaster with POM / Pflasterstein / Matsch) with irregular edges + potholes, and
// --wet renders them dark + reflective (low roughness, metallic, SSR). Pipeline mode: the
// generateCityWorld() pipeline (terrain+zones -> road graph -> lot subdivision -> houses).
// Rendered headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_worldgen_screenshot --prefix g31_worldgen [--width 1280] [--height 720]
//          [--frames 16] [--seed 777] [--mode both|landscape|city|pipeline] [--style town]
//          [--surface asphalt|cobble|paving|dirt] [--wet]
//   writes <prefix>_landscape.png, <prefix>_city.png, <prefix>_city_street.png and
//   <prefix>_pipeline*.png (skipping the shots of a mode not selected).
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>
#include <fuhglgraphics/procedural/citygen.hpp>
#include <fuhglgraphics/procedural/worldgen.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "../screenshot/png_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string prefix = "g31_worldgen";
    int width = 1280, height = 720, frames = 16;
    unsigned seed = 777u;
    std::string mode = "both"; // both | landscape | city | pipeline
    std::string style = "town"; // village | town | highrise (city house style)
    std::string surface = "cobble"; // asphalt | cobble | paving | dirt (city road surface)
    bool wet = false; // wet roads: dark + reflective
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) a.seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) a.mode = argv[++i];
        else if (std::strcmp(argv[i], "--style") == 0 && i + 1 < argc) a.style = argv[++i];
        else if (std::strcmp(argv[i], "--surface") == 0 && i + 1 < argc) a.surface = argv[++i];
        else if (std::strcmp(argv[i], "--wet") == 0) a.wet = true;
    }
    return a;
}

fuhglgraphics::procedural::RoadSurface surfaceFromName(const std::string& name) {
    using fuhglgraphics::procedural::RoadSurface;
    if (name == "cobble") return RoadSurface::Cobble;
    if (name == "paving") return RoadSurface::Paving;
    if (name == "dirt") return RoadSurface::Dirt;
    return RoadSurface::Asphalt;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

bool renderToPng(fuhglgraphics::scene::Scene& scn, fuhgl::FirstPersonCamera& cam,
                 fuhglgraphics::render::DeferredRenderer& renderer, int frames, int width, int height,
                 const std::string& outPath, fuhglgraphics::water::WaterSurface* water = nullptr,
                 const fuhgl::render::Environment* env = nullptr) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.view.env = env; // IBL environment baked from the sky cubemap
        frame.effects.applyTo(renderer);
        frame.view.fogColor = {0.80f, 0.83f, 0.90f};
        frame.view.fogDensity = 0.0009f;
        if (water) water->update(1.f / 60.f);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    fuhgl::gl::readPixels(0, 0, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    return fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), width, height);
}

// arena owning all uploaded meshes of one scene
struct MeshArena {
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> meshes;
    fuhgl::render::Mesh* add(const fuhgl::render::MeshData& md) {
        meshes.push_back(std::make_unique<fuhgl::render::Mesh>());
        meshes.back()->setMeshData(md);
        return meshes.back().get();
    }
};

void addMeshNode(fuhglgraphics::scene::Scene& scene, MeshArena& arena, const std::string& name,
                 const fuhgl::render::MeshData& md, const fuhgl::math::Vec3& albedo, float roughness,
                 std::uint32_t materialId, bool castShadow) {
    fuhglgraphics::scene::Node& n = scene.root().createChild(name);
    fuhglgraphics::scene::MeshEntry& e = n.attach<fuhglgraphics::scene::MeshComponent>().addMesh(arena.add(md));
    e.albedo = albedo;
    e.roughness = roughness;
    e.materialId = materialId;
    e.castShadow = castShadow;
}

// --- procedural textures for the houses (POM brick walls, shingle roofs) ----------------

std::uint32_t hashPx(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^ seed;
    h ^= h >> 13; h *= 0x9E3779B1u; h ^= h >> 16;
    return h;
}

fuhgl::Texture makeTex(int size, std::uint32_t seed, const std::function<void(int, int, float, unsigned char*)>& px) {
    std::vector<unsigned char> data(static_cast<std::size_t>(size) * size * 4, 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            px(x, y, (hashPx(x, y, seed) & 0xffu) / 255.f, &data[(static_cast<std::size_t>(y) * size + x) * 4]);
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, size, size, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 data.data());
    t.generateMipmap();
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR_MIPMAP_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_REPEAT);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_REPEAT);
    return t;
}

// upload a raw RGBA8 image (w*h*4 bytes, row-major) as a 2D texture
fuhgl::Texture makeTexFromRGBA(int w, int h, const std::vector<unsigned char>& rgba) {
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, w, h, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 rgba.data());
    t.generateMipmap();
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR_MIPMAP_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    return t;
}

// blend the GeneratedWorld splatmap weights (R grass / G rock / B sand / A snow) into an RGB
// albedo texture with fixed biome base colors — a production terrain shader would instead
// mix 4 material textures with these weights (triplanar on steep slopes)
fuhgl::Texture makeSplatAlbedoTex(const fuhglgraphics::procedural::GeneratedWorld& world) {
    const int g = world.gridW;
    const fuhgl::math::Vec3 grass{0.24f, 0.42f, 0.15f}, rock{0.42f, 0.40f, 0.38f},
        sand{0.72f, 0.64f, 0.46f}, snow{0.92f, 0.94f, 0.98f};
    std::vector<unsigned char> data(static_cast<std::size_t>(g) * g * 4, 0);
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const std::size_t s = (static_cast<std::size_t>(i) + g * j) * 4;
            const float r = world.splatmap[s + 0] / 255.f, gr = world.splatmap[s + 1] / 255.f,
                        b = world.splatmap[s + 2] / 255.f, a = world.splatmap[s + 3] / 255.f;
            const float sum = std::max(0.0001f, r + gr + b + a);
            fuhgl::math::Vec3 c = (grass * r + rock * gr + sand * b + snow * a) * (1.f / sum);
            // slight per-texel variation so large flat areas do not look flat
            const std::uint32_t n = hashPx(i, j, 99u);
            c = c * (0.92f + 0.16f * ((n & 0xffu) / 255.f));
            unsigned char* o = &data[s];
            o[0] = static_cast<unsigned char>(std::min(1.f, c.x) * 255.f);
            o[1] = static_cast<unsigned char>(std::min(1.f, c.y) * 255.f);
            o[2] = static_cast<unsigned char>(std::min(1.f, c.z) * 255.f);
            o[3] = 255;
        }
    return makeTexFromRGBA(g, g, data);
}

// static grid mesh built from a GeneratedWorld heightmap (pos3/normal3/color3/uv2 per vertex,
// uv = grid uv so the splatmap texture maps 1:1 onto the cells). In a production scene the
// terrain comes from scene::TerrainComponent (LodMesh LOD over the same heightmap).
// static grid mesh from a raw heightmap (pos3/normal3/color3/uv2 per vertex, uv = grid uv).
// In a production scene with a huge world use scene::TerrainComponent (LodMesh LOD) over the
// same heightmap instead; for the headless shots a static mesh keeps the render deterministic.
fuhgl::render::MeshData terrainMeshDataRaw(const std::vector<float>& heightmap, int gw, int gh,
                                           float sizeX, float sizeZ, float heightScale) {
    fuhgl::render::MeshData md;
    md.vertices.reserve(static_cast<std::size_t>(gw) * gh * fuhgl::render::MeshData::kFloatsPerVertex);
    const float cellX = sizeX / (gw - 1), cellZ = sizeZ / (gh - 1);
    auto hAt = [&](int x, int z) {
        x = std::min(gw - 1, std::max(0, x));
        z = std::min(gh - 1, std::max(0, z));
        return heightmap[static_cast<std::size_t>(x) + gw * z] * heightScale;
    };
    for (int j = 0; j < gh; ++j)
        for (int i = 0; i < gw; ++i) {
            const float x = -sizeX * 0.5f + i * cellX;
            const float z = -sizeZ * 0.5f + j * cellZ;
            const float y = hAt(i, j);
            const float dfdx = (hAt(i + 1, j) - hAt(i - 1, j)) / (2.f * cellX);
            const float dfdz = (hAt(i, j + 1) - hAt(i, j - 1)) / (2.f * cellZ);
            const fuhgl::math::Vec3 n = fuhgl::math::normalize({-dfdx, 1.f, -dfdz});
            const float vert[fuhgl::render::MeshData::kFloatsPerVertex] = {
                x, y, z, n.x, n.y, n.z, 1.f, 1.f, 1.f,
                static_cast<float>(i) / (gw - 1), static_cast<float>(j) / (gh - 1)};
            md.vertices.insert(md.vertices.end(), vert,
                               vert + fuhgl::render::MeshData::kFloatsPerVertex);
        }
    for (int j = 0; j < gh - 1; ++j)
        for (int i = 0; i < gw - 1; ++i) {
            const unsigned v00 = static_cast<unsigned>(j * gw + i), v10 = v00 + 1,
                           v01 = v00 + gw, v11 = v01 + 1;
            md.indices.push_back(v00); md.indices.push_back(v01); md.indices.push_back(v11);
            md.indices.push_back(v00); md.indices.push_back(v11); md.indices.push_back(v10);
        }
    return md;
}

fuhgl::render::MeshData terrainMeshData(const fuhglgraphics::procedural::GeneratedWorld& w) {
    return terrainMeshDataRaw(w.heightmap, w.gridW, w.gridH, w.sizeX, w.sizeZ, w.heightScale);
}

fuhgl::render::MeshData terrainMeshData(const fuhglgraphics::procedural::CityWorld& w) {
    return terrainMeshDataRaw(w.heightmap, w.gridW, w.gridH, w.sizeX, w.sizeZ, w.heightScale);
}

// brick pattern: running bond, mortar grooves; returns height in [0,1] (bricks high, mortar low)
float brickInfo(int x, int y, int size, float& rowH) {
    const float bx = static_cast<float>(x) / size * 8.f;   // 8 bricks per tile
    const float by = static_cast<float>(y) / size * 4.f;   // 4 rows per tile
    const int row = static_cast<int>(by);
    const float fy = by - row;
    const float offset = (row % 2) * 0.5f;
    const float fx = bx - offset - std::floor(bx - offset);
    const float mortar = 0.06f;
    rowH = (fx < mortar || fy < mortar) ? 0.f : 1.f;
    return rowH;
}

// normal map from a height function (wrap-around sampling), generic helper for the
// procedural road/facade height maps below
fuhgl::Texture makeNormalFromHeight(int size, const std::function<float(int, int)>& hFn) {
    return makeTex(size, 37u, [&](int x, int y, float, unsigned char* o) {
        const auto at = [&](int xx, int yy) {
            return hFn((xx + size) % size, (yy + size) % size);
        };
        const float dx = (at(x + 1, y) - at(x - 1, y)) * 2.2f;
        const float dy = (at(x, y + 1) - at(x, y - 1)) * 2.2f;
        const fuhgl::math::Vec3 n = fuhgl::math::normalize({-dx, -dy, 1.f});
        o[0] = static_cast<unsigned char>((n.x * 0.5f + 0.5f) * 255.f);
        o[1] = static_cast<unsigned char>((n.y * 0.5f + 0.5f) * 255.f);
        o[2] = static_cast<unsigned char>((n.z * 0.5f + 0.5f) * 255.f);
        o[3] = 255;
    });
}

fuhgl::Texture makeBrickAlbedo(int size) {
    return makeTex(size, 11u, [&](int x, int y, float n, unsigned char* o) {
        float h;
        brickInfo(x, y, size, h);
        const float brickTone = 0.55f + 0.25f * n;
        if (h < 0.5f) { o[0] = 168; o[1] = 160; o[2] = 152; } // mortar
        else { o[0] = static_cast<unsigned char>(150 * brickTone + 60); o[1] = static_cast<unsigned char>(70 * brickTone + 30); o[2] = static_cast<unsigned char>(55 * brickTone + 25); }
        o[3] = 255;
    });
}

fuhgl::Texture makeBrickHeight(int size) {
    return makeTex(size, 23u, [&](int x, int y, float, unsigned char* o) {
        float h;
        brickInfo(x, y, size, h);
        const unsigned char hv = static_cast<unsigned char>(h * 255.f);
        o[0] = hv; o[1] = hv; o[2] = hv; o[3] = 255;
    });
}

fuhgl::Texture makeBrickNormal(int size) {
    return makeNormalFromHeight(size, [&](int x, int y) {
        float hv;
        brickInfo(x, y, size, hv);
        return hv;
    });
}

// one procedural road surface set: albedo + height + normal (POM for the structured surfaces).
// The generator already gave the street slabs world-space uv (uvScale per surface), so the
// texture tiles in world units; wet roads are darkened by the generator vertex color and get
// a reflective material (low roughness + metallic) app-side.
struct RoadTexSet {
    fuhgl::Texture albedo, height, normal;
    bool pom = false; // attach heightMap + parallax params for this surface
};

RoadTexSet makeRoadTexSet(fuhglgraphics::procedural::RoadSurface surf, int size) {
    using fuhglgraphics::procedural::RoadSurface;
    RoadTexSet t;
    std::vector<float> h(static_cast<std::size_t>(size) * size, 0.5f);
    std::vector<float> tone(static_cast<std::size_t>(size) * size, 1.f);
    auto cellHash = [&](int cx, int cy, std::uint32_t seed) {
        return (hashPx(cx, cy, seed) & 0xffu) / 255.f;
    };
    if (surf == RoadSurface::Cobble) {
        // rounded cobblestones on a 6x6 cell grid, mortar in the gaps; height = dome per stone
        const int cells = 6;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float fx = static_cast<float>(x) / size * cells;
                const float fy = static_cast<float>(y) / size * cells;
                const int cx = static_cast<int>(fx), cy = static_cast<int>(fy);
                const float ox = (cellHash(cx, cy, 101u) - 0.5f) * 0.36f;
                const float oy = (cellHash(cx, cy, 202u) - 0.5f) * 0.36f;
                const float dx = fx - cx - 0.5f - ox, dy = fy - cy - 0.5f - oy;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float r = 0.44f;
                const std::size_t i = static_cast<std::size_t>(y) * size + x;
                tone[i] = 0.75f + 0.45f * cellHash(cx, cy, 303u);
                if (d >= r) h[i] = 0.f; // mortar gap
                else h[i] = std::sqrt(std::max(0.f, 1.f - (d / r) * (d / r))); // dome
            }
        t.pom = true;
        t.albedo = makeTex(size, 11u, [&](int x, int y, float, unsigned char* o) {
            const std::size_t i = static_cast<std::size_t>(y) * size + x;
            if (h[i] < 0.05f) { o[0] = 42; o[1] = 40; o[2] = 38; } // mortar
            else {
                const unsigned char v = static_cast<unsigned char>(96 * tone[i] + 40);
                o[0] = v; o[1] = static_cast<unsigned char>(v * 0.97f); o[2] = static_cast<unsigned char>(v * 0.92f);
            }
            o[3] = 255;
        });
    } else if (surf == RoadSurface::Paving) {
        // square paving slabs on a 4x4 grid with beveled edges
        const int cells = 4;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float fx = static_cast<float>(x) / size * cells;
                const float fy = static_cast<float>(y) / size * cells;
                const int cx = static_cast<int>(fx), cy = static_cast<int>(fy);
                const float lx = fx - cx, ly = fy - cy;
                const float border = std::min(std::min(lx, 1.f - lx), std::min(ly, 1.f - ly));
                const std::size_t i = static_cast<std::size_t>(y) * size + x;
                tone[i] = 0.8f + 0.3f * cellHash(cx, cy, 404u);
                h[i] = border < 0.09f ? 0.f : std::min(1.f, (border - 0.09f) / 0.12f) * 0.92f + 0.08f;
            }
        t.pom = true;
        t.albedo = makeTex(size, 23u, [&](int x, int y, float, unsigned char* o) {
            const std::size_t i = static_cast<std::size_t>(y) * size + x;
            if (h[i] < 0.05f) { o[0] = 52; o[1] = 51; o[2] = 49; }
            else {
                const unsigned char v = static_cast<unsigned char>(118 * tone[i] + 30);
                o[0] = v; o[1] = static_cast<unsigned char>(v * 0.985f); o[2] = static_cast<unsigned char>(v * 0.95f);
            }
            o[3] = 255;
        });
    } else if (surf == RoadSurface::Dirt) {
        // mud: brown noise with darker wet ruts
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * size + x;
                const float n = (hashPx(x, y, 505u) & 0xffu) / 255.f;
                const float n2 = (hashPx(x / 3, y / 3, 606u) & 0xffu) / 255.f;
                h[i] = 0.25f + 0.5f * n;
                tone[i] = n2 < 0.35f ? 0.55f : 0.85f + 0.3f * n; // dark ruts
            }
        t.albedo = makeTex(size, 53u, [&](int x, int y, float, unsigned char* o) {
            const std::size_t i = static_cast<std::size_t>(y) * size + x;
            o[0] = static_cast<unsigned char>(118 * tone[i]);
            o[1] = static_cast<unsigned char>(86 * tone[i]);
            o[2] = static_cast<unsigned char>(52 * tone[i]);
            o[3] = 255;
        });
    } else { // Asphalt
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * size + x;
                const float n = (hashPx(x, y, 707u) & 0xffu) / 255.f;
                const float crack = (hashPx(x / 2, y / 2, 808u) & 0xffu) / 255.f;
                h[i] = 0.5f + 0.06f * n;
                tone[i] = crack < 0.045f ? 0.45f : 0.85f + 0.3f * n; // sparse cracks
            }
        t.albedo = makeTex(size, 71u, [&](int x, int y, float, unsigned char* o) {
            const std::size_t i = static_cast<std::size_t>(y) * size + x;
            const unsigned char v = static_cast<unsigned char>(34 * tone[i] + 14);
            o[0] = v; o[1] = v; o[2] = static_cast<unsigned char>(v * 1.06f);
            o[3] = 255;
        });
    }
    t.height = makeTex(size, 91u, [&](int x, int y, float, unsigned char* o) {
        const unsigned char v = static_cast<unsigned char>(h[static_cast<std::size_t>(y) * size + x] * 255.f);
        o[0] = v; o[1] = v; o[2] = v; o[3] = 255;
    });
    t.normal = makeNormalFromHeight(size, [&](int x, int y) {
        return h[static_cast<std::size_t>(y) * size + x];
    });
    return t;
}

// one procedural facade set per HouseSpec.facade (0 Backstein, 1 Rauhputz, 2 Stein)
struct FacadeTexSet {
    fuhgl::Texture albedo, height, normal;
};

FacadeTexSet makePlasterTexSet(int size) {
    // Rauhputz: light warm plaster with fine grain (low height amplitude, no POM relief)
    FacadeTexSet t;
    std::vector<float> h(static_cast<std::size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            h[static_cast<std::size_t>(y) * size + x] =
                0.5f + 0.28f * ((hashPx(x, y, 909u) & 0xffu) / 255.f);
    t.albedo = makeTex(size, 31u, [&](int x, int y, float n, unsigned char* o) {
        const float v = 0.86f + 0.10f * n;
        o[0] = static_cast<unsigned char>(206 * v); o[1] = static_cast<unsigned char>(198 * v);
        o[2] = static_cast<unsigned char>(180 * v); o[3] = 255;
    });
    t.height = makeTex(size, 32u, [&](int x, int y, float, unsigned char* o) {
        const unsigned char v = static_cast<unsigned char>(h[static_cast<std::size_t>(y) * size + x] * 255.f);
        o[0] = v; o[1] = v; o[2] = v; o[3] = 255;
    });
    t.normal = makeNormalFromHeight(size, [&](int x, int y) {
        return h[static_cast<std::size_t>(y) * size + x];
    });
    return t;
}

FacadeTexSet makeStoneTexSet(int size) {
    // Stein: gray ashlar blocks (running bond, wide mortar) with real relief for POM
    FacadeTexSet t;
    std::vector<float> h(static_cast<std::size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            brickInfo(x, y, size, h[static_cast<std::size_t>(y) * size + x]);
    t.albedo = makeTex(size, 41u, [&](int x, int y, float n, unsigned char* o) {
        float hv;
        brickInfo(x, y, size, hv);
        if (hv < 0.5f) { o[0] = 62; o[1] = 60; o[2] = 57; } // mortar
        else {
            const float tone = 0.8f + 0.35f * n;
            const unsigned char v = static_cast<unsigned char>(128 * tone + 22);
            o[0] = v; o[1] = static_cast<unsigned char>(v * 0.985f);
            o[2] = static_cast<unsigned char>(v * 0.94f);
        }
        o[3] = 255;
    });
    t.height = makeTex(size, 42u, [&](int x, int y, float, unsigned char* o) {
        const unsigned char v = static_cast<unsigned char>(h[static_cast<std::size_t>(y) * size + x] * 255.f);
        o[0] = v; o[1] = v; o[2] = v; o[3] = 255;
    });
    t.normal = makeNormalFromHeight(size, [&](int x, int y) {
        return h[static_cast<std::size_t>(y) * size + x];
    });
    return t;
}

// MeshEntry material for the roads mesh of a generated world: surface texture set (+POM for
// structured surfaces) and, when wetRoads, a dark reflective material (IBL + SSR reflections)
void setupRoadEntry(fuhglgraphics::scene::MeshEntry& e, const fuhgl::Texture& albedo,
                    const fuhgl::Texture& height, const fuhgl::Texture& normal, bool pom,
                    bool wet) {
    e.albedo = {1.f, 1.f, 1.f};
    e.albedoTexture = &albedo;
    e.normalMap = &normal;
    if (pom) {
        e.heightMap = &height; // POM parallax for the cobble/paving relief
        e.parallaxScale = 0.055f;
        e.parallaxLayers = 28;
    }
    e.roughness = wet ? 0.12f : 0.92f; // wet asphalt: glossy reflections
    e.metallic = wet ? 0.4f : 0.f;
    e.castShadow = false;
    e.materialId = 3;
}

// reflective window glass entry (PlacedMesh::glassMesh) — metallic + near-zero roughness so
// IBL/SSR produce the sky reflection on the panes
void setupGlassEntry(fuhglgraphics::scene::MeshEntry& e) {
    e.albedo = {0.62f, 0.74f, 0.86f};
    e.roughness = 0.06f;
    e.metallic = 0.9f;
    e.castShadow = false;
    e.materialId = 9;
}

fuhgl::Texture makeShingleTex(int size) {
    return makeTex(size, 53u, [&](int x, int y, float n, unsigned char* o) {
        const float sx = static_cast<float>(x) / size * 12.f;
        const float sy = static_cast<float>(y) / size * 8.f;
        const int row = static_cast<int>(sy);
        const float fx = sx - (row % 2) * 0.5f - std::floor(sx - (row % 2) * 0.5f);
        const float fy = sy - row;
        const bool gap = fx < 0.08f || fy < 0.18f;
        const float tone = 0.5f + 0.3f * n;
        if (gap) { o[0] = 40; o[1] = 22; o[2] = 18; }
        else { o[0] = static_cast<unsigned char>(110 * tone); o[1] = static_cast<unsigned char>(38 * tone); o[2] = static_cast<unsigned char>(30 * tone); }
        o[3] = 255;
    });
}

void addSunAndSky(fuhglgraphics::scene::Scene& scene, fuhgl::FirstPersonCamera& cam,
                  const fuhgl::math::Vec3& sunDir) {
    using namespace fuhglgraphics;
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.95f, 0.85f}, 3.0f);
    cam.fovY = fuhgl::math::radians(55.f);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.params.exposure = 1.12f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowCenter = {0.f, 8.f, 0.f};
    scene.root().effects.params.shadowRadius = 160.f;
    scene.root().effects.params.shadowZNear = 1.f;
    scene.root().effects.params.shadowZFar = 600.f;
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
    if (!window.create("fuhglgraphics_worldgen_screenshot", 64, 64)) {
        std::fprintf(stderr, "worldgen_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "worldgen_screenshot: context below GL 3.3\n");
        return 2;
    }

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.enableCsmShadows(1024, 4);
    renderer.setWaterReflectionScale(0.5f);
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "worldgen_screenshot: skybox create failed\n");
    const fuhgl::math::Vec3 skyTop{0.20f, 0.38f, 0.72f}, skyHorizon{0.82f, 0.85f, 0.92f};
    skybox.makeGradient(skyTop, skyHorizon, 256);
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    fuhgl::render::Environment env; // IBL baked from the same sky cubemap
    if (skybox.texture().id() != 0 && !env.createFromCubemap(skybox.texture(), 16, 32, 64))
        std::fprintf(stderr, "worldgen_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.8f;
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    const fuhgl::Texture brickAlbedo = makeBrickAlbedo(256);
    const fuhgl::Texture brickNormal = makeBrickNormal(256);
    const fuhgl::Texture brickHeight = makeBrickHeight(256);
    const fuhgl::Texture shingle = makeShingleTex(256);

    fuhgl::FirstPersonCamera cam;
    const bool doLandscape = args.mode == "both" || args.mode == "landscape";
    const bool doCity = args.mode == "both" || args.mode == "city";
    const bool doPipeline = args.mode == "pipeline";

    // --- landscape: generateWorld(Landscape) -> terrain + water + grass + trees + bushes ---
    if (doLandscape) {
        procedural::WorldGenParams p;
        p.mode = procedural::WorldGenParams::Mode::Landscape;
        p.seed = args.seed;
        p.worldSize = 200.f;
        p.terrainGrid = 129;
        p.terrainHeight = 34.f;   // pronounced mountains
        p.warpStrength = 0.6f;
        p.heightExponent = 0.85f;
        p.waterLevel = 0.30f;     // lake
        p.lakeRadius = 55.f;      // central lake basin
        p.treeCount = 130;
        p.bushCount = 180;
        p.rockCount = 60;
        p.grassBlades = 6000; // GPU-instanced grass blades, weighted by the splatmap below
        p.treeMinDist = 6.f;
        const procedural::GeneratedWorld world = procedural::generateWorld(p);
        std::fprintf(stderr,
                     "worldgen_screenshot: landscape seed %u: grid %d, %zu trees, %zu bushes, %zu rocks\n",
                     args.seed, world.gridW, world.trees.size(), world.bushes.size(),
                     world.rocks.size());

        scene::Scene scene;
        MeshArena arena;

        // terrain: static grid mesh from the GeneratedWorld heightmap; the biome splatmap
        // (R grass / G rock / B sand / A snow, from slope + erosion flow + altitude) is
        // blended into the albedo texture. Production scenes can instead feed the same
        // heightmap into scene::TerrainComponent (LodMesh LOD) with a splatmap shader.
        const fuhgl::Texture splatTex = makeSplatAlbedoTex(world);
        scene::Node& tn = scene.root().createChild("terrain");
        fuhgl::render::Mesh terrainMesh;
        terrainMesh.setMeshData(terrainMeshData(world));
        {
            scene::MeshEntry& te = tn.attach<scene::MeshComponent>().addMesh(&terrainMesh);
            te.albedo = {1.f, 1.f, 1.f};
            te.albedoTexture = &splatTex;
            te.roughness = 1.f;
            te.castShadow = true;
            te.materialId = 1;
        }

        // lake water surface at the water level (G13 — needs Effect::Water)
        scene.root().effects.enable(render::Effect::Water);
        scene::WaterComponent* water = nullptr;
        if (world.waterLevelWorld > 0.f) {
            scene::Node& wn = scene.root().createChild("lake");
            wn.translation = {0.f, world.waterLevelWorld, 0.f};
            water = &wn.attach<scene::WaterComponent>();
            if (!water->surface.create(160, p.worldSize * 1.1f)) {
                std::fprintf(stderr, "worldgen_screenshot: water create failed\n");
                return 1;
            }
            water->surface.waves = {
                {{1.f, 0.15f}, 0.32f, 16.f, 2.0f, 0.65f},
                {{0.7f, 0.7f}, 0.20f, 8.f, 1.4f, 0.55f},
                {{-0.5f, 0.86f}, 0.11f, 4.5f, 1.0f, 0.45f},
            };
            water->surface.reflectionStrength = 0.8f;
            water->surface.foamStrength = 1.f;
            water->surface.deepColor = {0.004f, 0.05f, 0.11f};
            water->surface.shallowColor = {0.06f, 0.30f, 0.38f};
        }

        // GPU-instanced grass blades snapped onto the terrain; the heightFn hides blades
        // under water and on non-grass splatmap texels (rock/snow/sand) — a density mask
        if (p.grassBlades > 0) {
            fuhgl::render::Mesh blade;
            fuhgl::render::shapes::box(blade, {0.015f, 0.22f, 0.015f}, {0.25f, 0.45f, 0.18f});
            scene::Node& grass = scene.root().createChild("grass");
            grass.instancing.enabled = true;
            grass.instancing.count = p.grassBlades;
            grass.instancing.layout = scene::InstanceLayout::Random;
            grass.instancing.extent = {p.worldSize - 8.f, 4.f, p.worldSize - 8.f};
            grass.instancing.seed = 7u;
            grass.instancing.heightFn = [&world](float x, float z) {
                const float h = world.heightAt(x, z);
                if (h < world.waterLevelWorld + 0.15f) return -1000.f;
                const int g = world.gridW;
                const float u = std::min(1.f, std::max(0.f, x / world.sizeX + 0.5f)) * (g - 1);
                const float v = std::min(1.f, std::max(0.f, z / world.sizeZ + 0.5f)) * (g - 1);
                const std::size_t idx =
                    (static_cast<std::size_t>(u) + g * static_cast<std::size_t>(v)) * 4;
                if (world.splatmap[idx + 0] < 110) return -1000.f; // grass weight too low
                return h + 0.22f;
            };
            scene::MeshEntry& ge = grass.attach<scene::MeshComponent>().addMesh(&blade);
            ge.albedo = {0.55f, 0.75f, 0.35f};
            ge.roughness = 1.f;
            ge.castShadow = false;
            ge.materialId = 2;
        }

        auto addPlant = [&](const std::string& prefix, const std::vector<procedural::PlacedMesh>& plants,
                            std::uint32_t materialId) {
            for (std::size_t i = 0; i < plants.size(); ++i) {
                const auto& t = plants[i];
                scene::Node& n = scene.root().createChild(prefix + std::to_string(i));
                n.translation = t.position;
                n.rotation = fuhgl::math::fromAxisAngle(t.rotY, {0.f, 1.f, 0.f});
                n.scale = {t.scale, t.scale, t.scale};
                scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(arena.add(t.mesh));
                e.albedo = t.tint;
                e.roughness = 0.9f;
                e.castShadow = true;
                e.materialId = materialId;
            }
        };
        addPlant("tree", world.trees, 30);
        addPlant("bush", world.bushes, 31);
        addPlant("rock", world.rocks, 32);
        // warm sun in the south (behind the shore camera) so the seen slopes are lit
        addSunAndSky(scene, cam, fuhgl::math::normalize(fuhgl::math::Vec3{0.30f, 0.50f, 0.85f}));

        // composition derived from the heightmap: camera on the south rim of the lake,
        // looking across the water toward the far shore with the mountains behind —
        // this guarantees the lake is always in frame
        float lakeCx = 0.f, lakeCz = 0.f, lakeSouthZ = -1e9f;
        int lakeN = 0;
        // only the central basin counts as "lake" (the island falloff puts the whole
        // border ring below the water level — that is the outer sea, not the lake)
        const float lakeSearchR =
            p.lakeRadius > 0.f ? p.lakeRadius * 1.25f : p.worldSize * 0.38f;
        for (int j = 0; j < world.gridH; ++j)
            for (int i = 0; i < world.gridW; ++i) {
                if (world.heightmap[static_cast<std::size_t>(i) + world.gridW * j] < p.waterLevel) {
                    const float x = (static_cast<float>(i) / (world.gridW - 1) - 0.5f) * world.sizeX;
                    const float z = (static_cast<float>(j) / (world.gridH - 1) - 0.5f) * world.sizeZ;
                    if (std::hypot(x, z) > lakeSearchR) continue;
                    lakeCx += x;
                    lakeCz += z;
                    ++lakeN;
                    lakeSouthZ = std::max(lakeSouthZ, z);
                }
            }
        if (lakeN > 0) {
            lakeCx /= lakeN;
            lakeCz /= lakeN;
        } else {
            lakeSouthZ = world.sizeZ * 0.25f;
        }
        // high vantage SE of the lake, looking NW across the water: sky on top, mountains
        // (rock/snow) behind the far shore, lake band in the middle, grass foreground
        const float camX = lakeCx + 30.f, camZ = lakeSouthZ + 14.f;
        cam.position = {camX, world.heightAt(camX, camZ) + 16.f, camZ};
        aimAt(cam, {lakeCx - 20.f, world.waterLevelWorld + 5.f, lakeCz - 35.f});
        const std::string path = args.prefix + "_landscape.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, path,
                         water ? &water->surface : nullptr, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", path.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", path.c_str());
    }

    // --- city: generateWorld(City) -> WFC layout, roads, district plates, houses, parks ---
    if (doCity) {
        procedural::WorldGenParams p;
        p.mode = procedural::WorldGenParams::Mode::City;
        p.seed = args.seed;
        p.houseStyle = args.style == "village" ? procedural::HouseStyle::Village
                     : args.style == "highrise" ? procedural::HouseStyle::Highrise
                                                 : procedural::HouseStyle::Town;
        p.cityGrid = 12;
        p.cellSize = 8.f;
        p.buildingHeight = 8.f;
        p.blocksPerDistrict = 28;
        p.roadSurface = surfaceFromName(args.surface); // Asphalt/Cobble/Paving/Dirt
        p.wetRoads = args.wet; // wet streets: rendered dark + reflective
        const procedural::GeneratedWorld world = procedural::generateWorld(p);
        std::fprintf(stderr, "worldgen_screenshot: city seed %u: %dx%d cells, %zu houses, %zu park bushes\n",
                     args.seed, world.cityW, world.cityH, world.buildings.size(), world.bushes.size());

        scene::Scene scene;
        MeshArena arena;
        addMeshNode(scene, arena, "ground", world.cityGround, {1.f, 1.f, 1.f}, 1.f, 2, false);
        addMeshNode(scene, arena, "pavements", world.pavements, {1.f, 1.f, 1.f}, 1.f, 2, false);
        if (world.districts.vertexCount() > 0)
            addMeshNode(scene, arena, "districts", world.districts, {1.f, 1.f, 1.f}, 1.f, 4, false);
        // streets: the selected surface texture (cobble/paving with POM), wet = reflective
        const RoadTexSet roadTex = makeRoadTexSet(world.roadSurface, 256);
        {
            scene::Node& rn = scene.root().createChild("roads");
            scene::MeshEntry& re =
                rn.attach<scene::MeshComponent>().addMesh(arena.add(world.roads));
            setupRoadEntry(re, roadTex.albedo, roadTex.height, roadTex.normal, roadTex.pom,
                           world.wetRoads);
        }

        fuhgl::math::Vec3 streetSpot{0.f, 0.f, 0.f};
        bool haveStreet = false;
        for (int z = 0; z < world.cityH && !haveStreet; ++z)
            for (int x = 0; x < world.cityW && !haveStreet; ++x) {
                const auto& c = world.cells[static_cast<std::size_t>(x) + world.cityW * z];
                if (c.tile >= 0 && world.tiles[c.tile].name != "block" && world.tiles[c.tile].name != "plaza") {
                    streetSpot = {(x - (world.cityW - 1) * 0.5f) * world.cityCellSize, 0.f,
                                  (z - (world.cityH - 1) * 0.5f) * world.cityCellSize};
                    haveStreet = true;
                }
            }

        // houses: walls entry with the per-facade POM material (Backstein/Rauhputz/Stein from
        // PlacedMesh::facade), glass entry (reflective), roof entry with shingle texture
        const FacadeTexSet plasterTex = makePlasterTexSet(256);
        const FacadeTexSet stoneTex = makeStoneTexSet(256);
        for (std::size_t i = 0; i < world.buildings.size(); ++i) {
            const auto& b = world.buildings[i];
            scene::Node& n = scene.root().createChild("house" + std::to_string(i));
            n.translation = b.position;
            n.rotation = fuhgl::math::fromAxisAngle(b.rotY, {0.f, 1.f, 0.f});
            n.scale = {b.scale, b.scale, b.scale};
            scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
            const FacadeTexSet* ft = b.facade == 1 ? &plasterTex : b.facade == 2 ? &stoneTex : nullptr;
            scene::MeshEntry& walls = mc.addMesh(arena.add(b.mesh));
            walls.albedo = {1.f, 1.f, 1.f};
            walls.albedoTexture = ft ? &ft->albedo : &brickAlbedo;
            walls.normalMap = ft ? &ft->normal : &brickNormal;
            walls.heightMap = ft ? &ft->height : &brickHeight; // POM parallax on the facade
            walls.parallaxScale = 0.05f;
            walls.parallaxLayers = 24;
            walls.roughness = 0.85f;
            walls.castShadow = true;
            walls.materialId = 5;
            if (b.glassMesh.vertexCount() > 0) { // reflective window glass
                scene::MeshEntry& gl = mc.addMesh(arena.add(b.glassMesh));
                setupGlassEntry(gl);
            }
            if (b.detailMesh.vertexCount() > 0) { // door/windows/chimney/bay: plain vertex colors
                scene::MeshEntry& det = mc.addMesh(arena.add(b.detailMesh));
                det.albedo = {1.4f, 1.4f, 1.4f};
                det.roughness = 0.4f;
                det.castShadow = true;
                det.materialId = 8;
            }
            if (b.roofMesh.vertexCount() > 0) {
                scene::MeshEntry& roof = mc.addMesh(arena.add(b.roofMesh)); // built on top of the walls
                roof.albedoTexture = &shingle;
                roof.albedo = {1.6f, 1.6f, 1.6f}; // counter the dark vertex color
                roof.roughness = 0.7f;
                roof.castShadow = true;
                roof.materialId = 6;
            }
        }
        // park bushes
        for (std::size_t i = 0; i < world.bushes.size(); ++i) {
            const auto& b = world.bushes[i];
            scene::Node& n = scene.root().createChild("parkbush" + std::to_string(i));
            n.translation = b.position;
            n.rotation = fuhgl::math::fromAxisAngle(b.rotY, {0.f, 1.f, 0.f});
            n.scale = {b.scale, b.scale, b.scale};
            scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(arena.add(b.mesh));
            e.albedo = {0.8f, 1.f, 0.8f};
            e.roughness = 0.9f;
            e.castShadow = true;
            e.materialId = 7;
        }
        addSunAndSky(scene, cam, fuhgl::math::normalize(fuhgl::math::Vec3{-0.4f, 0.65f, 0.5f}));
        scene.root().effects.enable(render::Effect::Ssr); // screen-space reflections (wet roads, glass)

        // oblique overview of the whole WFC layout
        cam.position = {0.f, 80.f, 100.f};
        aimAt(cam, {0.f, 0.f, 0.f});
        const std::string ovPath = args.prefix + "_city.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, ovPath, nullptr, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", ovPath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", ovPath.c_str());

        // street-level view from the first street cell toward the city center
        cam.position = {streetSpot.x, 2.4f, streetSpot.z + world.cityCellSize * 2.2f};
        aimAt(cam, {streetSpot.x * 0.3f, 2.2f, streetSpot.z * 0.3f});
        const std::string stPath = args.prefix + "_city_street.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, stPath, nullptr, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", stPath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s (GL %d.%d %s)\n", stPath.c_str(),
                     fuhgl::gl::version().major, fuhgl::gl::version().minor,
                     fuhgl::gl::queryDeviceInfo().renderer.c_str());
    }

    // --- pipeline city: generateCityWorld (terrain+zones -> road graph -> lots -> houses) ---
    if (doPipeline) {
        procedural::CityGenParams p;
        p.seed = args.seed;
        p.worldSize = 240.f;
        p.cityRadius = 95.f;
        p.roadGrid = 9;
        p.roadSurface = surfaceFromName(args.surface);
        p.wetRoads = args.wet;
        const procedural::CityWorld world = procedural::generateCityWorld(p);
        std::fprintf(stderr,
                     "worldgen_screenshot: pipeline seed %u: %zu road nodes, %zu edges, %zu lots, %zu houses\n",
                     args.seed, world.roads.nodes.size(), world.roads.edges.size(), world.lots.size(),
                     world.houses.size());

        scene::Scene scene;
        MeshArena arena;
        // terrain from the pipeline heightmap as a static grid mesh (LodMesh LOD is flaky
        // under headless llvmpipe for this shot; production scenes use TerrainComponent)
        scene::Node& tn = scene.root().createChild("terrain");
        fuhgl::render::Mesh terrainMesh;
        terrainMesh.setMeshData(terrainMeshData(world));
        {
            scene::MeshEntry& te = tn.attach<scene::MeshComponent>().addMesh(&terrainMesh);
            te.albedo = {0.20f, 0.28f, 0.14f}; // grass green: roads/houses stay readable on it
            te.roughness = 1.f;
            te.castShadow = true;
            te.materialId = 1;
        }
        scene.root().effects.enable(render::Effect::Water);
        // lake around the plateau — placed a bit below the nominal level with small waves so
        // wave crests can never reach the plateau (water over the city would look broken)
        scene::Node& wn = scene.root().createChild("lake");
        wn.translation = {0.f, world.waterLevelWorld - 0.5f, 0.f};
        scene::WaterComponent& wc = wn.attach<scene::WaterComponent>();
        if (!wc.surface.create(160, p.worldSize * 1.1f)) {
            std::fprintf(stderr, "worldgen_screenshot: water create failed\n");
            return 1;
        }
        wc.surface.waves = {
            {{1.f, 0.15f}, 0.16f, 16.f, 2.0f, 0.65f},
            {{0.7f, 0.7f}, 0.10f, 8.f, 1.4f, 0.55f},
        };
        wc.surface.reflectionStrength = 0.8f;
        wc.surface.foamStrength = 1.f;
        wc.surface.deepColor = {0.004f, 0.05f, 0.11f};
        wc.surface.shallowColor = {0.06f, 0.30f, 0.38f};

        addMeshNode(scene, arena, "sidewalks", world.sidewalkMesh, {1.f, 1.f, 1.f}, 1.f, 2, false);
        addMeshNode(scene, arena, "lots", world.lotMesh, {1.f, 1.f, 1.f}, 1.f, 4, false);
        const RoadTexSet roadTex = makeRoadTexSet(world.roadSurface, 256);
        {
            scene::Node& rn = scene.root().createChild("roads");
            scene::MeshEntry& re =
                rn.attach<scene::MeshComponent>().addMesh(arena.add(world.roadMesh));
            setupRoadEntry(re, roadTex.albedo, roadTex.height, roadTex.normal, roadTex.pom,
                           world.wetRoads);
        }
        // houses: bake every house's transform into merged static meshes — walls split per
        // facade material (Backstein/Rauhputz/Stein POM), plus details, roofs and the
        // reflective glass — one draw item each instead of ~4 per house (~6500 houses here);
        // merging static geometry is the recommended app-side performance pattern and keeps
        // the headless render deterministic
        fuhgl::render::MeshData wallsAll[3], detAll, roofAll, glassAll;
        auto appendT = [](fuhgl::render::MeshData& dst, const fuhgl::render::MeshData& src,
                          const fuhgl::math::Mat4& m) {
            const fuhgl::math::Mat3 m3(m);
            const std::uint32_t base = static_cast<std::uint32_t>(dst.vertexCount());
            for (std::size_t i = 0; i < src.vertexCount(); ++i) {
                const float* v = &src.vertices[i * 11];
                const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{v[0], v[1], v[2], 1.f};
                const fuhgl::math::Vec3 n =
                    fuhgl::math::normalize(m3 * fuhgl::math::Vec3{v[3], v[4], v[5]});
                dst.vertices.push_back(p.x);
                dst.vertices.push_back(p.y);
                dst.vertices.push_back(p.z);
                dst.vertices.push_back(n.x);
                dst.vertices.push_back(n.y);
                dst.vertices.push_back(n.z);
                for (int k = 6; k < 11; ++k) dst.vertices.push_back(v[k]);
            }
            for (const std::uint32_t idx : src.indices) dst.indices.push_back(base + idx);
        };
        for (const auto& b : world.houses) {
            const fuhgl::math::Mat4 m = fuhgl::math::translate(b.position) *
                                        fuhgl::math::rotateY(b.rotY) *
                                        fuhgl::math::scale({b.scale, b.scale, b.scale});
            const int fi = b.facade >= 0 && b.facade <= 2 ? b.facade : 0;
            appendT(wallsAll[fi], b.mesh, m);
            if (b.glassMesh.vertexCount() > 0) appendT(glassAll, b.glassMesh, m);
            if (b.detailMesh.vertexCount() > 0) appendT(detAll, b.detailMesh, m);
            if (b.roofMesh.vertexCount() > 0) appendT(roofAll, b.roofMesh, m);
        }
        const FacadeTexSet plasterTex = makePlasterTexSet(256);
        const FacadeTexSet stoneTex = makeStoneTexSet(256);
        for (int fi = 0; fi < 3; ++fi) {
            if (wallsAll[fi].vertexCount() == 0) continue;
            const FacadeTexSet* ft = fi == 1 ? &plasterTex : fi == 2 ? &stoneTex : nullptr;
            scene::Node& n = scene.root().createChild("houseWalls" + std::to_string(fi));
            scene::MeshEntry& walls = n.attach<scene::MeshComponent>().addMesh(arena.add(wallsAll[fi]));
            walls.albedo = {1.f, 1.f, 1.f};
            walls.albedoTexture = ft ? &ft->albedo : &brickAlbedo;
            walls.normalMap = ft ? &ft->normal : &brickNormal;
            walls.heightMap = ft ? &ft->height : &brickHeight;
            walls.parallaxScale = 0.05f;
            walls.parallaxLayers = 24;
            walls.roughness = 0.85f;
            walls.materialId = 5;
            walls.castShadow = true;
        }
        if (glassAll.vertexCount() > 0) {
            scene::Node& n = scene.root().createChild("houseGlass");
            scene::MeshEntry& gl = n.attach<scene::MeshComponent>().addMesh(arena.add(glassAll));
            setupGlassEntry(gl);
        }
        if (detAll.vertexCount() > 0) {
            scene::Node& n = scene.root().createChild("houseDetails");
            scene::MeshEntry& det = n.attach<scene::MeshComponent>().addMesh(arena.add(detAll));
            det.albedo = {1.4f, 1.4f, 1.4f};
            det.roughness = 0.4f;
            det.materialId = 8;
        }
        if (roofAll.vertexCount() > 0) {
            scene::Node& n = scene.root().createChild("houseRoofs");
            scene::MeshEntry& roof = n.attach<scene::MeshComponent>().addMesh(arena.add(roofAll));
            roof.albedoTexture = &shingle;
            roof.albedo = {1.6f, 1.6f, 1.6f};
            roof.roughness = 0.7f;
            roof.materialId = 6;
            roof.castShadow = true;
        }
        addSunAndSky(scene, cam, fuhgl::math::normalize(fuhgl::math::Vec3{-0.3f, 0.55f, 0.6f}));
        scene.root().effects.enable(render::Effect::Ssr); // reflections on wet roads / glass

        // overview steeply down onto the plateau, same camera as the (stable) WFC city
        // overview: road grid + houses fill the lower frame, sky above
        cam.position = {0.f, 80.f, 100.f};
        aimAt(cam, {0.f, 0.f, 0.f});
        const std::string ovPath = args.prefix + "_pipeline.png";
        // overview without the water pass (the lake around the plateau is flaky under
        // headless llvmpipe and can wash out the whole frame); street shot keeps water
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, ovPath,
                         nullptr, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", ovPath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", ovPath.c_str());

        // street-level view from the road node nearest the city center, looking toward
        // the center; camera height follows the terrain so it is never underground
        if (!world.roads.nodes.empty()) {
            int best = 0;
            float bestD = 1e18f;
            for (int i = 0; i < static_cast<int>(world.roads.nodes.size()); ++i) {
                const float d = world.roads.nodes[i].x * world.roads.nodes[i].x +
                                world.roads.nodes[i].y * world.roads.nodes[i].y;
                if (d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            const auto& nd = world.roads.nodes[best];
            const float gy = world.heightAt(nd.x, nd.y);
            cam.position = {nd.x, gy + 2.4f, nd.y + 7.f};
            aimAt(cam, {0.f, world.heightAt(0.f, 0.f) + 1.8f, 0.f});
            const std::string stPath = args.prefix + "_pipeline_street.png";
            if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, stPath, &wc.surface, &env)) {
                std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", stPath.c_str());
                return 1;
            }
            std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", stPath.c_str());
        }
    }
    return 0;
}
