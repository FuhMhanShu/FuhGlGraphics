// World generator helper: generateWorld(params) builds either a landscape (G24 seeded noise
// heightmap with contrast shaping, island falloff, optional lake basin; scattered G26 L-system
// trees + bushes placed via rejection sampling on the height field) or a city (G27 WFC layout
// from makeCityTiles() over a ground plate; street cells get a sidewalk underlay + dark asphalt
// slab, block cells a concrete lot + a procedural house (walls/door/windows + gabled roof as a
// separate mesh part), plaza cells a lawn + park bushes; optional G28 voronoi district plates).
// Pure CPU, deterministic per seed — the app attaches the result to a scene::Scene
// (Mesh::setMeshData + node transforms).

#include <fuhglgraphics/procedural/worldgen.hpp>

#include <fuhglgraphics/procedural/plant.hpp>

#include <fuhgl/render.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

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

/// @brief UV sphere as MeshData (pos/normal/color/uv per vertex, outward CCW winding) —
/// used for the scattered rocks; keeps worldgen free of GL-side Mesh objects.
fuhgl::render::MeshData uvSphereData(float radius, int seg, const fuhgl::math::Vec3& color) {
    fuhgl::render::MeshData md;
    const int rings = std::max(3, seg);
    md.vertices.reserve(static_cast<std::size_t>(rings + 1) * (seg + 1) * 11);
    for (int r = 0; r <= rings; ++r) {
        const float phi = fuhgl::math::kPi * static_cast<float>(r) / rings; // 0..pi, top to bottom
        const float y = std::cos(phi) * radius;
        const float rr = std::sin(phi) * radius;
        for (int s = 0; s <= seg; ++s) {
            const float theta = 2.f * fuhgl::math::kPi * static_cast<float>(s) / seg;
            const float x = rr * std::cos(theta), z = rr * std::sin(theta);
            const fuhgl::math::Vec3 n = fuhgl::math::normalize({x, y, z});
            const float vert[11] = {x,  y,  z,  n.x, n.y, n.z, color.x, color.y, color.z,
                                    static_cast<float>(s) / seg, static_cast<float>(r) / rings};
            md.vertices.insert(md.vertices.end(), vert, vert + 11);
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < seg; ++s) {
            const std::uint32_t a = static_cast<std::uint32_t>(r * (seg + 1) + s);
            const std::uint32_t b = a + seg + 1;
            md.indices.push_back(a); md.indices.push_back(a + 1); md.indices.push_back(b);
            md.indices.push_back(a + 1); md.indices.push_back(b + 1); md.indices.push_back(b);
        }
    return md;
}

// --- landscape pipeline -------------------------------------------------------------------
// 1. macro relief: domain-warped coords + continentalness mask (low-freq simplex decides
//    plains vs. mountains) combining plain fBm (plains) with ridged multifractal (mountains)
// 2. hydraulic erosion (droplet particle sim, also produces the flow map) + thermal erosion
// 3. splatmap: R grass / G rock (slope) / B sand (shore + flow channels) / A snow (altitude)
// 4. vegetation & rocks scattered by poisson-disk sampling weighted by the splatmap

float cellWorld(const WorldGenParams& p, int g) { return p.worldSize / static_cast<float>(g - 1); }

/// @brief Hydraulic erosion: droplet particle simulation on the normalized heightmap.
/// Each droplet flows downhill, erodes sediment where fast/steep and deposits it in the flats;
/// the traveled water amount accumulates into `flow` (riverbeds).
void hydraulicErode(std::vector<float>& h, int g, int droplets, Rng& rng,
                    std::vector<float>& flow) {
    const std::size_t n = static_cast<std::size_t>(g) * g;
    flow.assign(n, 0.f);
    auto at = [&](int x, int z) { return h[static_cast<std::size_t>(x) + g * z]; };
    for (int d = 0; d < droplets; ++d) {
        float x = 1.f + rng.nextf() * (g - 3), z = 1.f + rng.nextf() * (g - 3);
        float speed = 1.f, water = 1.f, sediment = 0.f;
        for (int step = 0; step < 48; ++step) {
            const int xi = static_cast<int>(x), zi = static_cast<int>(z);
            // gradient via bilinear interpolation of the height differences
            const float fx = x - xi, fz = z - zi;
            const float h00 = at(xi, zi), h10 = at(xi + 1, zi), h01 = at(xi, zi + 1), h11 = at(xi + 1, zi + 1);
            const float gx = (h10 - h00) * (1.f - fz) + (h11 - h01) * fz;
            const float gz = (h01 - h00) * (1.f - fx) + (h11 - h10) * fx;
            float dirX = -gx, dirZ = -gz;
            const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (len < 1e-6f) break;
            dirX /= len;
            dirZ /= len;
            const float nx = x + dirX, nz = z + dirZ;
            if (nx < 0.f || nz < 0.f || nx > g - 1 || nz > g - 1) break;
            const int nxi = static_cast<int>(nx), nzi = static_cast<int>(nz);
            const float nfx = nx - nxi, nfz = nz - nzi;
            const float newH = (at(nxi, nzi) * (1.f - nfx) + at(nxi + 1, nzi) * nfx) * (1.f - nfz) +
                               (at(nxi, nzi + 1) * (1.f - nfx) + at(nxi + 1, nzi + 1) * nfx) * nfz;
            const float deltaH = at(xi, zi) - newH; // >0: flowing downhill
            const float capacity = std::max(deltaH * speed * water * 4.f, 0.01f);
            if (sediment > capacity || deltaH <= 0.f) {
                const float deposit = deltaH <= 0.f ? sediment : (sediment - capacity) * 0.5f;
                h[static_cast<std::size_t>(xi) + g * zi] += deposit;
                sediment -= deposit;
            } else {
                const float amount = std::min((capacity - sediment) * 0.3f, deltaH * 0.5f);
                h[static_cast<std::size_t>(xi) + g * zi] -= amount;
                sediment += amount;
            }
            speed = std::sqrt(std::max(0.f, speed * speed + deltaH * 4.f));
            water *= 0.985f;
            flow[static_cast<std::size_t>(xi) + g * zi] += water * speed * 0.02f;
            x = nx;
            z = nz;
            if (water < 0.01f) break;
        }
    }
    for (float& v : h) v = std::min(1.f, std::max(0.f, v));
    float maxFlow = 0.f;
    for (const float f : flow) maxFlow = std::max(maxFlow, f);
    if (maxFlow > 0.f)
        for (float& f : flow) f /= maxFlow;
}

/// @brief Thermal erosion: material slides down where the slope exceeds the talus threshold.
void thermalErode(std::vector<float>& h, int g, float talus, int passes) {
    for (int pass = 0; pass < passes; ++pass)
        for (int z = 1; z < g - 1; ++z)
            for (int x = 1; x < g - 1; ++x) {
                float& hc = h[static_cast<std::size_t>(x) + g * z];
                const float hArr[4] = {h[static_cast<std::size_t>(x + 1) + g * z],
                                       h[static_cast<std::size_t>(x - 1) + g * z],
                                       h[static_cast<std::size_t>(x) + g * (z + 1)],
                                       h[static_cast<std::size_t>(x) + g * (z - 1)]};
                for (int k = 0; k < 4; ++k) {
                    const float diff = hc - hArr[k];
                    if (diff > talus) {
                        const float move = (diff - talus) * 0.25f;
                        hc -= move;
                        h[static_cast<std::size_t>(x + (k == 0 ? 1 : k == 1 ? -1 : 0)) +
                          g * (z + (k == 2 ? 1 : k == 3 ? -1 : 0))] += move;
                    }
                }
            }
}

/// @brief Poisson-disk-ish scatter (dart throwing with a spatial hash): random points with a
/// guaranteed min distance, accepted with probability `weight(x,z)` (density mask).
std::vector<fuhgl::math::Vec2> poissonScatter(float half, float minDist, int maxPoints,
                                              const std::function<float(float, float)>& weight,
                                              Rng& rng) {
    std::vector<fuhgl::math::Vec2> pts;
    const float cell = minDist * 0.7071f;
    const int gw = std::max(1, static_cast<int>(std::ceil(2.f * half / cell)));
    std::vector<int> head(static_cast<std::size_t>(gw) * gw, -1);
    std::vector<int> next;
    const int attempts = maxPoints * 40;
    for (int a = 0; a < attempts && static_cast<int>(pts.size()) < maxPoints; ++a) {
        const float x = (rng.nextf() - 0.5f) * 2.f * half;
        const float z = (rng.nextf() - 0.5f) * 2.f * half;
        if (weight(x, z) < rng.nextf()) continue; // density-mask rejection
        const int cx = static_cast<int>((x + half) / cell), cz = static_cast<int>((z + half) / cell);
        bool tooClose = false;
        const int r = static_cast<int>(std::ceil(minDist / cell));
        for (int dz = -r; dz <= r && !tooClose; ++dz)
            for (int dx = -r; dx <= r && !tooClose; ++dx) {
                const int qx = cx + dx, qz = cz + dz;
                if (qx < 0 || qz < 0 || qx >= gw || qz >= gw) continue;
                for (int idx = head[static_cast<std::size_t>(qx) + gw * qz]; idx >= 0; idx = next[idx]) {
                    const float ddx = pts[idx].x - x, ddz = pts[idx].y - z;
                    if (ddx * ddx + ddz * ddz < minDist * minDist) { tooClose = true; break; }
                }
            }
        if (tooClose) continue;
        const int id = static_cast<int>(pts.size());
        pts.push_back({x, z});
        next.push_back(-1);
        const std::size_t cellIdx = static_cast<std::size_t>(cx) + gw * cz;
        next[id] = head[cellIdx];
        head[cellIdx] = id;
    }
    return pts;
}

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
    const float waterN = p.waterLevel; // normalized water level for masks
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(g - 1);
            const float v = static_cast<float>(j) / static_cast<float>(g - 1);
            const float x = (u - 0.5f) * p.worldSize, z = (v - 0.5f) * p.worldSize;
            // 1. domain warp: distort the noise input -> flowing folds, organic coastlines
            float nx = x * p.noiseScale, nz = z * p.noiseScale;
            if (p.warpStrength > 0.f) {
                const fuhgl::math::Vec2 wp = noise.warp2(nx, nz, p.warpStrength, 3);
                nx = wp.x;
                nz = wp.y;
            }
            // continentalness: very low-frequency simplex decides plains vs. mountain ranges
            const float cont = noise.simplex2(nx * 0.22f + 7.3f, nz * 0.22f + 2.9f); // ~[-1,1]
            const float mountainMask = smoothstep(0.15f, 0.75f, cont);
            const float plains = noise.fbm2(nx, nz, 5, Noise::Base::Simplex) * 0.5f + 0.5f;
            const float ridge = noise.ridged2(nx, nz, p.octaves, Noise::Base::Simplex); // sharp crests
            float h = plains * (1.f - mountainMask) + ridge * mountainMask;
            h = std::pow(std::min(1.f, std::max(0.f, h)), expo);
            if (p.lakeRadius > 0.f) { // explicit round lake basin around the center
                const float d = std::hypot(x, z);
                h *= smoothstep(p.lakeRadius * 0.55f, p.lakeRadius, d);
            }
            const float edge = std::max(std::fabs(u - 0.5f), std::fabs(v - 0.5f)) * 2.f;
            const float fall = 1.f - smoothstep(0.82f, 1.f, edge);
            if (p.waterLevel > 0.f)
                h = h * fall + p.waterLevel * 1.05f * (1.f - fall); // shore ring above water
            else
                h *= fall;
            if (p.waterLevel > 0.f && h < p.waterLevel) h *= p.lakeCarve; // lake bed
            w.heightmap[static_cast<std::size_t>(i) + g * j] = h;
        }

    // 2. erosion: hydraulic droplets carve valleys/riverbeds (and fill the flow map),
    //    thermal passes relax slopes above the talus angle into debris fans
    Rng rng(p.seed ^ 0x7EE5u);
    if (p.erosionDroplets > 0)
        hydraulicErode(w.heightmap, g, p.erosionDroplets, rng, w.flowmap);
    else
        w.flowmap.assign(w.heightmap.size(), 0.f);
    if (p.thermalPasses > 0) thermalErode(w.heightmap, g, p.talus, p.thermalPasses);

    // 3. splatmap: per-cell material weights from height, slope and the erosion flow map.
    // Slope is measured in world units (dh_world/dx); the rock band is relative to the
    // steepest slope present so cliffs always get rock at any heightScale/grid combination.
    const float cell = cellWorld(p, g);
    w.splatmap.resize(static_cast<std::size_t>(g) * g * 4);
    auto hAt = [&](int x, int z) {
        x = std::min(g - 1, std::max(0, x));
        z = std::min(g - 1, std::max(0, z));
        return w.heightmap[static_cast<std::size_t>(x) + g * z];
    };
    std::vector<float> slopeMap(static_cast<std::size_t>(g) * g, 0.f);
    float maxSlope = 0.f;
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const float gx = (hAt(i + 1, j) - hAt(i - 1, j)) * 0.5f * w.heightScale / cell;
            const float gz = (hAt(i, j + 1) - hAt(i, j - 1)) * 0.5f * w.heightScale / cell;
            const float slope = std::sqrt(gx * gx + gz * gz);
            slopeMap[static_cast<std::size_t>(i) + g * j] = slope;
            maxSlope = std::max(maxSlope, slope);
        }
    const float rockLo = maxSlope * 0.5f, rockHi = maxSlope * 0.95f;
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const float h = hAt(i, j);
            const float slope = slopeMap[static_cast<std::size_t>(i) + g * j];
            const float flow = w.flowmap[static_cast<std::size_t>(i) + g * j];
            float rock = smoothstep(rockLo, rockHi, slope);
            float snow = smoothstep(0.58f, 0.8f, h); // altitude-based snowline (heightmap is [0,1])
            float sand = smoothstep(0.035f, 0.f, std::fabs(h - waterN)) + flow * 0.6f; // shore + riverbeds
            sand = std::min(1.f, sand);
            float grass = std::min(1.f, std::max(0.f, 1.f - rock - snow - sand));
            if (h < waterN) grass *= 0.15f; // lake bed
            const std::size_t idx = (static_cast<std::size_t>(i) + g * j) * 4;
            w.splatmap[idx + 0] = static_cast<unsigned char>(grass * 255.f);
            w.splatmap[idx + 1] = static_cast<unsigned char>(std::min(1.f, rock) * 255.f);
            w.splatmap[idx + 2] = static_cast<unsigned char>(sand * 255.f);
            w.splatmap[idx + 3] = static_cast<unsigned char>(snow * 255.f);
        }

    // 4. poisson-disk scatter weighted by the splatmap: trees on grass, rocks on rock/steep
    const float half = p.worldSize * 0.5f;
    auto grassWeight = [&](float x, float z) {
        if (w.heightAt(x, z) < w.waterLevelWorld + 0.3f) return 0.f;
        const float u = std::min(1.f, std::max(0.f, x / w.sizeX + 0.5f)) * (g - 1);
        const float v = std::min(1.f, std::max(0.f, z / w.sizeZ + 0.5f)) * (g - 1);
        const std::size_t idx = (static_cast<std::size_t>(u) + g * static_cast<std::size_t>(v)) * 4;
        const float grass = w.splatmap[idx + 0] / 255.f;
        const float rock = w.splatmap[idx + 1] / 255.f;
        return grass * (1.f - rock);
    };
    auto rockWeight = [&](float x, float z) {
        const float u = std::min(1.f, std::max(0.f, x / w.sizeX + 0.5f)) * (g - 1);
        const float v = std::min(1.f, std::max(0.f, z / w.sizeZ + 0.5f)) * (g - 1);
        const std::size_t idx = (static_cast<std::size_t>(u) + g * static_cast<std::size_t>(v)) * 4;
        return w.splatmap[idx + 1] / 255.f;
    };
    // trees: mixed species via the PlantGenerator (deciduous / fir / birch), bushes likewise
    PlantGenerator plantGen;
    for (const auto& s : poissonScatter(half, p.treeMinDist, p.treeCount, grassWeight, rng)) {
        PlantParams pp;
        pp.seed = p.seed ^ (rng.next() * 2654435761u);
        const float kindRoll = rng.nextf();
        pp.kind = kindRoll < 0.5f ? PlantKind::Deciduous
                 : kindRoll < 0.75f ? PlantKind::Fir
                                   : PlantKind::Birch;
        pp.scale = 0.45f + 0.5f * rng.nextf();
        PlacedMesh pm;
        pm.mesh = plantGen.generate(pp).mesh;
        pm.position = {s.x, w.heightAt(s.x, s.y), s.y};
        pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
        pm.scale = pp.scale;
        w.trees.push_back(std::move(pm));
    }
    for (const auto& s : poissonScatter(half, p.treeMinDist * 0.5f, p.bushCount, grassWeight, rng)) {
        PlantParams pp;
        pp.kind = PlantKind::Bush;
        pp.seed = p.seed ^ (rng.next() * 40503u);
        pp.scale = 0.5f + 0.7f * rng.nextf();
        PlacedMesh pm;
        pm.mesh = plantGen.generate(pp).mesh;
        pm.position = {s.x, w.heightAt(s.x, s.y), s.y};
        pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
        pm.scale = pp.scale;
        w.bushes.push_back(std::move(pm));
    }
    for (const auto& s : poissonScatter(half, p.treeMinDist * 0.8f, p.rockCount, rockWeight, rng)) {
        PlacedMesh pm;
        pm.mesh = uvSphereData(1.f, 7, {0.45f, 0.44f, 0.42f});
        pm.position = {s.x, w.heightAt(s.x, s.y) + 0.1f, s.y};
        pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
        pm.scale = 0.4f + 1.2f * rng.nextf();
        w.rocks.push_back(std::move(pm));
    }
}
/// @brief City: WFC layout over a ground plate; street cells get a sidewalk underlay + dark
/// asphalt slab, block cells a concrete lot + a procedural house, plaza cells a lawn + park
/// bushes; optional voronoi district plates underneath.
void generateCity(const WorldGenParams& p, GeneratedWorld& w) {
    HouseGenerator houseGen;
    PlantGenerator plantGen;
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
            // interior block cells become parks with probability parkFraction (guarantees
            // green spaces even when the WFC layout contains no plaza tiles)
            const bool isPark = name == "plaza" ||
                                (name == "block" && x > 0 && z > 0 && x + 1 < gw && z + 1 < gw &&
                                 rng.nextf() < p.parkFraction);
            if (isPark) {
                ++w.parkCellCount;
                fuhgl::render::MeshData lawn =
                    fuhgl::render::shapes::boxData({p.cellSize * 0.46f, 0.02f, p.cellSize * 0.46f},
                                                   {0.16f, 0.42f, 0.16f});
                shiftY(lawn, 0.04f);
                appendTransformed(w.roads, lawn, place);
                for (int b = 0; b < p.parkBushesPerPlaza; ++b) {
                    PlantParams pp;
                    pp.kind = PlantKind::Bush;
                    pp.seed = p.seed ^ rng.next();
                    pp.scale = 0.5f + 0.5f * rng.nextf();
                    PlacedMesh pm;
                    pm.mesh = plantGen.generate(pp).mesh;
                    pm.position = {wx + (rng.nextf() - 0.5f) * p.cellSize * 0.6f, 0.06f,
                                   wz + (rng.nextf() - 0.5f) * p.cellSize * 0.6f};
                    pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
                    pm.scale = 0.5f + 0.5f * rng.nextf();
                    w.bushes.push_back(std::move(pm));
                }
            } else if (name == "block") {
                // concrete lot
                fuhgl::render::MeshData lot =
                    fuhgl::render::shapes::boxData({p.cellSize * 0.47f, 0.025f, p.cellSize * 0.47f},
                                                   {0.48f, 0.47f, 0.44f});
                shiftY(lot, 0.025f);
                appendTransformed(w.roads, lot, place);
                // procedural house on the lot
                const float n = noise.value2(wx * 0.05f + 13.1f, wz * 0.05f + 7.7f); // per-block jitter
                const std::uint32_t houseSeed =
                    p.seed ^ 0x11A5u ^ (static_cast<std::uint32_t>(x) * 73856093u) ^
                    (static_cast<std::uint32_t>(z) * 19349663u);
                HouseGenerator::Params hp =
                    HouseGenerator::randomParams(p.houseStyle, p.cellSize, houseSeed);
                hp.seed = houseSeed;
                hp.width *= (0.8f + 0.4f * n);
                hp.depth *= (0.8f + 0.4f * noise.value2(wz * 0.07f, wx * 0.07f));
                const HouseMesh hm = houseGen.generate(hp);
                PlacedMesh pm;
                pm.mesh = hm.walls;
                pm.detailMesh = hm.details;
                pm.roofMesh = hm.roof;
                pm.roofType = static_cast<int>(hm.spec.roofType);
                pm.position = {wx, 0.05f, wz};
                pm.rotY = -c.rot * fuhgl::math::kPi * 0.5f;
                w.buildings.push_back(std::move(pm));
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
