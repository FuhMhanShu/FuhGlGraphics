// G31 world-generator screenshot tool: builds a LANDSCAPE and a CITY with
// procedural::generateWorld() (WorldGenParams) / procedural::generateCityWorld() and attaches
// the result to a scene::Scene. Landscape: heightmap terrain (macro relief + hydraulic/thermal
// erosion) with the biome splatmap (R grass / G rock / B sand / A snow) as albedo texture,
// water surface, GPU-instanced grass weighted by the splatmap, L-system trees/bushes and
// scattered rocks, plus G38 settlements connected by an organic A* road network (road ribbons,
// bridges, milestones/lanterns/signs/walls props, hamlet houses). Roads carve their bench into
// the terrain; vegetation keeps off the road mask. G39 ambient life: causal clutter scatter
// (rockfall at cliff feet, riverbed pebbles/reeds, litter under trees, building + road-rim
// clutter) baked into one mesh, plus life zones (bird flock / fish school / insect swarm /
// fireflies / biome particles) derived from the world data and driven by a shared wind field.
// City: ground, sidewalk/asphalt roads, voronoi district plates, procedural
// houses with per-facade POM walls (Backstein/Rauhputz/Stein), reflective window glass,
// shingle roofs, park lawns with bushes. Roads use the selectable surface (Asphalt /
// Kopfsteinpflaster with POM / Pflasterstein / Matsch) with irregular edges + potholes, and
// --wet renders them dark + reflective (low roughness, metallic, SSR). Pipeline mode: the
// generateCityWorld() pipeline (terrain+zones -> road graph -> lot subdivision -> houses).
// Rendered headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_worldgen_screenshot --prefix g31_worldgen [--width 1280] [--height 720]
//          [--frames 16] [--seed 777] [--mode both|landscape|city|pipeline|village|panorama|tropical] [--style town]
//          [--fx all|none|hbao,ssr,bloom,volumetric,wetness] (village extra-effect selection)
//          [--surface asphalt|cobble|paving|dirt] [--wet]
//   writes <prefix>_landscape.png, <prefix>_roads.png (street-level at the main hamlet),
//   <prefix>_clutter.png + <prefix>_shore.png (G39 ground-level close-ups: building/road-rim
//   clutter at the hamlet, reeds/pebbles at the nearest waterline),
//   <prefix>_panorama.png (vegetated landscape + WFC city at a river + village at the lake,
//   connected by a G38 road with bridges), <prefix>_city.png, <prefix>_city_street.png,
//   <prefix>_village.png and <prefix>_pipeline*.png (skipping unselected modes).
//   --mode village: big landscape + small procedural village on the lake shore with many
//   effects (water, CSM shadows, IBL, HBAO, SSR, bloom, volumetric fog, wetness).
//   --mode tropical: writes <prefix>_tropical_sunset.png — a big sea bay with a fishing
//   village on the west beach, rendered at sunset with light cloud cover (G38 weather
//   drives sun/sky/volumetric clouds, tropical climate maps drive jungle vegetation).
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>
#include <fuhglgraphics/procedural/citygen.hpp>
#include <fuhglgraphics/procedural/plant.hpp>
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

#ifndef _WIN32
#include <csignal>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
// debug aid: print a symbolized backtrace when the headless llvmpipe render segfaults in CI
static void segvHandler(int) {
    void* bt[64];
    const int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
    _exit(139);
}
#endif

namespace {

struct Args {
    std::string prefix = "g31_worldgen";
    int width = 1280, height = 720, frames = 16;
    unsigned seed = 777u;
    std::string mode = "both"; // both | landscape | city | pipeline | village | panorama
    std::string style = "town"; // village | town | highrise (city house style)
    std::string surface = "asphalt"; // asphalt | cobble | paving | dirt (city road surface)
    std::string fx = "all"; // village extra effects: all | none | comma list (hbao,ssr,bloom,volumetric,wetness)
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
        else if (std::strcmp(argv[i], "--fx") == 0 && i + 1 < argc) a.fx = argv[++i];
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
                 const fuhgl::render::Environment* env = nullptr,
                 fuhglgraphics::weather::WeatherSystem* weather = nullptr,
                 float sunDiskBoost = 1.f) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.view.env = env; // IBL environment baked from the sky cubemap
        frame.effects.applyTo(renderer);
        if (!frame.weather) { // a G38 weather state owns fog/sun/sky itself
            frame.view.fogColor = {0.80f, 0.83f, 0.90f};
            frame.view.fogDensity = 0.0009f;
        }
        if (weather) {
            // HDR-boost the sun disk of a low sun so bloom picks up the sunset glow
            frame.view.sunColor = frame.view.sunColor * sunDiskBoost;
            weather->update(1.f / 60.f);
            renderer.skyTime += 1.f / 60.f;
            renderer.volumetricTime = renderer.skyTime;
        }
        if (water) water->update(1.f / 60.f);
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface,
                        frame.weather);
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

// --- G39 ambient life: causal clutter (G39a) + life zones (G39b) --------------------------

// Bakes all clutter items into one static mesh (per-item transform applied, vertex colors
// already encode the kind) so the whole scatter costs a single draw call.
fuhgl::render::MeshData bakeClutterMesh(const std::vector<fuhglgraphics::procedural::ClutterItem>& clutter) {
    fuhgl::render::MeshData out;
    for (const auto& c : clutter) {
        const fuhgl::math::Mat4 m = fuhgl::math::translate(c.position) *
                                    fuhgl::math::rotateY(c.rotY) *
                                    fuhgl::math::scale({c.scale, c.scale, c.scale});
        const fuhgl::math::Mat3 m3(m);
        const std::uint32_t base = static_cast<std::uint32_t>(out.vertexCount());
        for (std::size_t i = 0; i < c.mesh.vertexCount(); ++i) {
            const float* v = &c.mesh.vertices[i * 11];
            const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{v[0], v[1], v[2], 1.f};
            const fuhgl::math::Vec3 n = fuhgl::math::normalize(m3 * fuhgl::math::Vec3{v[3], v[4], v[5]});
            const float nv[11] = {p.x, p.y, p.z, n.x, n.y, n.z, v[6], v[7], v[8], v[9], v[10]};
            out.vertices.insert(out.vertices.end(), nv, nv + 11);
        }
        for (const std::uint32_t idx : c.mesh.indices) out.indices.push_back(base + idx);
    }
    return out;
}

void attachClutter(fuhglgraphics::scene::Scene& scene, MeshArena& arena,
                   const std::vector<fuhglgraphics::procedural::ClutterItem>& clutter) {
    if (clutter.empty()) return;
    addMeshNode(scene, arena, "clutter", bakeClutterMesh(clutter), {1.f, 1.f, 1.f}, 0.95f, 32, false);
}

// Re-scatters causal clutter on a post-processed world (village/panorama flatten pads and
// carve roads after generateWorld): reads the FINAL heightmap/flow/humidity/biome/road mask
// and the current tree + building spots.
std::vector<fuhglgraphics::procedural::ClutterItem>
rescatterClutter(const fuhglgraphics::procedural::GeneratedWorld& world,
                 const std::vector<float>* roadMask,
                 const std::vector<fuhglgraphics::procedural::PlacedMesh>& trees,
                 const std::vector<fuhgl::math::Vec2>& buildingSpots,
                 int maxItems, unsigned seed) {
    namespace pr = fuhglgraphics::procedural;
    pr::ClutterInput in;
    in.heightmap = &world.heightmap;
    in.gridW = world.gridW;
    in.gridH = world.gridH;
    in.sizeX = world.sizeX;
    in.sizeZ = world.sizeZ;
    in.heightScale = world.heightScale;
    in.waterLevel = world.waterLevelWorld / world.heightScale;
    in.flow = &world.flowmap;
    in.humidity = world.humidity.empty() ? nullptr : &world.humidity;
    in.biome = world.biome.empty() ? nullptr : &world.biome;
    in.roadMask = roadMask;
    for (const auto& t : trees) in.treeSpots.push_back({t.position.x, t.position.z});
    in.treeRadii.assign(trees.size(), 3.2f);
    in.buildingSpots = buildingSpots;
    pr::ClutterGenParams cp;
    cp.maxItems = maxItems;
    cp.seed = seed;
    pr::ClutterGenerator cg;
    return cg.generate(in, cp);
}

void printLifeZones(const std::vector<fuhglgraphics::procedural::LifeZone>& zones) {
    static const char* names[] = {"bird-flock", "fish-school", "insect-swarm", "fireflies", "ambient-particles"};
    for (const auto& z : zones)
        std::fprintf(stderr,
                     "worldgen_screenshot: lifezone %s at (%.1f, %.1f, %.1f) r=%.1f count=%d ambientKind=%d\n",
                     names[static_cast<int>(z.kind)], z.center.x, z.center.y, z.center.z, z.radius,
                     z.count, z.ambientKind);
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

// blend the GeneratedWorld splatmap weights (R vegetation / G rock / B sand / A snow) into an
// RGB albedo texture. The vegetation channel is colored by a soft Whittaker blend over the
// world's temperature/humidity maps (desert/grassland/forest/jungle/taiga/tundra — smooth
// transitions, no hard biome borders), rock gets sediment strata bands by world height, and
// river-sink ponds (pondMask) render as dark wet mud. A production terrain shader would
// instead mix 4 material textures with these weights (triplanar on steep slopes).
fuhgl::Texture makeSplatAlbedoTex(const fuhglgraphics::procedural::GeneratedWorld& world) {
    using fuhglgraphics::procedural::GeneratedWorld;
    const int g = world.gridW;
    const fuhgl::math::Vec3 rock{0.42f, 0.40f, 0.38f}, sand{0.72f, 0.64f, 0.46f},
        snow{0.92f, 0.94f, 0.98f}, mud{0.16f, 0.13f, 0.10f};
    // Whittaker biome centers in (temperature, humidity) space + base colors
    struct BiomeC { float t, h; fuhgl::math::Vec3 c; };
    const BiomeC biomes[] = {
        {0.90f, 0.15f, {0.76f, 0.66f, 0.42f}}, // Desert
        {0.55f, 0.30f, {0.30f, 0.46f, 0.16f}}, // Grassland
        {0.50f, 0.65f, {0.16f, 0.36f, 0.14f}}, // Forest
        {0.85f, 0.80f, {0.10f, 0.30f, 0.12f}}, // Jungle
        {0.20f, 0.60f, {0.14f, 0.28f, 0.18f}}, // Taiga
        {0.10f, 0.25f, {0.45f, 0.44f, 0.40f}}, // Tundra
    };
    const bool hasClimate = world.temperature.size() == world.heightmap.size();
    std::vector<unsigned char> data(static_cast<std::size_t>(g) * g * 4, 0);
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const std::size_t s = (static_cast<std::size_t>(i) + g * j) * 4;
            const float r = world.splatmap[s + 0] / 255.f, gr = world.splatmap[s + 1] / 255.f,
                        b = world.splatmap[s + 2] / 255.f, a = world.splatmap[s + 3] / 255.f;
            // soft Whittaker vegetation color from temperature/humidity (falloff weights)
            fuhgl::math::Vec3 veg{0.24f, 0.42f, 0.15f};
            if (hasClimate) {
                const float t = world.temperature[s / 4], h = world.humidity[s / 4];
                float wsum = 0.f;
                fuhgl::math::Vec3 acc{0.f, 0.f, 0.f};
                for (const BiomeC& bc : biomes) {
                    const float dt = t - bc.t, dh = h - bc.h;
                    const float d2 = dt * dt + dh * dh;
                    const float w = std::max(0.f, 1.f - d2 / 0.2f); // ~0.45 radius falloff
                    acc = acc + bc.c * (w * w);
                    wsum += w * w;
                }
                if (wsum > 1e-5f) veg = acc * (1.f / wsum);
            }
            // sediment strata in the rock channel: horizontal bands warped by noise, keyed on
            // world height (like Grand Canyon layers in cliffs)
            fuhgl::math::Vec3 rockC = rock;
            if (gr > 0.01f) {
                const float worldY = world.heightmap[s / 4] * world.heightScale;
                const float n = (hashPx(i * 3, j * 3, 77u) & 0xffu) / 255.f;
                const float band = 0.72f + 0.28f * std::sin(worldY * 1.8f + n * 6.2831f);
                rockC = rock * band;
            }
            const float sum = std::max(0.0001f, r + gr + b + a);
            fuhgl::math::Vec3 c = (veg * r + rockC * gr + sand * b + snow * a) * (1.f / sum);
            if (!world.pondMask.empty() && world.pondMask[s / 4]) c = c * 0.3f + mud * 0.7f; // pond
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

// facade material (HouseSpec.facade cast: 0 brick, 1 plaster, 2 stone, 3 wood, 4 adobe,
// 5 concrete) -> POM texture set + albedo tint. The context-driven new materials reuse the
// closest set with a tint: wood = plaster + warm tint (the timber frame itself is geometry in
// the details mesh), adobe = plaster + sand tint, concrete = stone + light grey.
const FacadeTexSet* facadeSet(int facade, const FacadeTexSet& plaster, const FacadeTexSet& stone,
                              fuhgl::math::Vec3& tint) {
    tint = {1.f, 1.f, 1.f};
    switch (facade) {
        case 1: return &plaster;
        case 2: return &stone;
        case 3: tint = {0.72f, 0.58f, 0.42f}; return &plaster; // wood
        case 4: tint = {1.12f, 0.95f, 0.68f}; return &plaster; // adobe
        case 5: tint = {0.95f, 0.97f, 1.0f};  return &stone;   // concrete
        default: return nullptr;                               // brick POM set
    }
}

// G31 weathering (moss / soil splash / rain streaks) for building material IDs 5..8
void enableWeathering(fuhglgraphics::scene::Scene& scene, float baseY, float humidity,
                      const fuhgl::math::Vec3& soil) {
    scene.root().effects.enable(fuhglgraphics::render::Effect::Weathering);
    scene.root().effects.params.weatherStrength = 0.85f;
    scene.root().effects.params.weatherHumidity = humidity;
    scene.root().effects.params.weatherBaseY = baseY;
    scene.root().effects.params.weatherSoilColor = soil;
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
#ifndef _WIN32
    std::signal(SIGSEGV, segvHandler);
#endif
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
    const FacadeTexSet plasterTex = makePlasterTexSet(256);
    const FacadeTexSet stoneTex = makeStoneTexSet(256);

    fuhgl::FirstPersonCamera cam;
    const bool doLandscape = args.mode == "both" || args.mode == "landscape";
    const bool doCity = args.mode == "both" || args.mode == "city";
    const bool doPipeline = args.mode == "pipeline";
    const bool doPanorama = args.mode == "both" || args.mode == "panorama";
    const bool doTropical = args.mode == "tropical";

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
        p.riverCount = 14;        // rivers carved from mountain sources down to the lake
        p.terraceSteps = 8;       // mild terracing on high ground (mesa/canyon steps)
        p.terraceStrength = 0.5f;
        p.settlementCount = 4;     // G38: hamlets placed by terrain+climate ...
        p.housesPerSettlement = 8; // ... connected by the organic A* road network
        procedural::GeneratedWorld world = procedural::generateWorld(p);
        std::fprintf(stderr,
                     "worldgen_screenshot: landscape seed %u: grid %d, %zu trees, %zu bushes, %zu rocks, "
                     "%zu settlements, %zu hamlet houses, %zu road props, %zu clutter items, %zu life zones\n",
                     args.seed, world.gridW, world.trees.size(), world.bushes.size(),
                     world.rocks.size(), world.settlements.size(), world.buildings.size(),
                     world.roadProps.size(), world.clutter.size(), world.lifeZones.size());
        printLifeZones(world.lifeZones);

        // lake center of the central basin (fallback frame when no settlements were placed).
        // Only the central basin counts as "lake" (the island falloff puts the whole border
        // ring below the water level — that is the outer sea, not the lake)
        float lakeCx = 0.f, lakeCz = 0.f;
        int lakeN = 0;
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
                }
            }
        if (lakeN > 0) {
            lakeCx /= lakeN;
            lakeCz /= lakeN;
        }
        // Scatter is removed around the camera so no tree can cross the near plane
        // (that renders as a green star blob).
        // frame the most important settlement: its hamlet houses, the road corridor with
        // milestones/lanterns and bridges over water — from a low vantage looking along the
        // main road that leaves the hamlet toward the nearest other settlement (the lake and
        // mountains stay in the background; falls back to the lake center without settlements)
        fuhgl::math::Vec2 topSite{lakeCx, lakeCz};
        float topImp = -1.f;
        for (const auto& s : world.settlements)
            if (s.importance > topImp) { topImp = s.importance; topSite = s.center; }
        fuhgl::math::Vec2 roadDir{0.f, -1.f}; // direction of the main road out of the hamlet
        float bestD = 1e9f;
        for (const auto& s : world.settlements) {
            const float d = std::hypot(s.center.x - topSite.x, s.center.y - topSite.y);
            if (d > 1.f && d < bestD) {
                bestD = d;
                roadDir = {(s.center.x - topSite.x) / d, (s.center.y - topSite.y) / d};
            }
        }
        const float siteH = world.heightAt(topSite.x, topSite.y);
        const float camX = topSite.x - roadDir.x * 26.f + 9.f;
        const float camZ = topSite.y - roadDir.y * 26.f + 12.f;
        const float camY = std::max(world.heightAt(camX, camZ), world.waterLevelWorld) + 14.f;
        const auto nearCam = [&](const procedural::PlacedMesh& pm) {
            return std::hypot(pm.position.x - camX, pm.position.z - camZ) < 14.f;
        };
        world.trees.erase(std::remove_if(world.trees.begin(), world.trees.end(), nearCam),
                          world.trees.end());
        world.bushes.erase(std::remove_if(world.bushes.begin(), world.bushes.end(), nearCam),
                           world.bushes.end());
        world.rocks.erase(std::remove_if(world.rocks.begin(), world.rocks.end(), nearCam),
                          world.rocks.end());
        world.clutter.erase(std::remove_if(world.clutter.begin(), world.clutter.end(),
                                           [&](const procedural::ClutterItem& c) {
                                               return std::hypot(c.position.x - camX, c.position.z - camZ) < 14.f;
                                           }),
                            world.clutter.end());
        // G39 close-up cameras (ground level), aimed at actual scattered items so the shot is
        // guaranteed to show clutter: hamlet rim (nearest building-ring item to the nearest
        // house) + shore (nearest reed/pebble/driftwood, fallback: nearest water cell).
        // Their clutter must be erased BEFORE the clutter mesh is baked below, so compute here.
        float clutterCamX = 0.f, clutterCamZ = 0.f, clutterAimX = 0.f, clutterAimZ = 0.f;
        bool haveClutterCam = false;
        {
            std::size_t hi = 0;
            float hd = 1e9f;
            for (std::size_t i = 0; i < world.buildings.size(); ++i) {
                const float d = std::hypot(world.buildings[i].position.x - topSite.x,
                                           world.buildings[i].position.z - topSite.y);
                if (d < hd) { hd = d; hi = i; }
            }
            if (!world.buildings.empty()) {
                const auto& hp = world.buildings[hi].position;
                // nearest building-ring clutter (weed/gravel/tile/crate) within 10 wu of the house
                float ad = 1e9f;
                fuhgl::math::Vec2 aim{hp.x, hp.z};
                for (const auto& c : world.clutter) {
                    const int k = static_cast<int>(c.kind);
                    if (k != static_cast<int>(procedural::ClutterKind::WeedTuft) &&
                        k != static_cast<int>(procedural::ClutterKind::GravelPile) &&
                        k != static_cast<int>(procedural::ClutterKind::RoofTile) &&
                        k != static_cast<int>(procedural::ClutterKind::Crate)) continue;
                    const float d = std::hypot(c.position.x - hp.x, c.position.z - hp.z);
                    if (d < 10.f && d < ad) { ad = d; aim = {c.position.x, c.position.z}; }
                }
                const float dirx = topSite.x - aim.x, dirz = topSite.y - aim.y;
                const float dl = std::hypot(dirx, dirz) + 1e-6f;
                clutterAimX = aim.x; clutterAimZ = aim.y;
                clutterCamX = aim.x + dirx / dl * 6.f;
                clutterCamZ = aim.y + dirz / dl * 6.f;
                haveClutterCam = true;
            }
        }
        float shoreCamX = 0.f, shoreCamZ = 0.f, shoreAimX = 0.f, shoreAimZ = 0.f;
        bool haveShoreCam = false;
        {
            // best REED nest at the waterline (reeds stand at/above the water -> visible, while
            // pebbles can sit deep in the channel below the surface): maximize the water-clutter
            // count within 6 wu, on flat ground (true world slope dh/dx <= 0.35), without a rock
            // near the camera line (a rock would fill the whole frame); tie-break: near the hamlet
            auto isWaterKind = [](int k) {
                return k == static_cast<int>(procedural::ClutterKind::Reed) ||
                       k == static_cast<int>(procedural::ClutterKind::Pebble) ||
                       k == static_cast<int>(procedural::ClutterKind::Driftwood);
            };
            auto slopeAt = [&](float x, float z) {
                const float e = 1.5f;
                const float gx = (world.heightAt(x + e, z) - world.heightAt(x - e, z)) / (2.f * e);
                const float gz = (world.heightAt(x, z + e) - world.heightAt(x, z - e)) / (2.f * e);
                return std::hypot(gx, gz);
            };
            auto rockNear = [&](float x, float z, float r) {
                for (const auto& rk : world.rocks)
                    if (std::hypot(rk.position.x - x, rk.position.z - z) < r) return true;
                return false;
            };
            const procedural::ClutterItem* best = nullptr;
            int bestNest = -1;
            float bestD = 1e9f;
            for (const auto& c : world.clutter) {
                if (static_cast<int>(c.kind) != static_cast<int>(procedural::ClutterKind::Reed)) continue;
                if (c.position.y < world.waterLevelWorld - 0.1f) continue; // at/above the waterline
                if (slopeAt(c.position.x, c.position.z) > 0.35f) continue;
                const float dirx = topSite.x - c.position.x, dirz = topSite.y - c.position.z;
                const float dl = std::hypot(dirx, dirz) + 1e-6f;
                const float camx = c.position.x + dirx / dl * 6.f, camz = c.position.z + dirz / dl * 6.f;
                if (rockNear(c.position.x, c.position.z, 5.f) || rockNear(camx, camz, 5.f)) continue;
                int nest = 0;
                for (const auto& o : world.clutter)
                    if (isWaterKind(static_cast<int>(o.kind)) &&
                        std::hypot(o.position.x - c.position.x, o.position.z - c.position.z) < 6.f) ++nest;
                const float d = std::hypot(c.position.x - topSite.x, c.position.z - topSite.y);
                if (nest > bestNest || (nest == bestNest && d < bestD)) {
                    bestNest = nest; bestD = d; best = &c;
                }
            }
            if (best) {
                const float dirx = topSite.x - best->position.x, dirz = topSite.y - best->position.z;
                const float dl = std::hypot(dirx, dirz) + 1e-6f;
                shoreAimX = best->position.x; shoreAimZ = best->position.z;
                shoreCamX = best->position.x + dirx / dl * 6.f; // camera on the land side
                shoreCamZ = best->position.z + dirz / dl * 6.f;
                haveShoreCam = true;
            } else {
                // fallback: nearest sub-water cell to the hamlet
                int bi = -1, bj = -1;
                float bd = 1e9f;
                for (int j = 0; j < world.gridH; ++j)
                    for (int i = 0; i < world.gridW; ++i) {
                        if (world.heightmap[static_cast<std::size_t>(i) + world.gridW * j] >= p.waterLevel) continue;
                        const float x = (static_cast<float>(i) / (world.gridW - 1) - 0.5f) * world.sizeX;
                        const float z = (static_cast<float>(j) / (world.gridH - 1) - 0.5f) * world.sizeZ;
                        const float d = std::hypot(x - topSite.x, z - topSite.y);
                        if (d < bd) { bd = d; bi = i; bj = j; }
                    }
                if (bi >= 0) {
                    const float sx = (static_cast<float>(bi) / (world.gridW - 1) - 0.5f) * world.sizeX;
                    const float sz = (static_cast<float>(bj) / (world.gridH - 1) - 0.5f) * world.sizeZ;
                    const float dirx = topSite.x - sx, dirz = topSite.y - sz;
                    const float dl = std::hypot(dirx, dirz) + 1e-6f;
                    shoreAimX = sx; shoreAimZ = sz;
                    shoreCamX = sx + dirx / dl * 5.f;
                    shoreCamZ = sz + dirz / dl * 5.f;
                    haveShoreCam = true;
                }
            }
        }
        auto eraseClutterNear = [&](float ex, float ez, float r) {
            world.clutter.erase(std::remove_if(world.clutter.begin(), world.clutter.end(),
                                               [&](const procedural::ClutterItem& c) {
                                                   return std::hypot(c.position.x - ex, c.position.z - ez) < r;
                                               }),
                                world.clutter.end());
        };
        if (haveClutterCam) eraseClutterNear(clutterCamX, clutterCamZ, 3.f);
        if (haveShoreCam) eraseClutterNear(shoreCamX, shoreCamZ, 3.f);

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
            // arena-owned: the MeshEntry must not point at a stack Mesh whose scope ends here
            scene::MeshEntry& ge = grass.attach<scene::MeshComponent>().addMesh(
                arena.add(fuhgl::render::shapes::boxData({0.015f, 0.22f, 0.015f}, {0.25f, 0.45f, 0.18f})));
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
        attachClutter(scene, arena, world.clutter); // G39a causal clutter, one baked mesh

        // G38 organic road network: extruded road ribbons (vertex colors carry the surface
        // material), bridge decks/posts over water, and spline props (milestones, lanterns,
        // signs, guard walls)
        if (world.roads.vertexCount() > 0) {
            scene::Node& rn = scene.root().createChild("roads");
            scene::MeshEntry& re = rn.attach<scene::MeshComponent>().addMesh(arena.add(world.roads));
            re.albedo = {1.f, 1.f, 1.f};
            re.roughness = 0.95f;
            re.castShadow = true;
            re.materialId = 1;
        }
        if (world.bridges.vertexCount() > 0) {
            scene::Node& bn = scene.root().createChild("bridges");
            scene::MeshEntry& be = bn.attach<scene::MeshComponent>().addMesh(arena.add(world.bridges));
            be.albedo = {1.f, 1.f, 1.f};
            be.roughness = 0.8f;
            be.castShadow = true;
            be.materialId = 1;
        }
        addPlant("roadprop", world.roadProps, 33);

        // hamlet houses of the settlements (same PlacedMesh layout as the city houses)
        for (std::size_t i = 0; i < world.buildings.size(); ++i) {
            const auto& b = world.buildings[i];
            scene::Node& n = scene.root().createChild("hamlet" + std::to_string(i));
            n.translation = b.position;
            n.rotation = fuhgl::math::fromAxisAngle(b.rotY, {0.f, 1.f, 0.f});
            n.scale = {b.scale, b.scale, b.scale};
            scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
            fuhgl::math::Vec3 facTint;
            const FacadeTexSet* ft = facadeSet(b.facade, plasterTex, stoneTex, facTint);
            scene::MeshEntry& walls = mc.addMesh(arena.add(b.mesh));
            walls.albedo = facTint;
            walls.albedoTexture = ft ? &ft->albedo : &brickAlbedo;
            walls.normalMap = ft ? &ft->normal : &brickNormal;
            walls.heightMap = ft ? &ft->height : &brickHeight;
            walls.parallaxScale = 0.05f;
            walls.parallaxLayers = 24;
            walls.roughness = 0.85f;
            walls.castShadow = true;
            walls.materialId = 5;
            if (b.glassMesh.vertexCount() > 0) {
                scene::MeshEntry& gl = mc.addMesh(arena.add(b.glassMesh));
                setupGlassEntry(gl);
            }
            if (b.detailMesh.vertexCount() > 0) {
                scene::MeshEntry& det = mc.addMesh(arena.add(b.detailMesh));
                det.albedo = {1.4f, 1.4f, 1.4f};
                det.roughness = 0.4f;
                det.castShadow = true;
                det.materialId = 8;
            }
            if (b.interiorMesh.vertexCount() > 0) { // rooms behind the real wall openings
                scene::MeshEntry& im = mc.addMesh(arena.add(b.interiorMesh));
                im.albedo = {1.2f, 1.2f, 1.2f};
                im.roughness = 0.9f;
                im.castShadow = true;
                im.materialId = 8;
            }
            if (b.doorMesh.vertexCount() > 0) { // hinged door leaves
                scene::MeshEntry& dm = mc.addMesh(arena.add(b.doorMesh));
                dm.albedo = {1.3f, 1.3f, 1.3f};
                dm.roughness = 0.5f;
                dm.castShadow = true;
                dm.materialId = 8;
            }
            if (b.roofMesh.vertexCount() > 0) {
                scene::MeshEntry& roof = mc.addMesh(arena.add(b.roofMesh));
                roof.albedoTexture = &shingle;
                roof.albedo = {1.6f, 1.6f, 1.6f};
                roof.roughness = 0.7f;
                roof.castShadow = true;
                roof.materialId = 6;
            }
        }
        // warm sun in the south (behind the shore camera) so the seen slopes are lit
        addSunAndSky(scene, cam, fuhgl::math::normalize(fuhgl::math::Vec3{0.30f, 0.50f, 0.85f}));

        // overview: hamlet + road corridor from a low vantage behind it (camX/camY/camZ above)
        cam.position = {camX, camY, camZ};
        aimAt(cam, {topSite.x, siteH + 3.f, topSite.y});
        const std::string path = args.prefix + "_landscape.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, path,
                         water ? &water->surface : nullptr, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", path.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", path.c_str());

        // oblique close-up of the hamlet and its road hub -> <prefix>_roads.png
        // (road ribbons with ruts, houses, milestones/lanterns, bridges over water)
        const float rx = topSite.x + 12.f, rz = topSite.y + 14.f;
        cam.position = {rx, std::max(world.heightAt(rx, rz), world.waterLevelWorld) + 22.f, rz};
        aimAt(cam, {topSite.x - roadDir.x * 4.f, siteH + 2.f, topSite.y - roadDir.y * 4.f});
        const std::string rpath = args.prefix + "_roads.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, rpath,
                         water ? &water->surface : nullptr, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", rpath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", rpath.c_str());

        // G39 ground-level close-ups -> <prefix>_clutter.png / <prefix>_shore.png.
        // Clutter items are 0.1..0.8 world units, so from a ~2 wu-high camera they fill pixels:
        // building-ring clutter + road-rim weeds at the hamlet, reeds/pebbles at the waterline.
        // (camera positions + near-camera clutter erase were computed before the bake above)
        if (haveClutterCam) {
            cam.position = {clutterCamX, std::max(world.heightAt(clutterCamX, clutterCamZ),
                                                  world.waterLevelWorld) + 3.0f, clutterCamZ};
            aimAt(cam, {clutterAimX, world.heightAt(clutterAimX, clutterAimZ) + 0.1f, clutterAimZ});
            const std::string cpath = args.prefix + "_clutter.png";
            if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, cpath,
                             water ? &water->surface : nullptr, &env)) {
                std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", cpath.c_str());
                return 1;
            }
            std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", cpath.c_str());
        }
        if (haveShoreCam) {
            cam.position = {shoreCamX, std::max(world.heightAt(shoreCamX, shoreCamZ),
                                                world.waterLevelWorld) + 3.2f, shoreCamZ};
            aimAt(cam, {shoreAimX, std::max(world.heightAt(shoreAimX, shoreAimZ), world.waterLevelWorld) + 0.1f,
                        shoreAimZ});
            const std::string spath = args.prefix + "_shore.png";
            if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, spath,
                             water ? &water->surface : nullptr, &env)) {
                std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", spath.c_str());
                return 1;
            }
            std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", spath.c_str());
        }
    }

    // --- village: big landscape + a small procedural village on the lake shore ----------
    // Many effects on: water, CSM shadows, IBL, HBAO, SSR, bloom, volumetric fog, wetness.
    if (args.mode == "village") {
        procedural::WorldGenParams p;
        p.mode = procedural::WorldGenParams::Mode::Landscape;
        p.seed = args.seed;
        p.worldSize = 200.f;
        p.terrainGrid = 129;
        p.terrainHeight = 34.f;
        p.warpStrength = 0.6f;
        p.heightExponent = 0.85f;
        p.waterLevel = 0.30f;
        p.lakeRadius = 55.f;
        p.treeCount = 150;
        p.bushCount = 200;
        p.rockCount = 60;
        p.grassBlades = 6000;
        p.treeMinDist = 6.f;
        p.riverCount = 14;
        p.clutterCount = 0;        // G39a: re-scattered below on the flattened village plateau
        procedural::GeneratedWorld world = procedural::generateWorld(p);
        printLifeZones(world.lifeZones);

        // lake south shore (same search as the landscape shot)
        float lakeCx = 0.f, lakeCz = 0.f, lakeSouthZ = -1e9f;
        int lakeN = 0;
        const float lakeSearchR = p.lakeRadius * 1.25f;
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
        if (lakeN > 0) { lakeCx /= lakeN; lakeCz /= lakeN; } else { lakeSouthZ = p.worldSize * 0.25f; }

        // flatten a village plateau on the south shore (edit the heightmap in place so the
        // terrain mesh, the grass heightFn and the camera all see the flat ground)
        const fuhgl::math::Vec2 vc{lakeCx, lakeSouthZ + 12.f};
        const float vr = 26.f;
        const float plateauY = world.waterLevelWorld + 1.4f;
        const float plateauN = plateauY / world.heightScale;
        const int g = world.gridW;
        const auto ss = [](float e0, float e1, float x) {
            const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
            return t * t * (3.f - 2.f * t);
        };
        for (int j = 0; j < world.gridH; ++j)
            for (int i = 0; i < world.gridW; ++i) {
                const float x = (static_cast<float>(i) / (g - 1) - 0.5f) * world.sizeX;
                const float z = (static_cast<float>(j) / (g - 1) - 0.5f) * world.sizeZ;
                const float d = std::hypot(x - vc.x, z - vc.y);
                const float t = 1.f - ss(vr * 0.55f, vr, d);
                float& h = world.heightmap[static_cast<std::size_t>(i) + g * j];
                h = h * (1.f - t) + plateauN * t;
            }
        // drop the scatter inside the village disc so houses do not intersect trees
        const auto inVillage = [&](const fuhgl::math::Vec3& pos) {
            return std::hypot(pos.x - vc.x, pos.z - vc.y) < vr + 5.f;
        };
        const auto notInVillage = [&](const procedural::PlacedMesh& pm) { return !inVillage(pm.position); };
        world.trees.erase(std::remove_if(world.trees.begin(), world.trees.end(), notInVillage),
                          world.trees.end());
        world.bushes.erase(std::remove_if(world.bushes.begin(), world.bushes.end(), notInVillage),
                           world.bushes.end());
        world.rocks.erase(std::remove_if(world.rocks.begin(), world.rocks.end(), notInVillage),
                          world.rocks.end());
        // G39a: re-scatter the causal clutter on the final (flattened) terrain, around the
        // village disc and the hamlet houses
        std::vector<fuhgl::math::Vec2> clutterBuildings{{vc.x, vc.y}};
        for (const auto& b : world.buildings) clutterBuildings.push_back({b.position.x, b.position.z});
        const std::vector<procedural::ClutterItem> clutter =
            rescatterClutter(world, world.roadMask.empty() ? nullptr : &world.roadMask, world.trees,
                             clutterBuildings, 500, args.seed ^ 0xC17u);
        std::fprintf(stderr, "worldgen_screenshot: village clutter: %zu items\n", clutter.size());
        // keep the village camera itself clear of trees (a tree crossing the near plane
        // renders as a green star blob)
        const auto nearVillageCam = [&](const procedural::PlacedMesh& pm) {
            return std::hypot(pm.position.x - (vc.x + 26.f), pm.position.z - (vc.y + 30.f)) < 10.f;
        };
        world.trees.erase(std::remove_if(world.trees.begin(), world.trees.end(), nearVillageCam),
                          world.trees.end());

        scene::Scene scene;
        MeshArena arena;
        const fuhgl::Texture splatTex = makeSplatAlbedoTex(world);
        scene::Node& tn = scene.root().createChild("terrain");
        fuhgl::render::Mesh terrainMesh;
        terrainMesh.setMeshData(terrainMeshData(world));
        {
            scene::MeshEntry& te = tn.attach<scene::MeshComponent>().addMesh(&terrainMesh);
            te.albedoTexture = &splatTex;
            te.roughness = 1.f;
            te.castShadow = true;
            te.materialId = 1;
        }
        scene.root().effects.enable(render::Effect::Water);
        scene::Node& wn = scene.root().createChild("lake");
        wn.translation = {0.f, world.waterLevelWorld, 0.f};
        scene::WaterComponent* water = &wn.attach<scene::WaterComponent>();
        water->surface.create(160, p.worldSize * 1.1f);
        water->surface.waves = {
            {{1.f, 0.15f}, 0.32f, 16.f, 2.0f, 0.65f},
            {{0.7f, 0.7f}, 0.20f, 8.f, 1.4f, 0.55f},
            {{-0.5f, 0.86f}, 0.11f, 4.5f, 1.0f, 0.45f},
        };
        water->surface.reflectionStrength = 0.8f;
        water->surface.foamStrength = 1.f;
        water->surface.deepColor = {0.004f, 0.05f, 0.11f};
        water->surface.shallowColor = {0.06f, 0.30f, 0.38f};
        if (p.grassBlades > 0) {
            scene::Node& grass = scene.root().createChild("grass");
            grass.instancing.enabled = true;
            grass.instancing.count = p.grassBlades;
            grass.instancing.layout = scene::InstanceLayout::Random;
            grass.instancing.extent = {p.worldSize - 8.f, 4.f, p.worldSize - 8.f};
            grass.instancing.seed = 7u;
            grass.instancing.heightFn = [&world](float x, float z) {
                const float h = world.heightAt(x, z);
                if (h < world.waterLevelWorld + 0.15f) return -1000.f;
                const int gg = world.gridW;
                const float u = std::min(1.f, std::max(0.f, x / world.sizeX + 0.5f)) * (gg - 1);
                const float v = std::min(1.f, std::max(0.f, z / world.sizeZ + 0.5f)) * (gg - 1);
                const std::size_t idx = (static_cast<std::size_t>(u) + gg * static_cast<std::size_t>(v)) * 4;
                if (world.splatmap[idx + 0] < 110) return -1000.f;
                return h + 0.22f;
            };
            // arena-owned: the MeshEntry must not point at a stack Mesh whose scope ends here
            scene::MeshEntry& ge = grass.attach<scene::MeshComponent>().addMesh(
                arena.add(fuhgl::render::shapes::boxData({0.015f, 0.22f, 0.015f}, {0.25f, 0.45f, 0.18f})));
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
        attachClutter(scene, arena, clutter); // G39a causal clutter on the flattened terrain

        // the village: a loose grid of procedural houses (HouseGenerator, Village style) on the
        // plateau, facade material + roof per house, reflective glass panes. The BuildingContext
        // comes from the landscape climate maps at the plateau center (climate -> roof/windows,
        // biome -> local material, low density -> gardens/fences)
        procedural::BuildingContext vctx;
        {
            const auto sampleF = [&](const std::vector<float>& m) -> float {
                if (m.empty()) return 0.5f;
                const int gg = world.gridW;
                const float u = std::min(1.f, std::max(0.f, vc.x / world.sizeX + 0.5f)) * (gg - 1);
                const float v = std::min(1.f, std::max(0.f, vc.y / world.sizeZ + 0.5f)) * (gg - 1);
                return m[static_cast<std::size_t>(u) + gg * static_cast<std::size_t>(v)];
            };
            vctx.temperature = sampleF(world.temperature);
            vctx.humidity = sampleF(world.humidity);
            vctx.precipitation = std::min(1.f, vctx.humidity * 0.95f);
            vctx.biome = world.biome.empty() ? -1
                : static_cast<int>(world.biome[static_cast<std::size_t>(
                      std::min(1.f, std::max(0.f, vc.x / world.sizeX + 0.5f)) * (world.gridW - 1)) +
                    world.gridW * static_cast<std::size_t>(
                      std::min(1.f, std::max(0.f, vc.y / world.sizeZ + 0.5f)) * (world.gridH - 1))]);
            vctx.wealth = 0.55f;
            vctx.density = 0.25f; // village: detached houses with garden fences
            fuhgl::math::Vec3 vSoil{0.38f, 0.31f, 0.22f};
            if (!world.splatmap.empty()) {
                const int gg = world.gridW;
                const float u = std::min(1.f, std::max(0.f, vc.x / world.sizeX + 0.5f)) * (gg - 1);
                const float v = std::min(1.f, std::max(0.f, vc.y / world.sizeZ + 0.5f)) * (gg - 1);
                const std::size_t si =
                    (static_cast<std::size_t>(u) + gg * static_cast<std::size_t>(v)) * 4;
                const float r = world.splatmap[si] / 255.f, g = world.splatmap[si + 1] / 255.f,
                            b = world.splatmap[si + 2] / 255.f;
                if (g >= r && g >= b) vSoil = {0.42f, 0.38f, 0.26f};       // grass soil
                else if (r >= g && r >= b) vSoil = {0.40f, 0.36f, 0.28f};  // meadow
                if (b > r && b > g) vSoil = {0.72f, 0.62f, 0.44f};         // sand
                if (g > r && g > b) vSoil = {0.36f, 0.34f, 0.30f};          // rock
            }
            vctx.soilColor = vSoil;
        }
        procedural::HouseGenerator houseGen;
        std::uint32_t hs = args.seed ^ 0x11EAu;
        auto nextr = [&]() {
            hs = hs * 1664525u + 1013904223u;
            return static_cast<float>(hs >> 8) * (1.f / 16777216.f);
        };
        int houseCount = 0;
        for (int gz = -2; gz <= 2; ++gz)
            for (int gx = -3; gx <= 3; ++gx) {
                if (nextr() < 0.3f) continue;
                const float hx = vc.x + gx * 9.f + (nextr() - 0.5f) * 2.f;
                const float hz = vc.y + gz * 9.f + (nextr() - 0.5f) * 2.f;
                if (std::hypot(hx - vc.x, hz - vc.y) > vr - 6.f) continue;
                const std::uint32_t hseed = args.seed ^ (0x5EEDu + static_cast<std::uint32_t>(houseCount) * 7919u);
                auto hp = procedural::HouseGenerator::randomParams(procedural::HouseStyle::Village, 8.f, hseed, vctx);
                hp.seed = hseed;
                hp.width = 5.f + nextr() * 2.5f;
                hp.depth = 5.f + nextr() * 2.5f;
                const auto hm = houseGen.generate(hp);
                scene::Node& n = scene.root().createChild("vhouse" + std::to_string(houseCount));
                n.translation = {hx, plateauY, hz};
                n.rotation = fuhgl::math::fromAxisAngle(nextr() * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
                scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
                fuhgl::math::Vec3 facTint;
                const FacadeTexSet* ft =
                    facadeSet(static_cast<int>(hm.spec.facade), plasterTex, stoneTex, facTint);
                scene::MeshEntry& walls = mc.addMesh(arena.add(hm.walls));
                walls.albedo = facTint;
                walls.albedoTexture = ft ? &ft->albedo : &brickAlbedo;
                walls.normalMap = ft ? &ft->normal : &brickNormal;
                walls.heightMap = ft ? &ft->height : &brickHeight; // POM parallax on the facade
                walls.parallaxScale = 0.05f;
                walls.parallaxLayers = 24;
                walls.roughness = 0.85f;
                walls.castShadow = true;
                walls.materialId = 5;
                if (hm.glass.vertexCount() > 0) {
                    scene::MeshEntry& gl = mc.addMesh(arena.add(hm.glass));
                    setupGlassEntry(gl);
                }
                if (hm.details.vertexCount() > 0) {
                    scene::MeshEntry& det = mc.addMesh(arena.add(hm.details));
                    det.albedo = {1.4f, 1.4f, 1.4f};
                    det.roughness = 0.4f;
                    det.castShadow = true;
                    det.materialId = 8;
                }
                if (hm.interior.vertexCount() > 0) { // rooms behind the real wall openings
                    scene::MeshEntry& im = mc.addMesh(arena.add(hm.interior));
                    im.albedo = {1.2f, 1.2f, 1.2f};
                    im.roughness = 0.9f;
                    im.castShadow = true;
                    im.materialId = 8;
                }
                if (hm.doors.vertexCount() > 0) { // hinged door leaves
                    scene::MeshEntry& dm = mc.addMesh(arena.add(hm.doors));
                    dm.albedo = {1.3f, 1.3f, 1.3f};
                    dm.roughness = 0.5f;
                    dm.castShadow = true;
                    dm.materialId = 8;
                }
                if (hm.roof.vertexCount() > 0) {
                    scene::MeshEntry& roof = mc.addMesh(arena.add(hm.roof));
                    roof.albedoTexture = &shingle;
                    roof.albedo = {1.6f, 1.6f, 1.6f};
                    roof.roughness = 0.7f;
                    roof.castShadow = true;
                    roof.materialId = 6;
                }
                ++houseCount;
            }
        // a ring of trees around the village clearing
        procedural::PlantGenerator plantGen;
        for (int t = 0; t < 14; ++t) {
            const float ang = nextr() * 2.f * fuhgl::math::kPi;
            const float rad = vr + 3.f + nextr() * 6.f;
            const float tx = vc.x + std::cos(ang) * rad, tz = vc.y + std::sin(ang) * rad;
            const float th = world.heightAt(tx, tz);
            if (th < world.waterLevelWorld + 0.3f) continue;
            procedural::PlantParams pp;
            pp.kind = static_cast<procedural::PlantKind>(static_cast<int>(nextr() * 3.f)); // deciduous/fir/birch
            pp.seed = args.seed ^ (0x7EAFu + static_cast<std::uint32_t>(t) * 131u);
            pp.scale = 0.5f + 0.5f * nextr();
            const auto pm = plantGen.generate(pp);
            scene::Node& n = scene.root().createChild("villageTree" + std::to_string(t));
            n.translation = {tx, th, tz};
            n.rotation = fuhgl::math::fromAxisAngle(nextr() * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
            n.scale = {pp.scale, pp.scale, pp.scale};
            scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(arena.add(pm.mesh));
            e.albedo = {0.9f, 1.f, 0.9f};
            e.roughness = 0.9f;
            e.castShadow = true;
            e.materialId = 30;
        }
        std::fprintf(stderr, "worldgen_screenshot: village seed %u: %d houses on the shore\n",
                     args.seed, houseCount);

        // many effects: water + reflections, CSM shadows, IBL, HBAO, SSR, bloom, volumetric
        // fog, wetness (morning damp), TAA + upscale + ACES tonemap
        addSunAndSky(scene, cam, fuhgl::math::normalize(fuhgl::math::Vec3{-0.35f, 0.30f, -0.85f}));
        scene.root().effects.enable(render::Effect::Water);
        // extra effects selectable via --fx (default: all) so CI can bisect llvmpipe crashes
        const auto fxOn = [&](const char* name) {
            return args.fx == "all" || args.fx.find(name) != std::string::npos;
        };
        if (fxOn("hbao")) scene.root().effects.enable(render::Effect::Hbao);
        if (fxOn("ssr")) scene.root().effects.enable(render::Effect::Ssr);
        if (fxOn("bloom")) scene.root().effects.enable(render::Effect::Bloom);
        if (fxOn("volumetric")) scene.root().effects.enable(render::Effect::VolumetricFog);
        if (fxOn("wetness")) scene.root().effects.enable(render::Effect::Wetness);
        enableWeathering(scene, plateauY, vctx.humidity, vctx.soilColor);
        std::fprintf(stderr, "worldgen_screenshot: village fx='%s'\n", args.fx.c_str());
        scene.root().effects.params.exposure = 1.1f;
        scene.root().effects.params.shadowCenter = {vc.x, plateauY, vc.y};
        scene.root().effects.params.shadowRadius = 120.f;
        scene.root().effects.params.shadowZFar = 400.f;

        // low vantage SE of the village looking NW: village foreground, lake middle,
        // mountains + sun behind the far shore
        cam.position = {vc.x + 26.f, plateauY + 4.5f, vc.y + 30.f};
        aimAt(cam, {lakeCx - 15.f, world.waterLevelWorld + 5.f, lakeCz - 30.f});
        const std::string vPath = args.prefix + "_village.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, vPath,
                         &water->surface, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", vPath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", vPath.c_str());
    }

    // --- tropical: a big sea bay with a fishing village on the west beach, rendered at
    //     sunset with light cloud cover. The G38 weather state drives sun light, the
    //     procedural sky, volumetric clouds and fog; the tropical climate maps (hot +
    //     humid shore) drive the jungle vegetation. Writes <prefix>_tropical_sunset.png --
    if (doTropical) {
        procedural::WorldGenParams p;
        p.mode = procedural::WorldGenParams::Mode::Landscape;
        p.seed = args.seed;
        p.worldSize = 420.f;        // big enough for a land strip west of the bay
        p.terrainGrid = 193;
        p.terrainHeight = 22.f;
        p.warpStrength = 0.5f;
        p.heightExponent = 0.8f;
        p.waterLevel = 0.33f;       // the sea (a bit higher -> wider beach band)
        p.lakeCarve = 0.22f;
        p.lakeRadius = 120.f;       // one big bay: the far shore stays below the horizon
        p.erosionDroplets = 3000;
        p.thermalPasses = 3;
        p.riverCount = 6;
        p.treeCount = 600;          // humid shore -> jungle biome -> broadleaf G37 trees
        p.bushCount = 500;
        p.rockCount = 40;
        p.grassBlades = 12000;
        p.treeMinDist = 5.f;
        p.settlementCount = 0;      // the fishing village is placed explicitly below
        p.clutterCount = 0;         // G39a: re-scattered below on the flattened beach plateau
        p.tempNoiseScale = 0.004f;  // little temperature variation: stays hot at the equator
        p.humidityRiverBoost = 0.9f;
        p.lapseRate = 0.3f;         // hills stay warm, so the shore stays jungle
        procedural::GeneratedWorld world = procedural::generateWorld(p);
        printLifeZones(world.lifeZones);
        const int g = world.gridW;

        // sea center + west waterline: the beach (and village) faces east, toward the low
        // evening sun over the water
        float seaCx = 0.f, seaCz = 0.f, westX = 1e9f, westZ = 0.f;
        int seaN = 0;
        const float searchR = p.lakeRadius * 1.25f;
        for (int j = 0; j < world.gridH; ++j)
            for (int i = 0; i < world.gridW; ++i) {
                if (world.heightmap[static_cast<std::size_t>(i) + world.gridW * j] >= p.waterLevel) continue;
                const float x = (static_cast<float>(i) / (g - 1) - 0.5f) * world.sizeX;
                const float z = (static_cast<float>(j) / (g - 1) - 0.5f) * world.sizeZ;
                if (std::hypot(x, z) > searchR) continue;
                seaCx += x; seaCz += z; ++seaN;
                if (x < westX) { westX = x; westZ = z; }
            }
        if (seaN > 0) { seaCx /= seaN; seaCz /= seaN; } else { westX = -p.lakeRadius; westZ = 0.f; }
        std::fprintf(stderr,
                     "worldgen_screenshot: tropical seed %u: sea center (%.1f, %.1f), west shore (%.1f, %.1f)\n",
                     args.seed, seaCx, seaCz, westX, westZ);

        // flatten a beach plateau for the fishing village just inland of the west waterline
        const fuhgl::math::Vec2 vc{westX - 15.f, westZ};
        const float vr = 20.f;
        const float plateauY = world.waterLevelWorld + 1.3f;
        const float plateauN = plateauY / world.heightScale;
        const auto ss = [](float e0, float e1, float x) {
            const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
            return t * t * (3.f - 2.f * t);
        };
        for (int j = 0; j < world.gridH; ++j)
            for (int i = 0; i < world.gridW; ++i) {
                const float x = (static_cast<float>(i) / (g - 1) - 0.5f) * world.sizeX;
                const float z = (static_cast<float>(j) / (g - 1) - 0.5f) * world.sizeZ;
                const float d = std::hypot(x - vc.x, z - vc.y);
                const float t = 1.f - ss(vr * 0.55f, vr, d);
                float& h = world.heightmap[static_cast<std::size_t>(i) + g * j];
                h = h * (1.f - t) + plateauN * t;
            }
        // keep the village disc clear of scatter, then re-scatter clutter on the final terrain
        const auto inVillage = [&](const fuhgl::math::Vec3& pos) {
            return std::hypot(pos.x - vc.x, pos.z - vc.y) < vr + 5.f;
        };
        const auto notInVillage = [&](const procedural::PlacedMesh& pm) { return !inVillage(pm.position); };
        world.trees.erase(std::remove_if(world.trees.begin(), world.trees.end(), notInVillage),
                          world.trees.end());
        world.bushes.erase(std::remove_if(world.bushes.begin(), world.bushes.end(), notInVillage),
                           world.bushes.end());
        world.rocks.erase(std::remove_if(world.rocks.begin(), world.rocks.end(), notInVillage),
                          world.rocks.end());
        // keep trees clear of the camera line (a tree crossing the near plane renders as a
        // green star blob); the beach camera sits west of the village looking east
        const float camX = vc.x - 30.f, camZ = vc.y + 10.f;
        world.trees.erase(std::remove_if(world.trees.begin(), world.trees.end(),
                                         [&](const procedural::PlacedMesh& pm) {
                                             return std::hypot(pm.position.x - camX,
                                                               pm.position.z - camZ) < 9.f;
                                         }),
                          world.trees.end());
        std::vector<fuhgl::math::Vec2> clutterBuildings{{vc.x, vc.y}};
        const std::vector<procedural::ClutterItem> clutter =
            rescatterClutter(world, nullptr, world.trees, clutterBuildings, 600, args.seed ^ 0xC17u);

        scene::Scene scene;
        MeshArena arena;
        const fuhgl::Texture splatTex = makeSplatAlbedoTex(world);
        scene::Node& tn = scene.root().createChild("terrain");
        fuhgl::render::Mesh terrainMesh;
        terrainMesh.setMeshData(terrainMeshData(world));
        {
            scene::MeshEntry& te = tn.attach<scene::MeshComponent>().addMesh(&terrainMesh);
            te.albedoTexture = &splatTex;
            te.roughness = 1.f;
            te.castShadow = true;
            te.materialId = 1;
        }

        // the sea: one big tropical water surface (turquoise shallows)
        scene::Node& wn = scene.root().createChild("sea");
        wn.translation = {0.f, world.waterLevelWorld, 0.f};
        scene::WaterComponent* water = &wn.attach<scene::WaterComponent>();
        if (!water->surface.create(200, p.worldSize * 1.2f)) {
            std::fprintf(stderr, "worldgen_screenshot: sea create failed\n");
            return 1;
        }
        water->surface.waves = {
            {{1.f, 0.20f}, 0.20f, 18.f, 2.2f, 0.60f},
            {{0.6f, 0.8f}, 0.12f, 9.f, 1.5f, 0.50f},
            {{-0.4f, 0.9f}, 0.07f, 5.f, 1.1f, 0.40f},
        };
        water->surface.reflectionStrength = 0.85f;
        water->surface.foamStrength = 0.8f;
        water->surface.deepColor = {0.004f, 0.10f, 0.16f};
        water->surface.shallowColor = {0.06f, 0.45f, 0.48f};

        // instanced grass, tropical green, masked by the splatmap like the other shots
        {
            scene::Node& grass = scene.root().createChild("grass");
            grass.instancing.enabled = true;
            grass.instancing.count = p.grassBlades;
            grass.instancing.layout = scene::InstanceLayout::Random;
            grass.instancing.extent = {p.worldSize - 8.f, 4.f, p.worldSize - 8.f};
            grass.instancing.seed = 7u;
            grass.instancing.heightFn = [&world](float x, float z) {
                const float h = world.heightAt(x, z);
                if (h < world.waterLevelWorld + 0.15f) return -1000.f;
                const int gg = world.gridW;
                const float u = std::min(1.f, std::max(0.f, x / world.sizeX + 0.5f)) * (gg - 1);
                const float v = std::min(1.f, std::max(0.f, z / world.sizeZ + 0.5f)) * (gg - 1);
                const std::size_t idx = (static_cast<std::size_t>(u) + gg * static_cast<std::size_t>(v)) * 4;
                if (world.splatmap[idx + 0] < 110) return -1000.f;
                return h + 0.22f;
            };
            scene::MeshEntry& ge = grass.attach<scene::MeshComponent>().addMesh(
                arena.add(fuhgl::render::shapes::boxData({0.015f, 0.22f, 0.015f}, {0.30f, 0.55f, 0.20f})));
            ge.albedo = {0.50f, 0.78f, 0.28f};
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
        attachClutter(scene, arena, clutter);

        // the fishing village: rustic Village-style houses on the plateau, smaller huts down
        // at the beach strip; the BuildingContext comes from the shore climate maps
        procedural::BuildingContext vctx;
        {
            const auto sampleF = [&](const std::vector<float>& m) -> float {
                if (m.empty()) return 0.5f;
                const float u = std::min(1.f, std::max(0.f, vc.x / world.sizeX + 0.5f)) * (g - 1);
                const float v = std::min(1.f, std::max(0.f, vc.y / world.sizeZ + 0.5f)) * (g - 1);
                return m[static_cast<std::size_t>(u) + g * static_cast<std::size_t>(v)];
            };
            vctx.temperature = sampleF(world.temperature);
            vctx.humidity = sampleF(world.humidity);
            vctx.precipitation = std::min(1.f, vctx.humidity * 0.95f);
            vctx.biome = world.biome.empty() ? -1
                : static_cast<int>(world.biome[static_cast<std::size_t>(
                      std::min(1.f, std::max(0.f, vc.x / world.sizeX + 0.5f)) * (g - 1)) +
                    g * static_cast<std::size_t>(
                      std::min(1.f, std::max(0.f, vc.y / world.sizeZ + 0.5f)) * (g - 1))]);
            vctx.wealth = 0.4f;   // poor fishing village
            vctx.density = 0.25f;
            vctx.soilColor = {0.72f, 0.62f, 0.44f}; // sandy soil
        }
        std::fprintf(stderr, "worldgen_screenshot: tropical beach climate: T=%.2f H=%.2f biome=%d\n",
                     vctx.temperature, vctx.humidity, vctx.biome);
        procedural::HouseGenerator houseGen;
        std::uint32_t hs = args.seed ^ 0x11EAu;
        auto nextr = [&]() {
            hs = hs * 1664525u + 1013904223u;
            return static_cast<float>(hs >> 8) * (1.f / 16777216.f);
        };
        int houseCount = 0;
        auto placeHouse = [&](const procedural::HouseMesh& hm, float x, float y, float z, float rotY) {
            scene::Node& n = scene.root().createChild("fhouse" + std::to_string(houseCount++));
            n.translation = {x, y, z};
            n.rotation = fuhgl::math::fromAxisAngle(rotY, {0.f, 1.f, 0.f});
            scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
            fuhgl::math::Vec3 facTint;
            const FacadeTexSet* ft =
                facadeSet(static_cast<int>(hm.spec.facade), plasterTex, stoneTex, facTint);
            scene::MeshEntry& walls = mc.addMesh(arena.add(hm.walls));
            walls.albedo = facTint;
            walls.albedoTexture = ft ? &ft->albedo : &brickAlbedo;
            walls.normalMap = ft ? &ft->normal : &brickNormal;
            walls.heightMap = ft ? &ft->height : &brickHeight; // POM parallax on the facade
            walls.parallaxScale = 0.05f;
            walls.parallaxLayers = 24;
            walls.roughness = 0.85f;
            walls.castShadow = true;
            walls.materialId = 5;
            if (hm.glass.vertexCount() > 0) {
                scene::MeshEntry& gl = mc.addMesh(arena.add(hm.glass));
                setupGlassEntry(gl);
                gl.emissive = {1.6f, 0.9f, 0.4f}; // warm lamplight in the windows at dusk
            }
            if (hm.details.vertexCount() > 0) {
                scene::MeshEntry& det = mc.addMesh(arena.add(hm.details));
                det.albedo = {1.4f, 1.4f, 1.4f};
                det.roughness = 0.4f;
                det.castShadow = true;
                det.materialId = 8;
            }
            if (hm.interior.vertexCount() > 0) { // rooms behind the real wall openings
                scene::MeshEntry& im = mc.addMesh(arena.add(hm.interior));
                im.albedo = {1.2f, 1.2f, 1.2f};
                im.roughness = 0.9f;
                im.castShadow = true;
                im.materialId = 8;
            }
            if (hm.doors.vertexCount() > 0) { // hinged door leaves
                scene::MeshEntry& dm = mc.addMesh(arena.add(hm.doors));
                dm.albedo = {1.3f, 1.3f, 1.3f};
                dm.roughness = 0.5f;
                dm.castShadow = true;
                dm.materialId = 8;
            }
            if (hm.roof.vertexCount() > 0) {
                scene::MeshEntry& roof = mc.addMesh(arena.add(hm.roof));
                roof.albedoTexture = &shingle;
                roof.albedo = {1.6f, 1.6f, 1.6f};
                roof.roughness = 0.7f;
                roof.castShadow = true;
                roof.materialId = 6;
            }
        };
        for (int gz = -2; gz <= 2; ++gz)
            for (int gx = -1; gx <= 2; ++gx) {
                if (nextr() < 0.3f) continue;
                const float hx = vc.x + gx * 7.f + (nextr() - 0.5f) * 2.f;
                const float hz = vc.y + gz * 7.f + (nextr() - 0.5f) * 2.f;
                if (std::hypot(hx - vc.x, hz - vc.y) > vr - 4.f) continue;
                const float groundY = world.heightAt(hx, hz);
                if (groundY < world.waterLevelWorld + 0.8f) continue; // keep houses on dry sand
                const bool beachHut = hx > vc.x + vr * 0.5f; // small huts down at the waterline
                const std::uint32_t hseed =
                    args.seed ^ (0x5EEDu + static_cast<std::uint32_t>(houseCount) * 7919u);
                auto hp = procedural::HouseGenerator::randomParams(procedural::HouseStyle::Village,
                                                                    8.f, hseed, vctx);
                hp.seed = hseed;
                hp.width = beachHut ? 4.f + nextr() * 1.5f : 6.f + nextr() * 2.5f;
                hp.depth = beachHut ? 3.5f + nextr() * 1.5f : 5.f + nextr() * 2.5f;
                auto hm = houseGen.generate(hp);
                hm.spec.facade = procedural::FacadeMaterial::Plaster; // whitewashed tropical huts
                placeHouse(hm, hx, groundY, hz, nextr() * 2.f * fuhgl::math::kPi);
            }
        // a ring of broadleaf trees around the village clearing
        procedural::PlantGenerator plantGen;
        for (int t = 0; t < 16; ++t) {
            const float ang = nextr() * 2.f * fuhgl::math::kPi;
            const float rad = vr + 3.f + nextr() * 8.f;
            const float tx = vc.x + std::cos(ang) * rad, tz = vc.y + std::sin(ang) * rad;
            const float th = world.heightAt(tx, tz);
            if (th < world.waterLevelWorld + 0.3f) continue;
            procedural::PlantParams pp;
            pp.kind = procedural::PlantKind::Deciduous; // broadleaf
            pp.seed = args.seed ^ (0x7EAFu + static_cast<std::uint32_t>(t) * 131u);
            pp.scale = 0.7f + 0.6f * nextr();
            const auto pm = plantGen.generate(pp);
            scene::Node& n = scene.root().createChild("beachtree" + std::to_string(t));
            n.translation = {tx, th, tz};
            n.rotation = fuhgl::math::fromAxisAngle(nextr() * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
            n.scale = {pp.scale, pp.scale, pp.scale};
            scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(arena.add(pm.mesh));
            e.albedo = {0.85f, 1.f, 0.85f};
            e.roughness = 0.9f;
            e.castShadow = true;
            e.materialId = 30;
        }
        std::fprintf(stderr, "worldgen_screenshot: tropical seed %u: %d houses on the beach\n",
                     args.seed, houseCount);

        // G38 weather: sunset (low sun toward +X, over the sea) + light cloud cover; the
        // weather state drives the sun light, the procedural sky, volumetric clouds and fog
        scene::WeatherComponent& wcomp =
            scene.root().createChild("weather").attach<scene::WeatherComponent>();
        wcomp.system = weather::WeatherSystem(args.seed ^ 0x7A0Fu);
        wcomp.system.setTimeOfDay(17.9f); // sun ~1.6 deg above the horizon toward +X, over the sea
        weather::WeatherParams wp = weather::presetFor(weather::WeatherType::PartlyCloudy);
        wp.cloudCoverage = 0.42f;  // light cloud cover
        wp.cloudDensity = 0.65f;
        wp.fogDensity = 0.0012f;
        wp.sunVisibility = 1.f;
        wp.windSpeed = 2.5f;
        wcomp.system.setParams(wp);

        // warm sunset sky cubemap + IBL (the procedural G38 sky overrides the cubemap)
        fuhgl::Skybox sunsetSky;
        fuhgl::render::Environment envSunset;
        const fuhgl::render::Environment* envUse = &env;
        if (sunsetSky.create()) {
            sunsetSky.makeGradient({0.16f, 0.20f, 0.42f}, {1.0f, 0.52f, 0.22f}, 256);
            if (sunsetSky.texture().id() != 0) {
                renderer.setSkyCubemap(&sunsetSky.texture());
                if (envSunset.createFromCubemap(sunsetSky.texture(), 16, 32, 64)) {
                    envSunset.intensity = 0.55f;
                    envUse = &envSunset;
                }
            }
        }

        cam.fovY = fuhgl::math::radians(55.f);
        scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;
        scene.root().effects.enable(render::Effect::Taa);
        scene.root().effects.enable(render::Effect::Upscale);
        scene.root().effects.enable(render::Effect::Tonemap);
        scene.root().effects.enable(render::Effect::CsmShadows);
        scene.root().effects.enable(render::Effect::Ibl);
        scene.root().effects.enable(render::Effect::Sky);
        scene.root().effects.enable(render::Effect::VolumetricFog);
        scene.root().effects.enable(render::Effect::Water);
        scene.root().effects.enable(render::Effect::Bloom);
        enableWeathering(scene, plateauY, vctx.humidity, vctx.soilColor);
        scene.root().effects.params.exposure = 1.4f;
        scene.root().effects.params.taaBlend = 0.08f;
        scene.root().effects.params.shadowCenter = {vc.x, plateauY, vc.y};
        scene.root().effects.params.shadowRadius = 140.f;
        scene.root().effects.params.shadowZNear = 1.f;
        scene.root().effects.params.shadowZFar = 500.f;
        renderer.bloomThreshold = 0.85f; // the low sun disk + its halo glow
        renderer.bloomStrength = 0.9f;
        renderer.volumetricSteps = 24;
        renderer.volumetricMaxDist = 300.f;

        // beach vantage west of the village looking east and slightly down: village close
        // in the midground, beach in the foreground, sea and the low sun beyond
        cam.position = {vc.x - 16.f, plateauY + 8.f, vc.y + 12.f};
        aimAt(cam, {vc.x + 12.f, plateauY + 2.f, vc.y - 2.f});
        const std::string tPath = args.prefix + "_tropical_sunset.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, tPath,
                         &water->surface, envUse, &wcomp.system, 16.f)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", tPath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", tPath.c_str());
    }

    // --- panorama: big vegetated landscape + WFC city at a river + village at the lake,
    //     connected by a G38 least-cost road (bridges over water) ------------------------
    if (doPanorama) {
        procedural::WorldGenParams p;
        p.mode = procedural::WorldGenParams::Mode::Landscape;
        p.seed = args.seed;
        p.worldSize = 340.f;
        p.terrainGrid = 193;
        p.terrainHeight = 30.f;
        p.warpStrength = 0.6f;
        p.heightExponent = 0.85f;
        p.waterLevel = 0.30f;      // lake + river channels
        p.lakeRadius = 62.f;
        p.erosionDroplets = 4000;
        p.thermalPasses = 4;
        p.riverCount = 18;
        p.treeCount = 500;         // lots of vegetation
        p.bushCount = 600;
        p.rockCount = 90;
        p.grassBlades = 14000;
        p.treeMinDist = 4.5f;
        p.settlementCount = 0;     // city + village are placed explicitly below
        p.clutterCount = 0;        // G39a: re-scattered below on the flattened + road-carved terrain
        procedural::GeneratedWorld world = generateWorld(p);
        printLifeZones(world.lifeZones);
        const int g = world.gridW;
        const float waterN = p.waterLevel;
        std::fprintf(stderr,
                     "worldgen_screenshot: panorama seed %u: grid %d, %zu trees, %zu bushes, %zu rocks\n",
                     args.seed, g, world.trees.size(), world.bushes.size(), world.rocks.size());

        // central lake basin: center + south rim (same search as the other landscape shots)
        float lakeCx = 0.f, lakeCz = 0.f, lakeSouthZ = -1e9f;
        int lakeN = 0;
        const float lakeSearchR = p.lakeRadius * 1.25f;
        for (int j = 0; j < world.gridH; ++j)
            for (int i = 0; i < world.gridW; ++i) {
                if (world.heightmap[static_cast<std::size_t>(i) + world.gridW * j] < waterN) {
                    const float x = (static_cast<float>(i) / (g - 1) - 0.5f) * world.sizeX;
                    const float z = (static_cast<float>(j) / (g - 1) - 0.5f) * world.sizeZ;
                    if (std::hypot(x, z) > lakeSearchR) continue;
                    lakeCx += x;
                    lakeCz += z;
                    ++lakeN;
                    lakeSouthZ = std::max(lakeSouthZ, z);
                }
            }
        if (lakeN > 0) { lakeCx /= lakeN; lakeCz /= lakeN; } else { lakeSouthZ = world.sizeZ * 0.25f; }

        // city spot: riverbank land (just above the water level, next to a strong flow
        // channel) at a moderate distance from the lake -> city at the river and village at
        // the lake both fit into one vista frame, with the connecting road between them
        fuhgl::math::Vec2 cityC{world.sizeX * 0.30f, -world.sizeX * 0.28f};
        const float cityTargetDist = world.sizeX * 0.26f;
        float bestScore = 1e9f;
        for (int j = 2; j < g - 2; ++j)
            for (int i = 2; i < g - 2; ++i) {
                const float h = world.heightmap[static_cast<std::size_t>(i) + g * j];
                if (h < waterN + 0.02f || h > waterN + 0.16f) continue;
                float nf = 0.f;
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx)
                        nf = std::max(nf, world.flowmap[static_cast<std::size_t>(i + dx) + g * (j + dz)]);
                if (nf < 0.6f) continue;
                const float x = (static_cast<float>(i) / (g - 1) - 0.5f) * world.sizeX;
                const float z = (static_cast<float>(j) / (g - 1) - 0.5f) * world.sizeZ;
                const float score = std::fabs(std::hypot(x - lakeCx, z - lakeCz) - cityTargetDist);
                if (score < bestScore) { bestScore = score; cityC = {x, z}; }
            }

        // village at the lake's south shore
        const fuhgl::math::Vec2 vilC{lakeCx, lakeSouthZ + 12.f};
        const float vilR = 26.f, cityR = 52.f;
        const float vilPlateauY = world.waterLevelWorld + 1.4f;
        const float cityPlateauY = world.waterLevelWorld + 1.2f;

        // flatten both plateaus in the heightmap (terrain mesh, grass heightFn and the
        // camera all read the same heightmap, so they see the flat pads)
        const auto ss = [](float e0, float e1, float x) {
            const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
            return t * t * (3.f - 2.f * t);
        };
        auto flatten = [&](const fuhgl::math::Vec2& c, float r, float plateauY) {
            const float pn = plateauY / world.heightScale;
            for (int j = 0; j < g; ++j)
                for (int i = 0; i < g; ++i) {
                    const float x = (static_cast<float>(i) / (g - 1) - 0.5f) * world.sizeX;
                    const float z = (static_cast<float>(j) / (g - 1) - 0.5f) * world.sizeZ;
                    const float t = 1.f - ss(r * 0.55f, r, std::hypot(x - c.x, z - c.y));
                    float& h = world.heightmap[static_cast<std::size_t>(i) + g * j];
                    h = h * (1.f - t) + pn * t;
                }
        };
        flatten(cityC, cityR, cityPlateauY);
        flatten(vilC, vilR, vilPlateauY);

        // G38 road between city and village: route on the flattened terrain, carve the bench
        // into the hillsides, then re-flatten the pads so they stay clean
        procedural::RoadInput rin;
        rin.heightmap = &world.heightmap;
        rin.gridW = g;
        rin.gridH = g;
        rin.sizeX = world.sizeX;
        rin.sizeZ = world.sizeZ;
        rin.heightScale = world.heightScale;
        rin.waterLevel = waterN;
        rin.flow = &world.flowmap;
        rin.biome = world.biome.empty() ? nullptr : &world.biome;
        rin.sites = {{cityC, 1.0f, 0}, {vilC, 0.85f, 1}};
        procedural::RoadGenParams rp;
        rp.seed = args.seed;
        procedural::RoadGenerator rg;
        procedural::RoadNetwork net = rg.generate(rin, rp);
        rg.carveTerrain(world.heightmap, net, rp);
        flatten(cityC, cityR, cityPlateauY);
        flatten(vilC, vilR, vilPlateauY);
        rg.buildGeometry(net, rin, rp);
        std::fprintf(stderr, "worldgen_screenshot: panorama road: %zu paths, %zu props, %zu bridge verts\n",
                     net.paths.size(), net.props.size(), net.bridgeMesh.vertexCount());

        // drop the scatter inside both pads so houses/roads do not intersect trees
        const auto inPad = [&](const fuhgl::math::Vec3& pos) {
            return std::hypot(pos.x - cityC.x, pos.z - cityC.y) < cityR + 6.f ||
                   std::hypot(pos.x - vilC.x, pos.z - vilC.y) < vilR + 6.f;
        };
        const auto notInPad = [&](const procedural::PlacedMesh& pm) { return !inPad(pm.position); };
        world.trees.erase(std::remove_if(world.trees.begin(), world.trees.end(), notInPad), world.trees.end());
        world.bushes.erase(std::remove_if(world.bushes.begin(), world.bushes.end(), notInPad), world.bushes.end());
        world.rocks.erase(std::remove_if(world.rocks.begin(), world.rocks.end(), notInPad), world.rocks.end());

        // G39a: re-scatter the causal clutter on the final terrain (pads flattened, road bench
        // carved), using the road mask of the freshly routed city<->village road
        const std::vector<procedural::ClutterItem> clutter =
            rescatterClutter(world, &net.mask, world.trees, {{cityC.x, cityC.y}, {vilC.x, vilC.y}},
                             600, args.seed ^ 0xC17u);
        std::fprintf(stderr, "worldgen_screenshot: panorama clutter: %zu items\n", clutter.size());

        // WFC city for the riverbank pad
        procedural::WorldGenParams cp;
        cp.mode = procedural::WorldGenParams::Mode::City;
        cp.seed = args.seed ^ 0xC17u;
        cp.cityGrid = 10;
        cp.cellSize = 8.f;
        cp.buildingHeight = 8.f;
        cp.blocksPerDistrict = 20;
        cp.parkFraction = 0.15f;
        const procedural::GeneratedWorld city = procedural::generateWorld(cp);

        scene::Scene scene;
        MeshArena arena;
        const fuhgl::Texture splatTex = makeSplatAlbedoTex(world);
        scene::Node& tn = scene.root().createChild("terrain");
        fuhgl::render::Mesh terrainMesh;
        terrainMesh.setMeshData(terrainMeshData(world));
        {
            scene::MeshEntry& te = tn.attach<scene::MeshComponent>().addMesh(&terrainMesh);
            te.albedoTexture = &splatTex;
            te.roughness = 1.f;
            te.castShadow = true;
            te.materialId = 1;
        }
        scene.root().effects.enable(render::Effect::Water);
        scene::Node& wn = scene.root().createChild("water");
        wn.translation = {0.f, world.waterLevelWorld, 0.f};
        scene::WaterComponent* water = &wn.attach<scene::WaterComponent>();
        water->surface.create(160, p.worldSize * 1.1f);
        water->surface.waves = {
            {{1.f, 0.15f}, 0.32f, 16.f, 2.0f, 0.65f},
            {{0.7f, 0.7f}, 0.20f, 8.f, 1.4f, 0.55f},
            {{-0.5f, 0.86f}, 0.11f, 4.5f, 1.0f, 0.45f},
        };
        water->surface.reflectionStrength = 0.8f;
        water->surface.foamStrength = 1.f;
        water->surface.deepColor = {0.004f, 0.05f, 0.11f};
        water->surface.shallowColor = {0.06f, 0.30f, 0.38f};
        if (p.grassBlades > 0) {
            scene::Node& grass = scene.root().createChild("grass");
            grass.instancing.enabled = true;
            grass.instancing.count = p.grassBlades;
            grass.instancing.layout = scene::InstanceLayout::Random;
            grass.instancing.extent = {p.worldSize - 8.f, 4.f, p.worldSize - 8.f};
            grass.instancing.seed = 7u;
            grass.instancing.heightFn = [&world](float x, float z) {
                const float h = world.heightAt(x, z);
                if (h < world.waterLevelWorld + 0.15f) return -1000.f;
                const int gg = world.gridW;
                const float u = std::min(1.f, std::max(0.f, x / world.sizeX + 0.5f)) * (gg - 1);
                const float v = std::min(1.f, std::max(0.f, z / world.sizeZ + 0.5f)) * (gg - 1);
                const std::size_t idx = (static_cast<std::size_t>(u) + gg * static_cast<std::size_t>(v)) * 4;
                if (world.splatmap[idx + 0] < 110) return -1000.f;
                return h + 0.22f;
            };
            scene::MeshEntry& ge = grass.attach<scene::MeshComponent>().addMesh(
                arena.add(fuhgl::render::shapes::boxData({0.015f, 0.22f, 0.015f}, {0.25f, 0.45f, 0.18f})));
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
        attachClutter(scene, arena, clutter); // G39a causal clutter, one baked mesh

        // G38 connecting road: ribbons (vertex colors), bridge decks over water, props
        if (net.roadMesh.vertexCount() > 0) {
            scene::Node& rn = scene.root().createChild("road");
            scene::MeshEntry& re = rn.attach<scene::MeshComponent>().addMesh(arena.add(net.roadMesh));
            re.albedo = {1.f, 1.f, 1.f};
            re.roughness = 0.95f;
            re.castShadow = true;
            re.materialId = 1;
        }
        if (net.bridgeMesh.vertexCount() > 0) {
            scene::Node& bn = scene.root().createChild("bridge");
            scene::MeshEntry& be = bn.attach<scene::MeshComponent>().addMesh(arena.add(net.bridgeMesh));
            be.albedo = {1.f, 1.f, 1.f};
            be.roughness = 0.8f;
            be.castShadow = true;
            be.materialId = 1;
        }
        {
            std::vector<procedural::PlacedMesh> props;
            for (const auto& pr : net.props) {
                procedural::PlacedMesh pm;
                pm.mesh = pr.mesh;
                pm.position = pr.position;
                pm.rotY = pr.rotY;
                props.push_back(std::move(pm));
            }
            addPlant("roadprop", props, 33);
        }

        // the WFC city on its riverbank pad (city-local coords are centered at the origin
        // with the ground top at y=0 -> one parent node carries the world offset).
        // NOTE: roadTex must outlive the inner scope — MeshEntries keep &roadTex.albedo
        const RoadTexSet roadTex = makeRoadTexSet(city.roadSurface, 256);
        {
            scene::Node& cn = scene.root().createChild("city");
            cn.translation = {cityC.x, cityPlateauY, cityC.y};
            auto cityMesh = [&](const char* name, const fuhgl::render::MeshData& md,
                                const fuhgl::math::Vec3& albedo, float rough, std::uint32_t mid,
                                bool shadow) {
                scene::Node& n = cn.createChild(name);
                scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(arena.add(md));
                e.albedo = albedo;
                e.roughness = rough;
                e.materialId = mid;
                e.castShadow = shadow;
            };
            cityMesh("ground", city.cityGround, {1.f, 1.f, 1.f}, 1.f, 2, false);
            cityMesh("pavements", city.pavements, {1.f, 1.f, 1.f}, 1.f, 2, false);
            if (city.districts.vertexCount() > 0)
                cityMesh("districts", city.districts, {1.f, 1.f, 1.f}, 1.f, 4, false);
            {
                scene::Node& rn = cn.createChild("roads");
                scene::MeshEntry& re = rn.attach<scene::MeshComponent>().addMesh(arena.add(city.roads));
                setupRoadEntry(re, roadTex.albedo, roadTex.height, roadTex.normal, roadTex.pom, city.wetRoads);
            }
            for (std::size_t i = 0; i < city.buildings.size(); ++i) {
                const auto& b = city.buildings[i];
                scene::Node& n = cn.createChild("house" + std::to_string(i));
                n.translation = b.position;
                n.rotation = fuhgl::math::fromAxisAngle(b.rotY, {0.f, 1.f, 0.f});
                n.scale = {b.scale, b.scale, b.scale};
                scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
                fuhgl::math::Vec3 facTint;
                const FacadeTexSet* ft = facadeSet(b.facade, plasterTex, stoneTex, facTint);
                scene::MeshEntry& walls = mc.addMesh(arena.add(b.mesh));
                walls.albedo = facTint;
                walls.albedoTexture = ft ? &ft->albedo : &brickAlbedo;
                walls.normalMap = ft ? &ft->normal : &brickNormal;
                walls.heightMap = ft ? &ft->height : &brickHeight;
                walls.parallaxScale = 0.05f;
                walls.parallaxLayers = 24;
                walls.roughness = 0.85f;
                walls.castShadow = true;
                walls.materialId = 5;
                if (b.glassMesh.vertexCount() > 0) {
                    scene::MeshEntry& gl = mc.addMesh(arena.add(b.glassMesh));
                    setupGlassEntry(gl);
                }
                if (b.detailMesh.vertexCount() > 0) {
                    scene::MeshEntry& det = mc.addMesh(arena.add(b.detailMesh));
                    det.albedo = {1.4f, 1.4f, 1.4f};
                    det.roughness = 0.4f;
                    det.castShadow = true;
                    det.materialId = 8;
                }
                if (b.interiorMesh.vertexCount() > 0) { // rooms behind the real wall openings
                    scene::MeshEntry& im = mc.addMesh(arena.add(b.interiorMesh));
                    im.albedo = {1.2f, 1.2f, 1.2f};
                    im.roughness = 0.9f;
                    im.castShadow = true;
                    im.materialId = 8;
                }
                if (b.doorMesh.vertexCount() > 0) { // hinged door leaves
                    scene::MeshEntry& dm = mc.addMesh(arena.add(b.doorMesh));
                    dm.albedo = {1.3f, 1.3f, 1.3f};
                    dm.roughness = 0.5f;
                    dm.castShadow = true;
                    dm.materialId = 8;
                }
                if (b.roofMesh.vertexCount() > 0) {
                    scene::MeshEntry& roof = mc.addMesh(arena.add(b.roofMesh));
                    roof.albedoTexture = &shingle;
                    roof.albedo = {1.6f, 1.6f, 1.6f};
                    roof.roughness = 0.7f;
                    roof.castShadow = true;
                    roof.materialId = 6;
                }
            }
            for (std::size_t i = 0; i < city.bushes.size(); ++i) {
                const auto& b = city.bushes[i];
                scene::Node& n = cn.createChild("parkbush" + std::to_string(i));
                n.translation = b.position;
                n.rotation = fuhgl::math::fromAxisAngle(b.rotY, {0.f, 1.f, 0.f});
                n.scale = {b.scale, b.scale, b.scale};
                scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(arena.add(b.mesh));
                e.albedo = {0.8f, 1.f, 0.8f};
                e.roughness = 0.9f;
                e.castShadow = true;
                e.materialId = 7;
            }
        }

        // the village at the lake shore: loose grid of HouseGenerator village houses
        {
            procedural::BuildingContext vctx;
            const int gg = world.gridW;
            const float u = std::min(1.f, std::max(0.f, vilC.x / world.sizeX + 0.5f)) * (gg - 1);
            const float v = std::min(1.f, std::max(0.f, vilC.y / world.sizeZ + 0.5f)) * (gg - 1);
            const std::size_t idx = static_cast<std::size_t>(u) + gg * static_cast<std::size_t>(v);
            vctx.temperature = world.temperature.empty() ? 0.6f : world.temperature[idx];
            vctx.humidity = world.humidity.empty() ? 0.5f : world.humidity[idx];
            vctx.precipitation = std::min(1.f, vctx.humidity * 0.95f);
            vctx.biome = world.biome.empty() ? -1 : static_cast<int>(world.biome[idx]);
            vctx.wealth = 0.55f;
            vctx.density = 0.25f;
            vctx.waterProximity = 1.f; // right at the lake shore
            vctx.soilColor = {0.42f, 0.38f, 0.26f};
            procedural::HouseGenerator houseGen;
            std::uint32_t hs = args.seed ^ 0x11EAu;
            auto nextr = [&]() {
                hs = hs * 1664525u + 1013904223u;
                return static_cast<float>(hs >> 8) * (1.f / 16777216.f);
            };
            int houseCount = 0;
            for (int gz = -2; gz <= 2; ++gz)
                for (int gx = -3; gx <= 3; ++gx) {
                    if (nextr() < 0.3f) continue;
                    const float hx = vilC.x + gx * 9.f + (nextr() - 0.5f) * 2.f;
                    const float hz = vilC.y + gz * 9.f + (nextr() - 0.5f) * 2.f;
                    if (std::hypot(hx - vilC.x, hz - vilC.y) > vilR - 6.f) continue;
                    const std::uint32_t hseed =
                        args.seed ^ (0x5EEDu + static_cast<std::uint32_t>(houseCount) * 7919u);
                    auto hp = procedural::HouseGenerator::randomParams(
                        procedural::HouseStyle::Village, 8.f, hseed, vctx);
                    hp.seed = hseed;
                    hp.width = 5.f + nextr() * 2.5f;
                    hp.depth = 5.f + nextr() * 2.5f;
                    const auto hm = houseGen.generate(hp);
                    scene::Node& n = scene.root().createChild("vhouse" + std::to_string(houseCount));
                    n.translation = {hx, vilPlateauY, hz};
                    n.rotation = fuhgl::math::fromAxisAngle(nextr() * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
                    scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
                    fuhgl::math::Vec3 facTint;
                    const FacadeTexSet* ft =
                        facadeSet(static_cast<int>(hm.spec.facade), plasterTex, stoneTex, facTint);
                    scene::MeshEntry& walls = mc.addMesh(arena.add(hm.walls));
                    walls.albedo = facTint;
                    walls.albedoTexture = ft ? &ft->albedo : &brickAlbedo;
                    walls.normalMap = ft ? &ft->normal : &brickNormal;
                    walls.heightMap = ft ? &ft->height : &brickHeight;
                    walls.parallaxScale = 0.05f;
                    walls.parallaxLayers = 24;
                    walls.roughness = 0.85f;
                    walls.castShadow = true;
                    walls.materialId = 5;
                    if (hm.glass.vertexCount() > 0) {
                        scene::MeshEntry& gl = mc.addMesh(arena.add(hm.glass));
                        setupGlassEntry(gl);
                    }
                    if (hm.details.vertexCount() > 0) {
                        scene::MeshEntry& det = mc.addMesh(arena.add(hm.details));
                        det.albedo = {1.4f, 1.4f, 1.4f};
                        det.roughness = 0.4f;
                        det.castShadow = true;
                        det.materialId = 8;
                    }
                    if (hm.interior.vertexCount() > 0) { // rooms behind the real wall openings
                        scene::MeshEntry& im = mc.addMesh(arena.add(hm.interior));
                        im.albedo = {1.2f, 1.2f, 1.2f};
                        im.roughness = 0.9f;
                        im.castShadow = true;
                        im.materialId = 8;
                    }
                    if (hm.doors.vertexCount() > 0) { // hinged door leaves
                        scene::MeshEntry& dm = mc.addMesh(arena.add(hm.doors));
                        dm.albedo = {1.3f, 1.3f, 1.3f};
                        dm.roughness = 0.5f;
                        dm.castShadow = true;
                        dm.materialId = 8;
                    }
                    if (hm.roof.vertexCount() > 0) {
                        scene::MeshEntry& roof = mc.addMesh(arena.add(hm.roof));
                        roof.albedoTexture = &shingle;
                        roof.albedo = {1.6f, 1.6f, 1.6f};
                        roof.roughness = 0.7f;
                        roof.castShadow = true;
                        roof.materialId = 6;
                    }
                    ++houseCount;
                }
        }

        // vista camera: on the perpendicular bisector of the city-village axis, pulled back
        // far enough that BOTH endpoints stay inside the horizontal half-FOV (~48 deg at
        // 16:9) — city at the river on one side, village at the lake on the other, the
        // connecting road across the middle ground
        const fuhgl::math::Vec2 d{vilC.x - cityC.x, vilC.y - cityC.y};
        const float dl = std::hypot(d.x, d.y);
        const fuhgl::math::Vec2 dn{d.x / dl, d.y / dl};
        const fuhgl::math::Vec2 perp{-dn.y, dn.x};
        const fuhgl::math::Vec2 mid{(cityC.x + vilC.x) * 0.5f, (cityC.y + vilC.y) * 0.5f};
        const float back = std::max(95.f, dl * 0.62f); // atan(dl/2 / back) < ~48 deg
        const float camX = mid.x + perp.x * back;
        const float camZ = mid.y + perp.y * back;
        cam.position = {camX, std::max(world.heightAt(camX, camZ), world.waterLevelWorld) + 38.f, camZ};
        aimAt(cam, {mid.x, world.waterLevelWorld + 4.f, mid.y});
        addSunAndSky(scene, cam, fuhgl::math::normalize(fuhgl::math::Vec3{-0.35f, 0.62f, 0.45f}));
        const std::string ppath = args.prefix + "_panorama.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, ppath,
                         water ? &water->surface : nullptr, &env)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", ppath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", ppath.c_str());
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
        for (std::size_t i = 0; i < world.buildings.size(); ++i) {
            const auto& b = world.buildings[i];
            scene::Node& n = scene.root().createChild("house" + std::to_string(i));
            n.translation = b.position;
            n.rotation = fuhgl::math::fromAxisAngle(b.rotY, {0.f, 1.f, 0.f});
            n.scale = {b.scale, b.scale, b.scale};
            scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
            fuhgl::math::Vec3 facTint;
            const FacadeTexSet* ft = facadeSet(b.facade, plasterTex, stoneTex, facTint);
            scene::MeshEntry& walls = mc.addMesh(arena.add(b.mesh));
            walls.albedo = facTint;
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
            if (b.interiorMesh.vertexCount() > 0) { // rooms behind the real wall openings
                scene::MeshEntry& im = mc.addMesh(arena.add(b.interiorMesh));
                im.albedo = {1.2f, 1.2f, 1.2f};
                im.roughness = 0.9f;
                im.castShadow = true;
                im.materialId = 8;
            }
            if (b.doorMesh.vertexCount() > 0) { // hinged door leaves
                scene::MeshEntry& dm = mc.addMesh(arena.add(b.doorMesh));
                dm.albedo = {1.3f, 1.3f, 1.3f};
                dm.roughness = 0.5f;
                dm.castShadow = true;
                dm.materialId = 8;
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
        enableWeathering(scene, 0.05f, 0.55f, {0.36f, 0.29f, 0.21f}); // houses sit on the y=0 plate

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
        p.houseClutter = 0.35f; // thousands of houses: keep the prop density moderate
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
        enableWeathering(scene, world.waterLevelWorld * 1.05f, 0.55f, {0.36f, 0.29f, 0.21f}); // city plateau

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
            cam.position = {nd.x, gy + 3.2f, nd.y + 9.f};
            aimAt(cam, {0.f, world.heightAt(0.f, 0.f) + 2.2f, 0.f});
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
