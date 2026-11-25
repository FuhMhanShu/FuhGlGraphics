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

Biome biomeFromClimate(float temperature, float humidity) {
    // Whittaker lookup: x = humidity, y = temperature
    if (temperature < 0.25f) return humidity < 0.5f ? Biome::Tundra : Biome::Taiga;
    if (temperature < 0.55f) return humidity < 0.35f ? Biome::Grassland : Biome::Forest;
    return humidity < 0.4f ? Biome::Desert : Biome::Jungle;
}

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

void pushVertex(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& n,
                const fuhgl::math::Vec3& c, float u, float v) {
    md.vertices.push_back(p.x); md.vertices.push_back(p.y); md.vertices.push_back(p.z);
    md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(u); md.vertices.push_back(v);
}

/// @brief One road slab: a flat quad at height `y` centered at (cx, cz) with per-corner edge
/// jitter (irregular street edges) and world-space uv (x,z)*uvScale so the app texture tiles.
void appendRoadSlab(fuhgl::render::MeshData& md, float cx, float cz, float half, float y,
                    const fuhgl::math::Vec3& col, float jitter, Rng& rng, float uvScale) {
    const float x0 = cx - half + (rng.nextf() - 0.5f) * 2.f * jitter;
    const float x1 = cx + half + (rng.nextf() - 0.5f) * 2.f * jitter;
    const float z0 = cz - half + (rng.nextf() - 0.5f) * 2.f * jitter;
    const float z1 = cz + half + (rng.nextf() - 0.5f) * 2.f * jitter;
    const fuhgl::math::Vec3 up{0.f, 1.f, 0.f};
    const std::uint32_t b = static_cast<std::uint32_t>(md.vertexCount());
    pushVertex(md, {x0, y, z0}, up, col, x0 * uvScale, z0 * uvScale);
    pushVertex(md, {x1, y, z0}, up, col, x1 * uvScale, z0 * uvScale);
    pushVertex(md, {x1, y, z1}, up, col, x1 * uvScale, z1 * uvScale);
    pushVertex(md, {x0, y, z1}, up, col, x0 * uvScale, z1 * uvScale);
    md.indices.push_back(b); md.indices.push_back(b + 2); md.indices.push_back(b + 1); // +y
    md.indices.push_back(b); md.indices.push_back(b + 3); md.indices.push_back(b + 2);
}

/// @brief BFS labeling of the road components of a solved WFC grid: cells whose (rotated)
/// tile has an "r" edge are road cells; two road cells belong to the same component when they
/// share an "r" edge. Fills `compId` (per cell, -1 = no road) and returns the component sizes.
std::vector<std::size_t> labelRoadComponents(const std::vector<WfcCell>& cells,
                                             const std::vector<WfcTile>& tiles,
                                             int gridW, int gridH, std::vector<int>& compId) {
    static const int dx[4] = {1, 0, -1, 0}, dz[4] = {0, 1, 0, -1}; // E, S, W, N
    const std::size_t n = static_cast<std::size_t>(gridW) * gridH;
    compId.assign(n, -1);
    std::vector<char> isRoad(n, 0);
    for (int z = 0; z < gridH; ++z)
        for (int x = 0; x < gridW; ++x) {
            const WfcCell& c = cells[static_cast<std::size_t>(x) + gridW * z];
            if (c.tile < 0) continue;
            const auto tags = wfcRotatedTags(tiles[static_cast<std::size_t>(c.tile)], c.rot);
            for (const auto& tg : tags)
                if (tg == "r") isRoad[static_cast<std::size_t>(x) + gridW * z] = 1;
        }
    std::vector<std::size_t> sizes;
    for (std::size_t i = 0; i < n; ++i) {
        if (!isRoad[i] || compId[i] >= 0) continue;
        const int id = static_cast<int>(sizes.size());
        std::size_t count = 0;
        std::vector<int> stack{static_cast<int>(i)};
        compId[i] = id;
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            ++count;
            const int cx = cur % gridW, cz = cur / gridW;
            const WfcCell& c = cells[static_cast<std::size_t>(cur)];
            const auto tags = wfcRotatedTags(tiles[static_cast<std::size_t>(c.tile)], c.rot);
            for (int d = 0; d < 4; ++d) {
                if (tags[static_cast<std::size_t>(d)] != "r") continue;
                const int nx = cx + dx[d], nz = cz + dz[d];
                if (nx < 0 || nz < 0 || nx >= gridW || nz >= gridH) continue;
                const int ni = nx + gridW * nz;
                if (!isRoad[static_cast<std::size_t>(ni)] || compId[static_cast<std::size_t>(ni)] >= 0)
                    continue;
                // the neighbor must share the "r" tag on its opposite edge
                const WfcCell& nc = cells[static_cast<std::size_t>(ni)];
                const auto ntags = wfcRotatedTags(tiles[static_cast<std::size_t>(nc.tile)], nc.rot);
                if (ntags[static_cast<std::size_t>(kWfcOpposite[d])] != "r") continue;
                compId[static_cast<std::size_t>(ni)] = id;
                stack.push_back(ni);
            }
        }
        sizes.push_back(count);
    }
    return sizes;
}

/// @brief When `allowPathCuts` is false: converts every road cell outside the largest connected
/// road component into a "block" cell, so the street network is one uninterrupted component and
/// no path/park fragment can cut a street. Stays a valid WFC solution: all "r" edges of a
/// component are internal (a valid solve matches every "r" with an "r"), so relabeling a whole
/// component to "s" edges keeps every shared edge equal.
void repairRoadConnectivity(std::vector<WfcCell>& cells, const std::vector<WfcTile>& tiles,
                            int gridW, int gridH) {
    std::vector<int> compId;
    const auto sizes = labelRoadComponents(cells, tiles, gridW, gridH, compId);
    if (sizes.size() <= 1) return; // already one (or zero) component
    int largest = 0;
    for (std::size_t i = 1; i < sizes.size(); ++i)
        if (sizes[i] > sizes[largest]) largest = static_cast<int>(i);
    int blockTile = -1;
    for (std::size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].name == "block") blockTile = static_cast<int>(i);
    if (blockTile < 0) return;
    for (std::size_t i = 0; i < compId.size(); ++i)
        if (compId[i] >= 0 && compId[i] != largest) {
            cells[i].tile = blockTile;
            cells[i].rot = 0; // block is rotationally symmetric
        }
}

// --- landscape pipeline -------------------------------------------------------------------
// 1. macro relief: domain-warped coords + three spline/noise layers — continentalness mapped
//    through a Spline1D curve (ocean/coast/plains/mountains), an erosion map that blends smooth
//    fBm (meadows) with ridged multifractal (rugged), and a peaks&valleys ridged detail layer
// 2. hydraulic erosion (droplet particle sim, also produces the flow map) + thermal erosion,
//    optional terracing (quantized steps on high ground) + rivers (gradient descent from
//    mountain sources, V-carved channels, sinks filled to their spill point as ponds)
// 3. splatmap: R grass / G rock (slope) / B sand (shore + flow channels) / A snow (altitude),
//    plus climate maps (temperature/humidity) and a Whittaker biome lookup per cell
// 4. vegetation & rocks scattered by poisson-disk sampling weighted by splatmap + biome

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

/// @brief Rivers: gradient descent from mountain source cells (h >= riverSourceMinH) down to
/// the water level. Each path cell carves a V-shaped channel (riverDepth at the center, fading
/// over riverWidth cells) into the heightmap and adds to the flow map. When the path gets stuck
/// in a sink above the water level, the connected basin is filled up to its spill point
/// (mountain lake, marked in `ponds`) and the river continues over the spill cell.
void carveRivers(std::vector<float>& h, int g, const WorldGenParams& p, Rng& rng,
                 std::vector<float>& flow, std::vector<unsigned char>& ponds) {
    ponds.assign(h.size(), 0);
    if (p.riverCount <= 0) return;
    auto at = [&](int x, int z) -> float& { return h[static_cast<std::size_t>(x) + g * z]; };
    // source candidates: high cells away from the border, highest first
    std::vector<std::pair<float, int>> cand;
    const int margin = std::max(2, g / 12);
    for (int a = 0; a < p.riverCount * 60 && static_cast<int>(cand.size()) < p.riverCount * 4; ++a) {
        const int x = margin + static_cast<int>(rng.nextf() * (g - 2 * margin));
        const int z = margin + static_cast<int>(rng.nextf() * (g - 2 * margin));
        const float hv = at(x, z);
        if (hv >= p.riverSourceMinH) cand.push_back({hv, x + g * z});
    }
    std::sort(cand.begin(), cand.end(),
              [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
    const int rivers = std::min(p.riverCount, static_cast<int>(cand.size()));
    const float waterN = p.waterLevel;
    const float rw = std::max(0.6f, p.riverWidth);
    for (int r = 0; r < rivers; ++r) {
        int cx = cand[static_cast<std::size_t>(r)].second % g;
        int cz = cand[static_cast<std::size_t>(r)].second / g;
        std::vector<char> seen(h.size(), 0);
        for (int step = 0; step < g * 3; ++step) {
            // carve a V cross-section around the current cell
            const int cr = static_cast<int>(std::ceil(rw));
            for (int dz = -cr; dz <= cr; ++dz)
                for (int dx = -cr; dx <= cr; ++dx) {
                    const int x = cx + dx, z = cz + dz;
                    if (x < 1 || z < 1 || x >= g - 1 || z >= g - 1) continue;
                    const float d = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                    if (d > rw) continue;
                    const float prof = 1.f - d / rw; // V profile across the channel
                    at(x, z) = std::max(0.f, at(x, z) - p.riverDepth * prof * prof);
                    // max-based accumulation: a cell counts as channel once, no matter how
                    // often the path loops over it (keeps the normalized flow map readable)
                    flow[static_cast<std::size_t>(x) + g * z] =
                        std::max(flow[static_cast<std::size_t>(x) + g * z], prof);
                }
            seen[static_cast<std::size_t>(cx) + g * cz] = 1;
            if (at(cx, cz) <= waterN + 0.001f) break; // reached the lake/sea
            // steepest descent over the 8 neighbors (tiny random jitter breaks ties/loops)
            int bx = cx, bz = cz;
            float bh = at(cx, cz) + rng.nextf() * 1e-4f;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dz) continue;
                    const int x = cx + dx, z = cz + dz;
                    if (x < 1 || z < 1 || x >= g - 1 || z >= g - 1) continue;
                    const float hv = at(x, z) + rng.nextf() * 1e-4f;
                    if (hv < bh) { bh = hv; bx = x; bz = z; }
                }
            if (bx == cx && bz == cz) {
                // local sink above the water level: fill the basin to its spill point (pond)
                float spill = 1e9f;
                int sx = cx, sz = cz;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = cx + dx, z = cz + dz;
                        if (x < 1 || z < 1 || x >= g - 1 || z >= g - 1) continue;
                        const float hv = at(x, z);
                        if (hv > at(cx, cz) && hv < spill) { spill = hv; sx = x; sz = z; }
                    }
                if (spill < 1e9f) {
                    const float level = spill - 0.002f;
                    std::vector<int> stack{cx + g * cz};
                    seen[static_cast<std::size_t>(cx) + g * cz] = 1;
                    while (!stack.empty()) {
                        const int cur = stack.back();
                        stack.pop_back();
                        const int ux = cur % g, uz = cur / g;
                        if (at(ux, uz) >= level) continue;
                        at(ux, uz) = level;
                        ponds[static_cast<std::size_t>(cur)] = 1;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int x = ux + dx, z = uz + dz;
                                if (x < 1 || z < 1 || x >= g - 1 || z >= g - 1) continue;
                                const std::size_t ni = static_cast<std::size_t>(x) + g * z;
                                if (!seen[ni] && at(x, z) < level) {
                                    seen[ni] = 1;
                                    stack.push_back(static_cast<int>(ni));
                                }
                            }
                    }
                    cx = sx;
                    cz = sz; // continue over the spill cell
                    continue;
                }
                break; // border sink: stop this river
            }
            if (seen[static_cast<std::size_t>(bx) + g * bz]) break; // loop guard
            cx = bx;
            cz = bz;
        }
    }
    // re-normalize the combined flow map
    float maxFlow = 0.f;
    for (const float f : flow) maxFlow = std::max(maxFlow, f);
    if (maxFlow > 0.f)
        for (float& f : flow) f /= maxFlow;
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
    // continentalness spline (Minecraft-style): deep ocean < contOcean, coast band up to
    // contCoast, plains up to contPlains, above that the curve rises steeply to mountains
    Spline1D contSpline;
    contSpline.pts = {{p.contOcean - 0.25f, 0.f}, {p.contOcean, 0.f}, {p.contCoast, 0.03f},
                      {p.contPlains, 0.38f}, {1.0f, 1.0f}};
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
            // 1a. continentalness layer mapped through the spline curve
            const float cont = noise.simplex2(nx * 0.22f + 7.3f, nz * 0.22f + 2.9f); // ~[-1,1]
            const float cu = std::min(1.f, std::max(0.f, cont * 0.5f + 0.5f));
            const float base = contSpline.eval(cu);
            // 1b. erosion map: high value = smooth (meadows, lake flats), low = rugged ridges
            const float ero =
                noise.fbm2(x * p.erosionScale + 31.7f, z * p.erosionScale + 11.2f, 3,
                           Noise::Base::Simplex) * 0.5f + 0.5f;
            const float plains = noise.fbm2(nx, nz, 5, Noise::Base::Simplex) * 0.5f + 0.5f;
            const float ridge = noise.ridged2(nx, nz, p.octaves, Noise::Base::Simplex); // sharp crests
            const float detail = plains * ero + ridge * (1.f - ero);
            // 1c. peaks & valleys: higher-frequency ridged detail scaled by the continental
            //     base, so oceans stay flat next to dramatic ridges
            const float pv =
                noise.ridged2(x * p.pvScale + 3.1f, z * p.pvScale + 8.7f, 4, Noise::Base::Simplex);
            float h = base * (0.55f + 0.45f * detail) + pv * p.pvWeight * base;
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

    // 2b. terracing: quantize high terrain into steps (mesa / canyon / rice-terrace look),
    //     blended by terraceStrength on high ground only
    if (p.terraceSteps > 0) {
        const float steps = static_cast<float>(p.terraceSteps);
        const float strength = std::min(1.f, p.terraceStrength);
        for (float& hv : w.heightmap) {
            const float q = std::floor(hv * steps + 0.5f) / steps;
            const float t = smoothstep(0.5f, 0.85f, hv) * strength;
            hv = hv * (1.f - t) + q * t;
        }
    }

    // 2c. rivers: gradient descent from mountain sources carves V channels into the heightmap
    //     and the flow map; sinks above the water level become filled ponds (mountain lakes)
    carveRivers(w.heightmap, g, p, rng, w.flowmap, w.pondMask);

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

    // 3b. climate maps + Whittaker biome lookup. The continuous temperature/humidity fields
    //     are what an app blends biome colors with (soft borders, no hard edges); the biome id
    //     per cell is for introspection + scatter species.
    if (p.climateMaps) {
        const std::size_t n = static_cast<std::size_t>(g) * g;
        w.temperature.assign(n, 0.f);
        w.humidity.assign(n, 0.f);
        w.biome.assign(n, 0);
        for (int j = 0; j < g; ++j)
            for (int i = 0; i < g; ++i) {
                const float x = (static_cast<float>(i) / (g - 1) - 0.5f) * p.worldSize;
                const float z = (static_cast<float>(j) / (g - 1) - 0.5f) * p.worldSize;
                const std::size_t idx = static_cast<std::size_t>(i) + g * j;
                const float h = w.heightmap[idx];
                // temperature: latitude (equator at the map center) + variation noise - altitude
                const float lat = std::fabs(z) / (p.worldSize * 0.5f);
                float temp = 1.f - lat * 0.85f +
                             noise.fbm2(x * p.tempNoiseScale + 51.3f, z * p.tempNoiseScale + 17.9f, 3,
                                        Noise::Base::Simplex) * 0.3f -
                             h * p.lapseRate;
                temp = std::min(1.f, std::max(0.f, temp));
                // humidity: noise + boost near water + boost along rivers (flow map)
                float hum = noise.fbm2(x * p.humidityNoiseScale + 7.9f, z * p.humidityNoiseScale + 3.1f, 4,
                                       Noise::Base::Simplex) * 0.5f + 0.5f;
                if (h < waterN + 0.03f) hum += 0.35f;
                hum += w.flowmap[idx] * p.humidityRiverBoost;
                hum = std::min(1.f, std::max(0.f, hum));
                w.temperature[idx] = temp;
                w.humidity[idx] = hum;
                // Whittaker lookup: x = humidity, y = temperature
                Biome b;
                if (h < waterN) b = Biome::Ocean;
                else if (std::fabs(h - waterN) < 0.035f) b = Biome::Shore;
                else b = biomeFromClimate(temp, hum);
                // overrides: steep slopes = rock, high altitude = snow
                if (slopeMap[idx] > rockHi) b = Biome::Rock;
                else if (h > 0.8f) b = Biome::Snow;
                w.biome[idx] = static_cast<unsigned char>(b);
            }
    } else {
        w.temperature.clear();
        w.humidity.clear();
        w.biome.clear();
    }

    // 4. poisson-disk scatter weighted by the splatmap (+ biome density): trees on grass,
    //    rocks on rock/steep
    const float half = p.worldSize * 0.5f;
    auto biomeAt = [&](float x, float z) -> int {
        if (w.biome.empty()) return -1;
        const float u = std::min(1.f, std::max(0.f, x / w.sizeX + 0.5f)) * (g - 1);
        const float v = std::min(1.f, std::max(0.f, z / w.sizeZ + 0.5f)) * (g - 1);
        return w.biome[static_cast<std::size_t>(u) + g * static_cast<std::size_t>(v)];
    };
    auto grassWeight = [&](float x, float z) {
        if (w.heightAt(x, z) < w.waterLevelWorld + 0.3f) return 0.f;
        const float u = std::min(1.f, std::max(0.f, x / w.sizeX + 0.5f)) * (g - 1);
        const float v = std::min(1.f, std::max(0.f, z / w.sizeZ + 0.5f)) * (g - 1);
        const std::size_t idx = (static_cast<std::size_t>(u) + g * static_cast<std::size_t>(v)) * 4;
        const float grass = w.splatmap[idx + 0] / 255.f;
        const float rock = w.splatmap[idx + 1] / 255.f;
        float wgt = grass * (1.f - rock);
        const int b = biomeAt(x, z); // biome density: deserts/tundra/snow/rock are sparse
        if (b == static_cast<int>(Biome::Desert)) wgt *= 0.12f;
        else if (b == static_cast<int>(Biome::Tundra)) wgt *= 0.45f;
        else if (b == static_cast<int>(Biome::Snow) || b == static_cast<int>(Biome::Rock)) wgt *= 0.05f;
        return wgt;
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
        // species follows the biome: taiga/tundra -> firs, jungle -> deciduous,
        // otherwise a mixed deciduous/fir/birch roll
        const int tb = biomeAt(s.x, s.y);
        PlantKind kind = PlantKind::Deciduous;
        if (tb == static_cast<int>(Biome::Taiga) || tb == static_cast<int>(Biome::Tundra))
            kind = PlantKind::Fir;
        else {
            const float kindRoll = rng.nextf();
            kind = kindRoll < 0.5f ? PlantKind::Deciduous
                  : kindRoll < 0.75f ? PlantKind::Fir
                                     : PlantKind::Birch;
        }
        pp.kind = kind;
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
    if (!p.allowPathCuts) {
        // paths/parks (plaza tiles) must not interrupt the road network: drop them, green
        // spaces still come from parkFraction block cells
        w.tiles.erase(std::remove_if(w.tiles.begin(), w.tiles.end(),
                                     [](const WfcTile& t) { return t.name == "plaza"; }),
                      w.tiles.end());
    }
    for (const auto& t : w.tiles) wfc.addTile(t);
    w.roadSurface = p.roadSurface;
    w.wetRoads = p.wetRoads;
    w.cells = wfc.solve(p.seed, p.maxBacktracks);
    if (!p.allowPathCuts)
        repairRoadConnectivity(w.cells, w.tiles, gw, gw); // streets stay one uninterrupted network
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
                appendTransformed(w.pavements, lawn, place);
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
                appendTransformed(w.pavements, lot, place);
                // procedural house on the lot. The BuildingContext drives the architecture
                // rules: climate from latitude + noise, Whittaker biome -> local material,
                // density falls off from the city center (zones), wealth from a noise layer
                const float n = noise.value2(wx * 0.05f + 13.1f, wz * 0.05f + 7.7f); // per-block jitter
                const std::uint32_t houseSeed =
                    p.seed ^ 0x11A5u ^ (static_cast<std::uint32_t>(x) * 73856093u) ^
                    (static_cast<std::uint32_t>(z) * 19349663u);
                const float lat = std::fabs(wz) / (size * 0.5f);
                BuildingContext ctx;
                ctx.temperature = std::min(1.f, std::max(0.f, 1.f - lat * 0.85f +
                    noise.fbm2(wx * 0.008f + 51.3f, wz * 0.008f + 17.9f, 3, Noise::Base::Simplex) * 0.3f));
                ctx.precipitation = std::min(1.f, std::max(0.f,
                    noise.fbm2(wx * 0.013f + 3.7f, wz * 0.013f + 9.2f, 4, Noise::Base::Simplex) * 0.5f + 0.45f));
                ctx.humidity = std::min(1.f, std::max(0.f,
                    noise.fbm2(wx * 0.013f + 7.9f, wz * 0.013f + 3.1f, 4, Noise::Base::Simplex) * 0.5f + 0.5f));
                ctx.biome = static_cast<int>(biomeFromClimate(ctx.temperature, ctx.humidity));
                const float dc = std::hypot(wx, wz) / (size * 0.45f);
                ctx.density = std::min(1.f, std::max(0.f, 1.15f - dc +
                    noise.fbm2(wx * 0.02f + 91.7f, wz * 0.02f + 43.1f, 3, Noise::Base::Simplex) * 0.3f));
                ctx.wealth = std::min(1.f, std::max(0.f,
                    noise.fbm2(wx * 0.017f + 31.7f, wz * 0.017f + 11.3f, 3, Noise::Base::Simplex) * 0.7f + 0.45f));
                ctx.soilColor = fuhgl::math::Vec3{0.36f, 0.29f, 0.21f} *
                                (0.85f + 0.3f * noise.value2(wx * 0.09f + 5.5f, wz * 0.09f));
                // zones: an explicit non-default houseStyle wins, otherwise density picks it
                const HouseStyle style =
                    p.houseStyle != HouseStyle::Town ? p.houseStyle
                    : ctx.density < 0.35f ? HouseStyle::Village
                    : ctx.density < 0.65f ? HouseStyle::Town
                                          : HouseStyle::Highrise;
                HouseGenerator::Params hp =
                    HouseGenerator::randomParams(style, p.cellSize, houseSeed, ctx);
                hp.seed = houseSeed;
                hp.width *= (0.8f + 0.4f * n);
                hp.depth *= (0.8f + 0.4f * noise.value2(wz * 0.07f, wx * 0.07f));
                const HouseMesh hm = houseGen.generate(hp);
                PlacedMesh pm;
                pm.mesh = hm.walls;
                pm.detailMesh = hm.details;
                pm.roofMesh = hm.roof;
                pm.glassMesh = hm.glass;
                pm.roofType = static_cast<int>(hm.spec.roofType);
                pm.facade = static_cast<int>(hm.spec.facade);
                pm.position = {wx, 0.05f, wz};
                pm.rotY = -c.rot * fuhgl::math::kPi * 0.5f;
                w.buildings.push_back(std::move(pm));
            } else {
                // street / crossing / T / end: light sidewalk underlay + road slab with
                // irregular edges, world-space uv (texture tiling) and the chosen surface
                // material; occasional pothole = small dark patch slightly above the slab
                fuhgl::render::MeshData walk =
                    fuhgl::render::shapes::boxData({p.cellSize * 0.5f, 0.02f, p.cellSize * 0.5f},
                                                   {0.55f, 0.55f, 0.52f});
                shiftY(walk, 0.02f);
                appendTransformed(w.pavements, walk, place);
                fuhgl::math::Vec3 surfCol;
                float uvScale = 0.25f;
                roadSurfaceStyle(p.roadSurface, p.wetRoads, surfCol, uvScale);
                appendRoadSlab(w.roads, wx, wz, p.cellSize * 0.44f, 0.045f, surfCol,
                               p.roadIrregularity, rng, uvScale);
                if (rng.nextf() < p.roadDamage) {
                    const float ph = 0.22f + 0.25f * rng.nextf();
                    appendRoadSlab(w.roads, wx + (rng.nextf() - 0.5f) * p.cellSize * 0.5f,
                                   wz + (rng.nextf() - 0.5f) * p.cellSize * 0.5f, ph, 0.047f,
                                   {0.05f, 0.05f, 0.06f}, 0.f, rng, uvScale);
                }
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

/// @brief Slab color + uv scale per road surface; wet roads are darker (app adds reflection).
void roadSurfaceStyle(RoadSurface s, bool wet, fuhgl::math::Vec3& color, float& uvScale) {
    switch (s) {
        case RoadSurface::Cobble: color = {0.34f, 0.33f, 0.31f}; uvScale = 0.4f; break;  // ~2.5 wu/tile
        case RoadSurface::Paving: color = {0.46f, 0.45f, 0.43f}; uvScale = 0.5f; break;  // 2 wu/tile
        case RoadSurface::Dirt:   color = {0.36f, 0.28f, 0.18f}; uvScale = 0.33f; break;
        case RoadSurface::Asphalt:
        default:                  color = {0.09f, 0.09f, 0.10f}; uvScale = 0.25f; break; // 4 wu/tile
    }
    if (wet) color = color * 0.55f;
}

bool wfcRoadsConnected(const std::vector<WfcCell>& cells, const std::vector<WfcTile>& tiles,
                       int gridW, int gridH) {
    // true when all road cells (tiles with an "r" edge) form one component via shared "r"
    // edges — i.e. no path/park interrupts the street network (0 or 1 components count as connected)
    std::vector<int> compId;
    return labelRoadComponents(cells, tiles, gridW, gridH, compId).size() <= 1;
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
