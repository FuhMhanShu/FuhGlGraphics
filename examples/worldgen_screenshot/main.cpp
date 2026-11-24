// G31 world-generator screenshot tool: builds a LANDSCAPE and a CITY with
// procedural::generateWorld() (WorldGenParams) and attaches the result to a scene::Scene
// (heightmap terrain + water surface + instanced grass + L-system trees/bushes; city ground,
// sidewalk/asphalt roads, voronoi district plates, procedural houses with POM brick walls +
// shingle roofs, park lawns with bushes), rendered headless (surfaceless EGL) to PNG for
// docs/images/.
//
// Usage: fuhglgraphics_worldgen_screenshot --prefix g31_worldgen [--width 1280] [--height 720]
//          [--frames 16] [--seed 777] [--mode both|landscape|city]
//   writes <prefix>_landscape.png, <prefix>_city.png and <prefix>_city_street.png
//   (skipping the shots of a mode not selected).
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>
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
    std::string mode = "both"; // both | landscape | city
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
    }
    return a;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

bool renderToPng(fuhglgraphics::scene::Scene& scn, fuhgl::FirstPersonCamera& cam,
                 fuhglgraphics::render::DeferredRenderer& renderer, int frames, int width, int height,
                 const std::string& outPath, fuhglgraphics::water::WaterSurface* water = nullptr) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = {0.80f, 0.83f, 0.90f};
        frame.view.fogDensity = 0.0012f;
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

fuhgl::math::Vec3 lerpColor(const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// heightmap (gridW*gridH, [0,1], row-major) -> terrain MeshData (11 floats/vertex),
// colored by normalized height: lake bed / sand / grass / rock / snow
fuhgl::render::MeshData heightmapMesh(const fuhglgraphics::procedural::GeneratedWorld& w) {
    fuhgl::render::MeshData md;
    const int g = w.gridW;
    const float cell = w.sizeX / static_cast<float>(g - 1);
    const float water = w.waterLevelWorld / w.heightScale;
    md.vertices.reserve(static_cast<std::size_t>(g) * g * 11);
    auto sample = [&](int i, int j) {
        i = std::min(g - 1, std::max(0, i));
        j = std::min(g - 1, std::max(0, j));
        return w.heightmap[static_cast<std::size_t>(i) + g * j];
    };
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(g - 1);
            const float v = static_cast<float>(j) / static_cast<float>(g - 1);
            const float h = sample(i, j);
            const float px = (u - 0.5f) * w.sizeX;
            const float pz = (v - 0.5f) * w.sizeZ;
            const float py = h * w.heightScale;
            const float slope = w.heightScale / (2.f * cell); // world-units rise per world-unit x/z
            const fuhgl::math::Vec3 n = fuhgl::math::normalize(
                {(sample(i - 1, j) - sample(i + 1, j)) * slope, 1.f,
                 (sample(i, j - 1) - sample(i, j + 1)) * slope});
            fuhgl::math::Vec3 c;
            if (h < water) c = lerpColor({0.23f, 0.30f, 0.38f}, {0.76f, 0.70f, 0.50f}, h / std::max(water, 1e-5f));
            else if (h < 0.42f) c = lerpColor({0.76f, 0.70f, 0.50f}, {0.24f, 0.42f, 0.18f}, (h - water) / std::max(0.42f - water, 1e-5f));
            else if (h < 0.72f) c = lerpColor({0.24f, 0.42f, 0.18f}, {0.45f, 0.44f, 0.42f}, (h - 0.42f) / 0.30f);
            else c = lerpColor({0.45f, 0.44f, 0.42f}, {0.90f, 0.92f, 0.95f}, (h - 0.72f) / 0.28f);
            md.vertices.push_back(px); md.vertices.push_back(py); md.vertices.push_back(pz);
            md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
            md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
            md.vertices.push_back(u); md.vertices.push_back(v);
        }
    for (int j = 0; j < g - 1; ++j)
        for (int i = 0; i < g - 1; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(i + g * j);
            const std::uint32_t b = a + 1, c = a + g, d = c + 1;
            md.indices.push_back(a); md.indices.push_back(c); md.indices.push_back(b);
            md.indices.push_back(b); md.indices.push_back(c); md.indices.push_back(d);
        }
    return md;
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
    std::vector<float> h(static_cast<std::size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            brickInfo(x, y, size, h[static_cast<std::size_t>(y) * size + x]);
    return makeTex(size, 37u, [&](int x, int y, float, unsigned char* o) {
        const auto at = [&](int xx, int yy) {
            return h[static_cast<std::size_t>((yy + size) % size) * size + ((xx + size) % size)];
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
    scene.root().effects.params.exposure = 1.05f;
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
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "worldgen_screenshot: skybox create failed\n");
    const fuhgl::math::Vec3 skyTop{0.20f, 0.38f, 0.72f}, skyHorizon{0.82f, 0.85f, 0.92f};
    skybox.makeGradient(skyTop, skyHorizon, 256);
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    const fuhgl::Texture brickAlbedo = makeBrickAlbedo(256);
    const fuhgl::Texture brickNormal = makeBrickNormal(256);
    const fuhgl::Texture brickHeight = makeBrickHeight(256);
    const fuhgl::Texture shingle = makeShingleTex(256);

    fuhgl::FirstPersonCamera cam;
    const bool doLandscape = args.mode == "both" || args.mode == "landscape";
    const bool doCity = args.mode == "both" || args.mode == "city";

    // --- landscape: generateWorld(Landscape) -> terrain + water + grass + trees + bushes ---
    if (doLandscape) {
        procedural::WorldGenParams p;
        p.mode = procedural::WorldGenParams::Mode::Landscape;
        p.seed = args.seed;
        p.worldSize = 200.f;
        p.terrainGrid = 129;
        p.terrainHeight = 26.f;   // pronounced mountains
        p.warpStrength = 1.2f;
        p.heightExponent = 0.75f;
        p.waterLevel = 0.30f;     // lake
        p.treeCount = 130;
        p.bushCount = 180;
        p.grassBlades = 4000;
        p.treeMinDist = 6.f;
        const procedural::GeneratedWorld world = procedural::generateWorld(p);
        std::fprintf(stderr, "worldgen_screenshot: landscape seed %u: grid %d, %zu trees, %zu bushes\n",
                     args.seed, world.gridW, world.trees.size(), world.bushes.size());

        scene::Scene scene;
        MeshArena arena;
        addMeshNode(scene, arena, "terrain", heightmapMesh(world), {1.f, 1.f, 1.f}, 0.95f, 1, false);

        // lake water surface at the water level
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

        // instanced grass snapped onto the terrain (hidden under water)
        {
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
                return h < world.waterLevelWorld + 0.15f ? -1000.f : h + 0.22f;
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
        addSunAndSky(scene, cam, fuhgl::math::normalize(fuhgl::math::Vec3{0.35f, 0.5f, 0.6f}));

        // high oblique overview: mountains behind, lake + meadow + forest in front, sky above
        cam.position = {0.f, 70.f, 160.f};
        aimAt(cam, {0.f, 6.f, 0.f});
        const std::string path = args.prefix + "_landscape.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, path,
                         water ? &water->surface : nullptr)) {
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
        p.cityGrid = 12;
        p.cellSize = 8.f;
        p.buildingHeight = 8.f;
        p.blocksPerDistrict = 28;
        const procedural::GeneratedWorld world = procedural::generateWorld(p);
        std::fprintf(stderr, "worldgen_screenshot: city seed %u: %dx%d cells, %zu houses, %zu park bushes\n",
                     args.seed, world.cityW, world.cityH, world.buildings.size(), world.bushes.size());

        scene::Scene scene;
        MeshArena arena;
        addMeshNode(scene, arena, "ground", world.cityGround, {1.f, 1.f, 1.f}, 1.f, 2, false);
        addMeshNode(scene, arena, "roads", world.roads, {1.f, 1.f, 1.f}, 0.9f, 3, false);
        if (world.districts.vertexCount() > 0)
            addMeshNode(scene, arena, "districts", world.districts, {1.f, 1.f, 1.f}, 1.f, 4, false);

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

        // houses: walls entry with POM brick material, roof entry with shingle texture
        for (std::size_t i = 0; i < world.buildings.size(); ++i) {
            const auto& b = world.buildings[i];
            scene::Node& n = scene.root().createChild("house" + std::to_string(i));
            n.translation = b.position;
            n.rotation = fuhgl::math::fromAxisAngle(b.rotY, {0.f, 1.f, 0.f});
            n.scale = {b.scale, b.scale, b.scale};
            scene::MeshComponent& mc = n.attach<scene::MeshComponent>();
            scene::MeshEntry& walls = mc.addMesh(arena.add(b.mesh));
            walls.albedo = {1.f, 1.f, 1.f};
            walls.albedoTexture = &brickAlbedo;
            walls.normalMap = &brickNormal;
            walls.heightMap = &brickHeight; // POM parallax on the facade
            walls.parallaxScale = 0.05f;
            walls.parallaxLayers = 24;
            walls.roughness = 0.85f;
            walls.castShadow = true;
            walls.materialId = 5;
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

        // oblique overview of the whole WFC layout
        cam.position = {0.f, 80.f, 100.f};
        aimAt(cam, {0.f, 0.f, 0.f});
        const std::string ovPath = args.prefix + "_city.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, ovPath)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", ovPath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s\n", ovPath.c_str());

        // street-level view from the first street cell toward the city center
        cam.position = {streetSpot.x, 2.4f, streetSpot.z + world.cityCellSize * 2.2f};
        aimAt(cam, {streetSpot.x * 0.3f, 2.2f, streetSpot.z * 0.3f});
        const std::string stPath = args.prefix + "_city_street.png";
        if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, stPath)) {
            std::fprintf(stderr, "worldgen_screenshot: failed to write %s\n", stPath.c_str());
            return 1;
        }
        std::fprintf(stderr, "worldgen_screenshot: wrote %s (GL %d.%d %s)\n", stPath.c_str(),
                     fuhgl::gl::version().major, fuhgl::gl::version().minor,
                     fuhgl::gl::queryDeviceInfo().renderer.c_str());
    }
    return 0;
}
