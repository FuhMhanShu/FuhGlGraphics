// World generator helper: generateWorld(params) builds either a landscape (G24 seeded noise
// heightmap with contrast shaping, island falloff, optional lake basin; scattered G26 L-system
// trees + bushes placed via rejection sampling on the height field) or a city (G27 WFC layout
// from makeCityTiles() over a ground plate; street cells get a sidewalk underlay + dark asphalt
// slab, block cells a concrete lot + a procedural house (walls/door/windows + gabled roof as a
// separate mesh part), plaza cells a lawn + park bushes; optional G28 voronoi district plates).
// Pure CPU, deterministic per seed — the app attaches the result to a scene::Scene
// (Mesh::setMeshData + node transforms).

#include <fuhglgraphics/procedural/worldgen.hpp>

#include <fuhgl/render.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fuhglgraphics::procedural {

namespace {

/// @brief Deterministic splitmix32 RNG (same stream on every platform).
struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() {
        state += 0x9E3779B9u;
        std::uint32_t z = state;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }
    /// @brief Uniform float in [0, 1).
    float nextf() { return static_cast<float>(next() * (1.0 / 4294967296.0)); }
};

float smoothstep(float e0, float e1, float x) {
    const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
    return t * t * (3.f - 2.f * t);
}

/// @brief Appends `src` transformed by `m` into `dst` (MeshData, 11 floats/vertex).
void appendTransformed(fuhgl::render::MeshData& dst, const fuhgl::render::MeshData& src,
                       const fuhgl::math::Mat4& m) {
    const fuhgl::math::Mat3 m3(m);
    const std::uint32_t base = static_cast<std::uint32_t>(dst.vertexCount());
    for (std::size_t i = 0; i < src.vertexCount(); ++i) {
        const float* v = &src.vertices[i * 11];
        const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{v[0], v[1], v[2], 1.f};
        const fuhgl::math::Vec3 n = fuhgl::math::normalize(m3 * fuhgl::math::Vec3{v[3], v[4], v[5]});
        dst.vertices.push_back(p.x); dst.vertices.push_back(p.y); dst.vertices.push_back(p.z);
        dst.vertices.push_back(n.x); dst.vertices.push_back(n.y); dst.vertices.push_back(n.z);
        for (int k = 6; k < 11; ++k) dst.vertices.push_back(v[k]);
    }
    for (const std::uint32_t idx : src.indices) dst.indices.push_back(base + idx);
}

/// @brief Shifts every vertex of `md` by `dy` (boxData is centered at the origin).
void shiftY(fuhgl::render::MeshData& md, float dy) {
    for (std::size_t i = 0; i < md.vertexCount(); ++i) md.vertices[i * 11 + 1] += dy;
}

/// @brief Appends one box (half extents `half`, centered at `pos`) with a flat color to `dst`.
void appendBox(fuhgl::render::MeshData& dst, const fuhgl::math::Vec3& half,
               const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& color) {
    fuhgl::render::MeshData b = fuhgl::render::shapes::boxData(half, color);
    shiftY(b, pos.y);
    appendTransformed(dst, b, fuhgl::math::translate({pos.x, 0.f, pos.z}));
}

// --- procedural house ---------------------------------------------------------------

void pushVertex(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& n,
                const fuhgl::math::Vec3& c, float u, float v) {
    md.vertices.push_back(p.x); md.vertices.push_back(p.y); md.vertices.push_back(p.z);
    md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(u); md.vertices.push_back(v);
}

void addQuad(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
             const fuhgl::math::Vec3& c, const fuhgl::math::Vec3& d, const fuhgl::math::Vec3& n,
             const fuhgl::math::Vec3& col) {
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    pushVertex(md, a, n, col, 0.f, 0.f);
    pushVertex(md, b, n, col, 1.f, 0.f);
    pushVertex(md, c, n, col, 1.f, 1.f);
    pushVertex(md, d, n, col, 0.f, 1.f);
    md.indices.push_back(base); md.indices.push_back(base + 1); md.indices.push_back(base + 2);
    md.indices.push_back(base); md.indices.push_back(base + 2); md.indices.push_back(base + 3);
}

void addTri(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
            const fuhgl::math::Vec3& c, const fuhgl::math::Vec3& n, const fuhgl::math::Vec3& col) {
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    pushVertex(md, a, n, col, 0.f, 0.f);
    pushVertex(md, b, n, col, 1.f, 0.f);
    pushVertex(md, c, n, col, 0.5f, 1.f);
    md.indices.push_back(base); md.indices.push_back(base + 1); md.indices.push_back(base + 2);
}

/// @brief Gabled roof prism (ridge along x) as its own MeshData: two slopes + two gable
/// triangles, with an eave overhang. Base sits at y = 0 (the caller places it at wallH).
fuhgl::render::MeshData makeRoofMesh(float w, float d, float roofH, const fuhgl::math::Vec3& color) {
    const float hw = w * 0.5f + 0.14f, hd = d * 0.5f + 0.14f; // overhang
    fuhgl::render::MeshData md;
    const fuhgl::math::Vec3 ns = fuhgl::math::normalize({0.f, roofH, hd});  // south slope
    const fuhgl::math::Vec3 nn = fuhgl::math::normalize({0.f, roofH, -hd}); // north slope
    addQuad(md, {-hw, 0.f, hd}, {hw, 0.f, hd}, {hw, roofH, 0.f}, {-hw, roofH, 0.f}, ns, color);
    addQuad(md, {hw, 0.f, -hd}, {-hw, 0.f, -hd}, {-hw, roofH, 0.f}, {hw, roofH, 0.f}, nn, color);
    addTri(md, {hw, 0.f, -hd}, {hw, 0.f, hd}, {hw, roofH, 0.f}, {1.f, 0.f, 0.f}, color);
    addTri(md, {-hw, 0.f, hd}, {-hw, 0.f, -hd}, {-hw, roofH, 0.f}, {-1.f, 0.f, 0.f}, color);
    return md;
}

/// @brief One procedural house: walls box + door + windows + chimney merged into `body`,
/// gabled roof into `roof`. Local space, base at y = 0, front door faces +z. `wallTint` picks
/// one of a few palettes; uv 0..1 per box face so a brick texture tiles on the walls.
void makeHouse(float w, float d, float wallH, float roofH, float wallTint,
               fuhgl::render::MeshData& body, fuhgl::render::MeshData& roof) {
    static const fuhgl::math::Vec3 wallPalettes[4] = {
        {0.82f, 0.78f, 0.70f}, {0.72f, 0.45f, 0.36f}, {0.78f, 0.74f, 0.66f}, {0.62f, 0.66f, 0.70f}};
    static const fuhgl::math::Vec3 roofPalettes[4] = {
        {0.45f, 0.16f, 0.12f}, {0.30f, 0.22f, 0.18f}, {0.50f, 0.24f, 0.14f}, {0.28f, 0.28f, 0.32f}};
    const int pal = static_cast<int>(wallTint * 4.f) % 4;
    const fuhgl::math::Vec3 wall = wallPalettes[pal];
    const fuhgl::math::Vec3 roofCol = roofPalettes[pal];

    body = fuhgl::render::shapes::boxData({w * 0.5f, wallH * 0.5f, d * 0.5f}, wall);
    shiftY(body, wallH * 0.5f); // walls
    appendBox(body, {0.09f, 0.8f, 0.035f}, {0.f, 0.8f, d * 0.5f + 0.02f}, {0.24f, 0.14f, 0.08f}); // door
    const fuhgl::math::Vec3 winCol{0.65f, 0.78f, 0.88f};
    for (int s = -1; s <= 1; s += 2) { // windows front + back
        appendBox(body, {0.28f, 0.38f, 0.03f}, {s * w * 0.25f, wallH * 0.62f, d * 0.5f + 0.015f}, winCol);
        appendBox(body, {0.28f, 0.38f, 0.03f}, {s * w * 0.25f, wallH * 0.62f, -d * 0.5f - 0.015f}, winCol);
    }
    if (w > 2.5f) // side windows on wider houses
        for (int s = -1; s <= 1; s += 2)
            appendBox(body, {0.03f, 0.38f, 0.28f}, {s * w * 0.5f + (s > 0 ? 0.015f : -0.015f), wallH * 0.62f, 0.f}, winCol);
    appendBox(body, {0.16f, roofH * 0.7f, 0.16f}, {w * 0.28f, wallH + roofH * 0.45f, -d * 0.22f},
              {0.55f, 0.53f, 0.50f}); // chimney
    roof = makeRoofMesh(w, d, roofH, roofCol);
    shiftY(roof, wallH); // eaves sit on top of the walls
}

/// @brief Landscape: noise heightmap (ridged fBm, contrast shaping, optional domain warp +
/// island falloff + lake basin) + scattered L-system trees and bushes that avoid water and
/// keep a min distance.
void generateLandscape(const WorldGenParams& p, GeneratedWorld& w) {
    Noise noise(p.seed);
    const int g = std::max(2, p.terrainGrid);
    w.gridW = g;
    w.gridH = g;
    w.sizeX = p.worldSize;
    w.sizeZ = p.worldSize;
    w.heightScale = p.terrainHeight;
    w.waterLevelWorld = p.waterLevel > 0.f ? p.waterLevel * p.terrainHeight : 0.f;
    w.heightmap.resize(static_cast<std::size_t>(g) * g);
    const float expo = std::max(0.05f, p.heightExponent);
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(g - 1);
            const float v = static_cast<float>(j) / static_cast<float>(g - 1);
            float nx = (u - 0.5f) * p.worldSize * p.noiseScale;
            float nz = (v - 0.5f) * p.worldSize * p.noiseScale;
            if (p.warpStrength > 0.f) {
                const fuhgl::math::Vec2 wp = noise.warp2(nx, nz, p.warpStrength, 3);
                nx = wp.x;
                nz = wp.y;
            }
            float h = noise.ridged2(nx, nz, p.octaves, Noise::Base::Simplex);
            h = std::pow(h, expo);                                                 // sharper peaks
            const float edge = std::max(std::fabs(u - 0.5f), std::fabs(v - 0.5f)) * 2.f; // [0,1] to border
            h *= 1.f - smoothstep(0.82f, 1.f, edge);                               // island falloff
            if (p.waterLevel > 0.f && h < p.waterLevel) h *= p.lakeCarve;           // lake basin
            w.heightmap[static_cast<std::size_t>(i) + g * j] = h;
        }

    Rng rng(p.seed ^ 0x7EE5u);
    const float half = p.worldSize * 0.5f;
    auto freeSpot = [&](float x, float z, float minDist) {
        if (w.waterLevelWorld > 0.f && w.heightAt(x, z) < w.waterLevelWorld + 0.4f) return false; // not in the lake
        for (const auto& other : w.trees) {
            const float dx = other.position.x - x, dz = other.position.z - z;
            if (dx * dx + dz * dz < minDist * minDist) return false;
        }
        for (const auto& other : w.bushes) {
            const float dx = other.position.x - x, dz = other.position.z - z;
            if (dx * dx + dz * dz < minDist * minDist) return false;
        }
        return true;
    };
    for (int t = 0; t < p.treeCount; ++t) {
        const float x = (rng.nextf() - 0.5f) * 2.f * half;
        const float z = (rng.nextf() - 0.5f) * 2.f * half;
        if (!freeSpot(x, z, p.treeMinDist)) continue;
        LSystem tree = LSystem::makeTree(static_cast<int>(rng.next() % 3u), p.seed);
        PlacedMesh pm;
        pm.mesh = tree.generate();
        pm.position = {x, w.heightAt(x, z), z};
        pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
        pm.scale = 0.45f + 0.5f * rng.nextf(); // modest trees
        w.trees.push_back(std::move(pm));
    }
    for (int t = 0; t < p.bushCount; ++t) {
        const float x = (rng.nextf() - 0.5f) * 2.f * half;
        const float z = (rng.nextf() - 0.5f) * 2.f * half;
        if (!freeSpot(x, z, p.treeMinDist * 0.5f)) continue;
        PlacedMesh pm;
        pm.mesh = LSystem::makeBush().generate();
        pm.position = {x, w.heightAt(x, z), z};
        pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
        pm.scale = 0.5f + 0.7f * rng.nextf();
        w.bushes.push_back(std::move(pm));
    }
}

/// @brief City: WFC layout over a ground plate; street cells get a sidewalk underlay + dark
/// asphalt slab, block cells a concrete lot + a procedural house, plaza cells a lawn + park
/// bushes; optional voronoi district plates underneath.
void generateCity(const WorldGenParams& p, GeneratedWorld& w) {
    const int gw = std::max(2, p.cityGrid);
    Wfc wfc(gw, gw, "s"); // border tag "s": the ring closes with blocks/sidewalk edges
    w.tiles = makeCityTiles();
    for (const auto& t : w.tiles) wfc.addTile(t);
    w.cells = wfc.solve(p.seed, p.maxBacktracks);
    w.cityW = gw;
    w.cityH = gw;
    w.cityCellSize = p.cellSize;

    const float size = gw * p.cellSize;
    w.cityGround = fuhgl::render::shapes::boxData({size * 0.5f, 0.1f, size * 0.5f}, {0.32f, 0.33f, 0.35f});
    shiftY(w.cityGround, -0.1f); // plate top at y = 0

    Noise noise(p.seed ^ 0xC17u);
    Rng rng(p.seed ^ 0xB05u);
    for (int z = 0; z < gw; ++z)
        for (int x = 0; x < gw; ++x) {
            const WfcCell& c = w.cells[static_cast<std::size_t>(x) + gw * z];
            const float wx = (x - (gw - 1) * 0.5f) * p.cellSize;
            const float wz = (z - (gw - 1) * 0.5f) * p.cellSize;
            if (c.tile < 0) continue; // unsolved cell stays plain ground
            const std::string& name = w.tiles[c.tile].name;
            const fuhgl::math::Mat4 place = fuhgl::math::translate({wx, 0.f, wz});
            if (name == "block") {
                // concrete lot
                fuhgl::render::MeshData lot =
                    fuhgl::render::shapes::boxData({p.cellSize * 0.47f, 0.025f, p.cellSize * 0.47f},
                                                   {0.48f, 0.47f, 0.44f});
                shiftY(lot, 0.025f);
                appendTransformed(w.roads, lot, place);
                // procedural house on the lot
                const float n = noise.value2(wx * 0.05f + 13.1f, wz * 0.05f + 7.7f); // per-block jitter
                const float wallH = p.buildingHeight * (0.5f + 0.9f * n);
                const float fw = p.cellSize * p.houseFootprint * (0.8f + 0.4f * n);
                const float fd = p.cellSize * p.houseFootprint * (0.8f + 0.4f * noise.value2(wz * 0.07f, wx * 0.07f));
                PlacedMesh pm;
                makeHouse(fw, fd, wallH, std::max(1.2f, wallH * 0.45f), n, pm.mesh, pm.roofMesh);
                pm.position = {wx, 0.05f, wz};
                pm.rotY = -c.rot * fuhgl::math::kPi * 0.5f;
                w.buildings.push_back(std::move(pm));
            } else if (name == "plaza") {
                // park: lawn slab + scattered bushes
                fuhgl::render::MeshData lawn =
                    fuhgl::render::shapes::boxData({p.cellSize * 0.46f, 0.02f, p.cellSize * 0.46f},
                                                   {0.16f, 0.42f, 0.16f});
                shiftY(lawn, 0.04f);
                appendTransformed(w.roads, lawn, place);
                for (int b = 0; b < p.parkBushesPerPlaza; ++b) {
                    PlacedMesh pm;
                    pm.mesh = LSystem::makeBush().generate();
                    pm.position = {wx + (rng.nextf() - 0.5f) * p.cellSize * 0.6f, 0.06f,
                                   wz + (rng.nextf() - 0.5f) * p.cellSize * 0.6f};
                    pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
                    pm.scale = 0.5f + 0.5f * rng.nextf();
                    w.bushes.push_back(std::move(pm));
                }
            } else {
                // street / crossing / T / end: light sidewalk underlay + dark asphalt slab
                fuhgl::render::MeshData walk =
                    fuhgl::render::shapes::boxData({p.cellSize * 0.5f, 0.02f, p.cellSize * 0.5f},
                                                   {0.55f, 0.55f, 0.52f});
                shiftY(walk, 0.02f);
                appendTransformed(w.roads, walk, place);
                fuhgl::render::MeshData asphalt =
                    fuhgl::render::shapes::boxData({p.cellSize * 0.44f, 0.015f, p.cellSize * 0.44f},
                                                   {0.09f, 0.09f, 0.10f});
                shiftY(asphalt, 0.045f);
                appendTransformed(w.roads, asphalt, place);
            }
        }

    if (p.blocksPerDistrict > 0) {
        const float half = size * 0.5f;
        const Voronoi2D v = voronoi2d(p.blocksPerDistrict, {-half, -half}, {half, half}, p.seed ^ 0xB10Cu);
        w.districts = extrudeCells(v, -0.25f, -0.02f, 0.12f, 0.f, 0, {0.36f, 0.34f, 0.30f}, 0.1f);
    }
}

} // namespace

float GeneratedWorld::heightAt(float x, float z) const {
    if (gridW < 2 || gridH < 2 || sizeX <= 0.f || sizeZ <= 0.f) return 0.f;
    const float u = std::min(1.f, std::max(0.f, x / sizeX + 0.5f));
    const float v = std::min(1.f, std::max(0.f, z / sizeZ + 0.5f));
    const float fx = u * static_cast<float>(gridW - 1);
    const float fz = v * static_cast<float>(gridH - 1);
    const int x0 = static_cast<int>(fx), z0 = static_cast<int>(fz);
    const int x1 = std::min(gridW - 1, x0 + 1), z1 = std::min(gridH - 1, z0 + 1);
    const float tx = fx - x0, tz = fz - z0;
    const float h00 = heightmap[static_cast<std::size_t>(x0) + gridW * z0];
    const float h10 = heightmap[static_cast<std::size_t>(x1) + gridW * z0];
    const float h01 = heightmap[static_cast<std::size_t>(x0) + gridW * z1];
    const float h11 = heightmap[static_cast<std::size_t>(x1) + gridW * z1];
    const float h = (h00 * (1.f - tx) + h10 * tx) * (1.f - tz) + (h01 * (1.f - tx) + h11 * tx) * tz;
    return h * heightScale;
}

std::vector<WfcTile> makeCityTiles() {
    // edge tags in cyclic dir order (E, S, W, N); rotations are handled by the WFC solver
    return {
        {"block", 4.f, {"s", "s", "s", "s"}},   // building lot (fills the border ring too)
        {"plaza", 1.2f, {"p", "p", "p", "p"}},   // park square, only next to plazas
        {"streetH", 1.f, {"r", "s", "r", "s"}}, // road along E-W
        {"streetV", 1.f, {"s", "r", "s", "r"}}, // road along N-S (rotation of streetH)
        {"cross", 0.4f, {"r", "r", "r", "r"}},   // 4-way crossing
        {"tE", 0.6f, {"r", "r", "r", "s"}},      // T junction: road to E, S, W
        {"endE", 0.35f, {"r", "s", "s", "s"}},   // dead end to E
    };
}

GeneratedWorld generateWorld(const WorldGenParams& params) {
    GeneratedWorld w;
    w.mode = params.mode;
    w.seed = params.seed;
    if (params.mode == WorldGenParams::Mode::City)
        generateCity(params, w);
    else
        generateLandscape(params, w);
    return w;
}

} // namespace fuhglgraphics::procedural
