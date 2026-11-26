// G38 organic road/path network: A* least-cost routing with physics costs (quadratic
// slope, critical-slope penalty -> automatic switchbacks, water/fords/bridges, swamp/
// forest/rock, existing-road merge discount), hierarchy (highway/country/trail), Catmull-Rom
// smoothing with minimum curve radius, cross-section extrusion (crowned highway, wheel-rut
// country road, trodden trail), terrain bench carving (Hanganschnitt/Damm), noise-frayed
// road mask + rut mask for the shaders/scatter, and spline props (milestones, lanterns,
// signs, guard walls). Pure CPU, deterministic per seed — see docs/ROAD_GENERATION.md.

#include <fuhglgraphics/procedural/roadgen.hpp>

#include <fuhgl/render.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace fuhglgraphics::procedural {

namespace {

/// @brief Deterministic splitmix32 RNG (same stream scheme as worldgen).
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
    float nextf() { return static_cast<float>(next() * (1.0 / 4294967296.0)); }
};

float smoothstep(float e0, float e1, float x) {
    const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
    return t * t * (3.f - 2.f * t);
}

float clamp01(float x) { return std::min(1.f, std::max(0.f, x)); }

/// @brief World-space cell size of the input grid (x/z may differ).
float cellX(const RoadInput& in) { return in.gridW > 1 ? in.sizeX / (in.gridW - 1) : 1.f; }
float cellZ(const RoadInput& in) { return in.gridH > 1 ? in.sizeZ / (in.gridH - 1) : 1.f; }

/// @brief World XZ -> grid coords (float), clamped.
fuhgl::math::Vec2 worldToGrid(const RoadInput& in, float x, float z) {
    const float u = clamp01(x / in.sizeX + 0.5f) * (in.gridW - 1);
    const float v = clamp01(z / in.sizeZ + 0.5f) * (in.gridH - 1);
    return {u, v};
}

/// @brief Grid coords (int cell) -> world XZ of the cell center.
fuhgl::math::Vec2 gridToWorld(const RoadInput& in, int u, int v) {
    return {(static_cast<float>(u) / (in.gridW - 1) - 0.5f) * in.sizeX,
            (static_cast<float>(v) / (in.gridH - 1) - 0.5f) * in.sizeZ};
}

/// @brief World-space height (bilinear, clamped) from the normalized input heightmap.
float heightWorld(const RoadInput& in, float x, float z) {
    if (!in.heightmap || in.gridW < 2 || in.gridH < 2) return 0.f;
    const fuhgl::math::Vec2 g = worldToGrid(in, x, z);
    const int x0 = static_cast<int>(g.x), z0 = static_cast<int>(g.y);
    const int x1 = std::min(in.gridW - 1, x0 + 1), z1 = std::min(in.gridH - 1, z0 + 1);
    const float tx = g.x - x0, tz = g.y - z0;
    const auto& h = *in.heightmap;
    const float h00 = h[static_cast<std::size_t>(x0) + in.gridW * z0];
    const float h10 = h[static_cast<std::size_t>(x1) + in.gridW * z0];
    const float h01 = h[static_cast<std::size_t>(x0) + in.gridW * z1];
    const float h11 = h[static_cast<std::size_t>(x1) + in.gridW * z1];
    return ((h00 * (1.f - tx) + h10 * tx) * (1.f - tz) + (h01 * (1.f - tx) + h11 * tx) * tz) *
           in.heightScale;
}

/// @brief Per-cell world slope (dh/dx) of the input heightmap — the A* slope cost reads it.
std::vector<float> slopeMapOf(const RoadInput& in) {
    std::vector<float> slope(static_cast<std::size_t>(in.gridW) * in.gridH, 0.f);
    if (!in.heightmap || in.gridW < 3 || in.gridH < 3) return slope;
    const float cx = cellX(in), cz = cellZ(in);
    const auto& h = *in.heightmap;
    auto at = [&](int x, int z) {
        x = std::min(in.gridW - 1, std::max(0, x));
        z = std::min(in.gridH - 1, std::max(0, z));
        return h[static_cast<std::size_t>(x) + in.gridW * z];
    };
    for (int z = 0; z < in.gridH; ++z)
        for (int x = 0; x < in.gridW; ++x) {
            const float gx = (at(x + 1, z) - at(x - 1, z)) * 0.5f * in.heightScale / cx;
            const float gz = (at(x, z + 1) - at(x, z - 1)) * 0.5f * in.heightScale / cz;
            slope[static_cast<std::size_t>(x) + in.gridW * z] = std::sqrt(gx * gx + gz * gz);
        }
    return slope;
}

/// @brief Critical slope of a road class (world dh/dx; 0.27 ~ 15 deg for carts/cars).
float critSlope(const RoadGenParams& p, RoadClass cls) {
    switch (cls) {
        case RoadClass::Highway: return p.critSlopeHighway;
        case RoadClass::Country: return p.critSlopeCountry;
        case RoadClass::Trail:   return p.critSlopeTrail;
    }
    return p.critSlopeTrail;
}

/// @brief Half width of the road corridor incl. shoulder (mask stamping + carving radius).
float halfWidth(const RoadGenParams& p, RoadClass cls) {
    switch (cls) {
        case RoadClass::Highway: return p.highwayWidth * 0.5f + p.shoulderWidth;
        case RoadClass::Country: return p.countryWidth * 0.5f;
        case RoadClass::Trail:   return p.trailWidth * 0.5f;
    }
    return p.trailWidth * 0.5f;
}

/// @brief Carriageway half width (profile extrusion) of a road class.
float roadHalfWidth(const RoadGenParams& p, RoadClass cls) {
    switch (cls) {
        case RoadClass::Highway: return p.highwayWidth * 0.5f;
        case RoadClass::Country: return p.countryWidth * 0.5f;
        case RoadClass::Trail:   return p.trailWidth * 0.5f;
    }
    return p.trailWidth * 0.5f;
}

/// @brief Minimum curve radius of a road class.
float minRadius(const RoadGenParams& p, RoadClass cls) {
    switch (cls) {
        case RoadClass::Highway: return p.minRadiusHighway;
        case RoadClass::Country: return p.minRadiusCountry;
        case RoadClass::Trail:   return p.minRadiusTrail;
    }
    return p.minRadiusTrail;
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

void pushVertex(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& n,
                const fuhgl::math::Vec3& c, float u, float v) {
    md.vertices.push_back(p.x); md.vertices.push_back(p.y); md.vertices.push_back(p.z);
    md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(u); md.vertices.push_back(v);
}

/// @brief Circumradius of the triangle (a, b, c) — the curve radius through three points.
float circumRadius(const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b,
                   const fuhgl::math::Vec2& c) {
    const float la = std::hypot(b.x - c.x, b.y - c.y);
    const float lb = std::hypot(a.x - c.x, a.y - c.y);
    const float lc = std::hypot(a.x - b.x, a.y - b.y);
    const float area2 = std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
    if (area2 < 1e-9f) return 1e9f; // collinear -> straight
    return la * lb * lc / (2.f * area2);
}

/// @brief Biome code at a world position (nearest cell), -1 when no biome map is set.
/// Biome codes follow worldgen's Biome cast (4 Forest, 5 Jungle, 6 Taiga, 8 Rock, 9 Snow).
int biomeAt(const RoadInput& in, float x, float z) {
    if (!in.biome || in.biome->empty() || in.gridW < 1) return -1;
    const fuhgl::math::Vec2 g = worldToGrid(in, x, z);
    const std::size_t idx =
        static_cast<std::size_t>(std::min(g.x, in.gridW - 1.f)) +
        static_cast<std::size_t>(in.gridW) * static_cast<std::size_t>(std::min(g.y, in.gridH - 1.f));
    return (*in.biome)[std::min(idx, in.biome->size() - 1)];
}

/// @brief Surface color + uv scale per road class, modulated by the local biome
/// (mountains -> stone slabs, forest -> dirt, rich flatland highway -> asphalt/cobble).
/// `rut` darkens the wheel-rut vertices (wet mud / puddles after rain).
void roadClassStyle(RoadClass cls, int biome, bool rut, fuhgl::math::Vec3& color, float& uvScale) {
    switch (cls) {
        case RoadClass::Highway:
            if (biome == 8 || biome == 9) { color = {0.42f, 0.41f, 0.38f}; uvScale = 0.5f; } // stone slabs
            else if (biome == 2)           { color = {0.50f, 0.46f, 0.38f}; uvScale = 0.5f; } // desert paving
            else                           { color = {0.10f, 0.10f, 0.11f}; uvScale = 0.25f; } // asphalt
            break;
        case RoadClass::Country:
            if (biome == 4 || biome == 5 || biome == 6) { color = {0.38f, 0.30f, 0.20f}; uvScale = 0.4f; } // forest dirt
            else                                        { color = {0.46f, 0.42f, 0.33f}; uvScale = 0.4f; } // gravel
            break;
        case RoadClass::Trail:
        default:
            color = {0.40f, 0.32f, 0.22f}; uvScale = 0.5f; // trodden earth
            break;
    }
    if (rut) color = color * 0.5f; // wheel ruts: dark + wet (puddles)
}

} // namespace

float RoadPath::length() const {
    float len = 0.f;
    for (std::size_t i = 1; i < points.size(); ++i)
        len += std::hypot(points[i].x - points[i - 1].x, points[i].z - points[i - 1].z);
    return len;
}

float RoadNetwork::roadDistanceAt(float x, float z) const {
    if (dist.empty() || gridW < 2 || gridH < 2) return 1e9f;
    const float u = clamp01(x / sizeX + 0.5f) * (gridW - 1);
    const float v = clamp01(z / sizeZ + 0.5f) * (gridH - 1);
    const int x0 = static_cast<int>(u), z0 = static_cast<int>(v);
    const int x1 = std::min(gridW - 1, x0 + 1), z1 = std::min(gridH - 1, z0 + 1);
    const float tx = u - x0, tz = v - z0;
    const auto at = [&](int x, int zz) { return dist[static_cast<std::size_t>(x) + gridW * zz]; };
    return (at(x0, z0) * (1.f - tx) + at(x1, z0) * tx) * (1.f - tz) +
           (at(x0, z1) * (1.f - tx) + at(x1, z1) * tx) * tz;
}

std::vector<int> RoadGenerator::leastCostPath(const RoadInput& in, const RoadGenParams& p,
                                              RoadClass cls, fuhgl::math::Vec2 from,
                                              fuhgl::math::Vec2 to,
                                              const std::vector<float>& existingMask) {
    std::vector<int> empty;
    if (!in.heightmap || in.gridW < 2 || in.gridH < 2) return empty;
    const int gw = in.gridW, gh = in.gridH;
    const std::size_t n = static_cast<std::size_t>(gw) * gh;
    const auto& h = *in.heightmap;
    const float cell = (cellX(in) + cellZ(in)) * 0.5f;
    const float waterN = in.waterLevel;
    const float crit = critSlope(p, cls);
    const std::vector<float> slope = slopeMapOf(in);
    auto cellOf = [&](const fuhgl::math::Vec2& w) {
        const fuhgl::math::Vec2 g = worldToGrid(in, w.x, w.y);
        return static_cast<int>(std::min(g.x, gw - 1.001f)) +
               gw * static_cast<int>(std::min(g.y, gh - 1.001f));
    };
    const int start = cellOf(from), goal = cellOf(to);
    if (start == goal) return {start};

    // admissible heuristic: every step costs at least d * (1 - discount) per world unit
    const float disc = 1.f - p.existingRoadDiscount;
    auto heuristic = [&](int idx) {
        const int x = idx % gw, z = idx / gw;
        const int gx = goal % gw, gz = goal / gw;
        return std::hypot(static_cast<float>(x - gx), static_cast<float>(z - gz)) * cell * disc;
    };

    std::vector<float> gScore(n, std::numeric_limits<float>::infinity());
    std::vector<int> came(n, -1);
    std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>,
                        std::greater<std::pair<float, int>>> open;
    gScore[static_cast<std::size_t>(start)] = 0.f;
    open.push({heuristic(start), start});
    int expanded = 0;
    while (!open.empty() && expanded < p.maxExpand) {
        const std::pair<float, int> top = open.top();
        open.pop();
        const int cur = top.second;
        if (top.first > gScore[static_cast<std::size_t>(cur)] + heuristic(cur) + 1e-3f) continue;
        if (cur == goal) break;
        ++expanded;
        const int cx = cur % gw, cz = cur / gw;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dz) continue;
                const int nx = cx + dx, nz = cz + dz;
                if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
                const int ni = nx + gw * nz;
                const float d = cell * std::sqrt(static_cast<float>(dx * dx + dz * dz));
                const float sl = slope[static_cast<std::size_t>(ni)];
                float cost = d * (1.f + p.slopeWeight * sl * sl); // quadratic slope cost
                if (sl > crit) cost *= p.criticalPenalty;         // too steep -> switchback
                const float hv = h[static_cast<std::size_t>(ni)];
                if (hv < waterN) {
                    // water: ford (Furt) at shallow + narrow spots, otherwise bridge-costly
                    const float depth = waterN - hv;
                    const float fl = in.flow ? (*in.flow)[static_cast<std::size_t>(ni)] : 0.f;
                    cost = (depth <= p.fordMaxDepth && fl <= p.fordMaxFlow) ? d * p.fordCost
                                                                             : d * p.waterCost;
                } else {
                    float tf = 1.f;
                    const float fl = in.flow ? (*in.flow)[static_cast<std::size_t>(ni)] : 0.f;
                    if (fl > 0.55f && hv < waterN + 0.04f) tf = p.swampCost; // swamp
                    const fuhgl::math::Vec2 w = gridToWorld(in, nx, nz);
                    const int b = biomeAt(in, w.x, w.y);
                    if (b == 4 || b == 5 || b == 6) tf = std::max(tf, p.forestCost); // dense woods
                    else if (b == 8 || sl > 1.2f) tf = std::max(tf, p.rockCost);     // rock
                    cost *= tf;
                }
                if (!existingMask.empty()) {
                    const float m = existingMask[static_cast<std::size_t>(ni)];
                    cost *= 1.f - p.existingRoadDiscount * m; // merge into existing roads
                }
                const float ng = gScore[static_cast<std::size_t>(cur)] + cost;
                if (ng < gScore[static_cast<std::size_t>(ni)]) {
                    gScore[static_cast<std::size_t>(ni)] = ng;
                    came[static_cast<std::size_t>(ni)] = cur;
                    open.push({ng + heuristic(ni), ni});
                }
            }
    }
    if (goal != start && came[static_cast<std::size_t>(goal)] < 0) return empty;
    std::vector<int> path;
    for (int cur = goal; cur >= 0; cur = came[static_cast<std::size_t>(cur)])
        path.push_back(cur);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<fuhgl::math::Vec2> RoadGenerator::smoothPath(
    const std::vector<fuhgl::math::Vec2>& raw, float step, float minR) {
    if (raw.size() < 3) return raw;
    // 1. Catmull-Rom resample at ~step spacing (endpoint tangents clamped)
    std::vector<fuhgl::math::Vec2> out;
    for (std::size_t i = 0; i + 1 < raw.size(); ++i) {
        const fuhgl::math::Vec2 p0 = raw[i > 0 ? i - 1 : 0];
        const fuhgl::math::Vec2 p1 = raw[i];
        const fuhgl::math::Vec2 p2 = raw[i + 1];
        const fuhgl::math::Vec2 p3 = raw[std::min(raw.size() - 1, i + 2)];
        const float segLen = std::hypot(p2.x - p1.x, p2.y - p1.y);
        const int slices = std::max(1, static_cast<int>(std::ceil(segLen / std::max(0.1f, step))));
        for (int s = 0; s < slices; ++s) {
            const float t = static_cast<float>(s) / slices;
            const float t2 = t * t, t3 = t2 * t;
            out.push_back({
                0.5f * ((2.f * p1.x) + (-p0.x + p2.x) * t +
                        (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2 +
                        (-p0.x + 3.f * p1.x - 3.f * p2.x + p3.x) * t3),
                0.5f * ((2.f * p1.y) + (-p0.y + p2.y) * t +
                        (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2 +
                        (-p0.y + 3.f * p1.y - 3.f * p2.y + p3.y) * t3)});
        }
    }
    out.push_back(raw.back());
    if (out.size() < 3) return out;
    // 2. minimum-curve-radius clamp: pull too-tight middle points toward the chord midpoint
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (std::size_t i = 1; i + 1 < out.size(); ++i) {
            const float r = circumRadius(out[i - 1], out[i], out[i + 1]);
            if (r < minR) {
                const fuhgl::math::Vec2 mid{(out[i - 1].x + out[i + 1].x) * 0.5f,
                                            (out[i - 1].y + out[i + 1].y) * 0.5f};
                const float k = std::min(0.85f, 0.25f + 0.5f * (minR - r) / std::max(minR, 1e-3f));
                out[i] = {out[i].x + (mid.x - out[i].x) * k, out[i].y + (mid.y - out[i].y) * k};
                changed = true;
            }
        }
        if (!changed) break;
    }
    return out;
}

RoadNetwork RoadGenerator::generate(const RoadInput& in, const RoadGenParams& p) const {
    RoadNetwork net;
    net.gridW = in.gridW;
    net.gridH = in.gridH;
    net.sizeX = in.sizeX;
    net.sizeZ = in.sizeZ;
    net.heightScale = in.heightScale;
    net.waterLevel = in.waterLevel;
    const std::size_t n = static_cast<std::size_t>(in.gridW) * in.gridH;
    net.mask.assign(n, 0.f);
    net.rutMask.assign(n, 0.f);
    net.bridgeMask.assign(n, 0);
    net.dist.assign(n, 1e9f);
    if (!in.heightmap || in.gridW < 3 || in.gridH < 3 || in.sites.empty()) return net;

    Rng rng(p.seed);
    Noise noise(p.seed ^ 0xF4Au);
    const float cell = (cellX(in) + cellZ(in)) * 0.5f;
    const float step = std::max(0.8f, cell * 0.9f);
    std::vector<float> built(n, 0.f); // existing-road mask for the merge discount
    if (in.existingMask && in.existingMask->size() == n) built = *in.existingMask;

    // bridge flag per grid cell: roads cross water ONLY as a bridge/footbridge (deck above
    // the water level) — never as a sunken ford ribbon. Shallow/narrow spots (ford criteria)
    // only make the A* prefer them as bridge locations (cheap fordCost), the geometry is
    // always a bridge deck.
    auto isBridgeCell = [&](int idx) {
        return (*in.heightmap)[static_cast<std::size_t>(idx)] < in.waterLevel;
    };

    // stamps one routed path into the network (smoothed spline, bridge flags, masks)
    auto addPath = [&](const std::vector<int>& cells, RoadClass cls, int fromId, int toId) {
        if (cells.size() < 2) return;
        std::vector<fuhgl::math::Vec2> raw;
        raw.reserve(cells.size());
        for (int c : cells) raw.push_back(gridToWorld(in, c % in.gridW, c / in.gridW));
        const auto smooth = smoothPath(raw, step, minRadius(p, cls));
        RoadPath path;
        path.cls = cls;
        path.fromSite = fromId;
        path.toSite = toId;
        path.points.reserve(smooth.size());
        path.bridge.reserve(smooth.size());
        std::size_t rawIdx = 0;
        for (const auto& s : smooth) {
            path.points.push_back({s.x, heightWorld(in, s.x, s.y) + 0.02f, s.y});
            // nearest raw cell (the resample walks the raw polyline monotonically)
            while (rawIdx + 1 < raw.size() &&
                   std::hypot(s.x - raw[rawIdx + 1].x, s.y - raw[rawIdx + 1].y) <
                       std::hypot(s.x - raw[rawIdx].x, s.y - raw[rawIdx].y))
                ++rawIdx;
            path.bridge.push_back(isBridgeCell(cells[rawIdx]) ? 1 : 0);
        }
        // road mask: full strength on the corridor, noise-frayed at the rim (organic edges
        // for the terrain shader's edge blending + scatter suppression)
        const float hw = halfWidth(p, cls);
        const float radius = hw + p.blendMargin;
        const int cr = static_cast<int>(std::ceil(radius / cell)) + 1;
        for (const auto& pt : path.points) {
            const fuhgl::math::Vec2 gc = worldToGrid(in, pt.x, pt.z);
            const int cx = static_cast<int>(gc.x), cz = static_cast<int>(gc.y);
            for (int dz = -cr; dz <= cr; ++dz)
                for (int dx = -cr; dx <= cr; ++dx) {
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || z < 0 || x >= in.gridW || z >= in.gridH) continue;
                    const std::size_t idx = static_cast<std::size_t>(x) + in.gridW * z;
                    const fuhgl::math::Vec2 w = gridToWorld(in, x, z);
                    const float d = std::hypot(w.x - pt.x, w.y - pt.z);
                    float m = 1.f - smoothstep(hw * 0.8f, radius, d);
                    if (m <= 0.f) continue;
                    if (m <= 0.85f) {
                        const float fr =
                            noise.simplex2(w.x * 0.35f + 3.7f, w.y * 0.35f + 9.1f) * 0.5f + 0.5f;
                        m *= 0.45f + 0.95f * fr;
                    }
                    net.mask[idx] = std::max(net.mask[idx], std::min(1.f, m));
                }
        }
        // rut mask: two strips at +-rutOffset of the centerline (country road / trail)
        if (cls != RoadClass::Highway) {
            const float rutOff = p.rutOffset * roadHalfWidth(p, cls);
            const float rutR = 0.45f;
            const int rr = static_cast<int>(std::ceil((rutOff + rutR) / cell)) + 1;
            for (std::size_t i = 0; i < path.points.size(); ++i) {
                const fuhgl::math::Vec3& a = path.points[i];
                const fuhgl::math::Vec3& b = path.points[std::min(i + 1, path.points.size() - 1)];
                float tx = b.x - a.x, tz = b.z - a.z;
                const float tl = std::hypot(tx, tz);
                if (tl < 1e-4f) continue;
                tx /= tl;
                tz /= tl;
                const float nx = -tz, nz = tx;
                for (int side = -1; side <= 1; side += 2) {
                    const float ox = a.x + nx * rutOff * side, oz = a.z + nz * rutOff * side;
                    const fuhgl::math::Vec2 gc = worldToGrid(in, ox, oz);
                    const int cx = static_cast<int>(gc.x), cz = static_cast<int>(gc.y);
                    for (int dz = -rr; dz <= rr; ++dz)
                        for (int dx = -rr; dx <= rr; ++dx) {
                            const int x = cx + dx, z = cz + dz;
                            if (x < 0 || z < 0 || x >= in.gridW || z >= in.gridH) continue;
                            const std::size_t idx = static_cast<std::size_t>(x) + in.gridW * z;
                            const fuhgl::math::Vec2 w = gridToWorld(in, x, z);
                            const float d = std::hypot(w.x - ox, w.y - oz);
                            net.rutMask[idx] =
                                std::max(net.rutMask[idx], 1.f - smoothstep(0.f, rutR, d));
                        }
                }
            }
        }
        // bridge mask: path points over water that is too deep for a ford
        for (std::size_t i = 0; i < path.points.size(); ++i) {
            if (!path.bridge[i]) continue;
            const fuhgl::math::Vec2 gc = worldToGrid(in, path.points[i].x, path.points[i].z);
            const int x = static_cast<int>(gc.x), z = static_cast<int>(gc.y);
            if (x < 0 || z < 0 || x >= in.gridW || z >= in.gridH) continue;
            const std::size_t idx = static_cast<std::size_t>(x) + in.gridW * z;
            if ((*in.heightmap)[idx] < in.waterLevel) net.bridgeMask[idx] = 1;
        }
        // the new road becomes "existing" for the merge discount of the next paths
        for (int c : cells) {
            built[static_cast<std::size_t>(c)] = 1.f;
            const int x = c % in.gridW, z = c / in.gridW;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= in.gridW || nz >= in.gridH) continue;
                    const std::size_t ni = static_cast<std::size_t>(nx) + in.gridW * nz;
                    built[ni] = std::max(built[ni], 0.5f);
                }
        }
        net.paths.push_back(std::move(path));
    };

    // --- hierarchy 1: highways between the major sites (Prim MST over euclidean distance) ---
    std::vector<RoadSite> sites = in.sites;
    std::sort(sites.begin(), sites.end(),
              [](const RoadSite& a, const RoadSite& b) { return a.importance > b.importance; });
    std::vector<int> majors; // indexes into `sites`
    for (std::size_t i = 0; i < sites.size(); ++i)
        if (sites[i].importance >= p.highwayMinImportance) majors.push_back(static_cast<int>(i));
    if (majors.size() < 2 && sites.size() >= 2) majors = {0, 1}; // promote the top two
    std::vector<char> inNet(sites.size(), 0);
    if (majors.size() >= 2) {
        std::vector<char> done(sites.size(), 0);
        done[static_cast<std::size_t>(majors[0])] = 1;
        inNet[static_cast<std::size_t>(majors[0])] = 1;
        for (std::size_t e = 1; e < majors.size(); ++e) {
            int bestFrom = -1, bestTo = -1;
            float bestD = 1e9f;
            for (const int m : majors)
                if (done[static_cast<std::size_t>(m)])
                    for (const int m2 : majors)
                        if (!done[static_cast<std::size_t>(m2)]) {
                            const float d = std::hypot(sites[m].pos.x - sites[m2].pos.x,
                                                       sites[m].pos.y - sites[m2].pos.y);
                            if (d < bestD) { bestD = d; bestFrom = m; bestTo = m2; }
                        }
            if (bestFrom < 0) break;
            const auto cells = leastCostPath(in, p, RoadClass::Highway, sites[bestFrom].pos,
                                             sites[bestTo].pos, built);
            addPath(cells, RoadClass::Highway, sites[bestFrom].id, sites[bestTo].id);
            done[static_cast<std::size_t>(bestTo)] = 1;
            inNet[static_cast<std::size_t>(bestTo)] = 1;
        }
    }
    // --- hierarchy 2: country roads — every remaining site to the nearest connected site ---
    for (std::size_t i = 0; i < sites.size(); ++i) {
        if (inNet[i]) continue;
        int bestTo = -1;
        float bestD = 1e9f;
        for (std::size_t j = 0; j < sites.size(); ++j) {
            if (!inNet[j] || j == i) continue;
            const float d = std::hypot(sites[i].pos.x - sites[j].pos.x,
                                       sites[i].pos.y - sites[j].pos.y);
            if (d < bestD) { bestD = d; bestTo = static_cast<int>(j); }
        }
        if (bestTo < 0 && sites.size() >= 2) bestTo = static_cast<int>(i == 0 ? 1 : 0);
        if (bestTo < 0) break;
        const auto cells = leastCostPath(in, p, RoadClass::Country, sites[i].pos,
                                         sites[static_cast<std::size_t>(bestTo)].pos, built);
        addPath(cells, RoadClass::Country, sites[i].id, sites[bestTo].id);
        inNet[i] = 1;
    }
    // --- hierarchy 3: desire paths — each site to random nearby land POIs ---
    for (const auto& s : sites) {
        for (int k = 0; k < p.desirePathsPerSite; ++k) {
            const float ang = rng.nextf() * 2.f * fuhgl::math::kPi;
            const float len = p.desirePathMinLen +
                              rng.nextf() * (p.desirePathMaxLen - p.desirePathMinLen);
            const fuhgl::math::Vec2 target{s.pos.x + std::cos(ang) * len,
                                           s.pos.y + std::sin(ang) * len};
            const fuhgl::math::Vec2 g = worldToGrid(in, target.x, target.y);
            if (g.x < 1.f || g.y < 1.f || g.x > in.gridW - 2.f || g.y > in.gridH - 2.f) continue;
            const std::size_t ti =
                static_cast<std::size_t>(g.x) + in.gridW * static_cast<std::size_t>(g.y);
            if ((*in.heightmap)[ti] < in.waterLevel + 0.02f) continue; // target must be land
            const auto cells = leastCostPath(in, p, RoadClass::Trail, s.pos, target, built);
            addPath(cells, RoadClass::Trail, s.id, -1);
        }
    }

    // --- distance field: multi-source Dijkstra from the road centerlines (world units) ---
    {
        const float cx = cellX(in), cz = cellZ(in);
        std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>,
                            std::greater<std::pair<float, int>>> open;
        for (std::size_t i = 0; i < n; ++i)
            if (net.mask[i] > 0.85f) {
                net.dist[i] = 0.f;
                open.push({0.f, static_cast<int>(i)});
            }
        while (!open.empty()) {
            const std::pair<float, int> top = open.top();
            open.pop();
            const float d = top.first;
            const int cur = top.second;
            if (d > net.dist[static_cast<std::size_t>(cur)] + 1e-4f) continue;
            const int x = cur % in.gridW, z = cur / in.gridW;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dz) continue;
                    const int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= in.gridW || nz >= in.gridH) continue;
                    const int ni = nx + in.gridW * nz;
                    const float stepLen =
                        (dx && dz) ? 1.41421356f * (cx + cz) * 0.5f : (dx ? cx : cz);
                    const float nd = d + stepLen;
                    if (nd < net.dist[static_cast<std::size_t>(ni)]) {
                        net.dist[static_cast<std::size_t>(ni)] = nd;
                        open.push({nd, ni});
                    }
                }
        }
    }
    return net;
}

void RoadGenerator::carveTerrain(std::vector<float>& heightmap, const RoadNetwork& net,
                                 const RoadGenParams& p) const {
    if (heightmap.empty() || net.gridW < 3 || net.paths.empty() || net.heightScale <= 0.f) return;
    const int gw = net.gridW, gh = net.gridH;
    const float cx = net.sizeX / (gw - 1), cz = net.sizeZ / (gh - 1);
    const float cell = (cx + cz) * 0.5f;
    for (const RoadPath& path : net.paths) {
        const float hw = halfWidth(p, path.cls);
        const float radius = hw + p.blendMargin;
        const int cr = static_cast<int>(std::ceil(radius / cell)) + 1;
        const float roadNorm = 1.f / net.heightScale; // world y -> normalized sample
        for (const auto& pt : path.points) {
            const float roadH = pt.y * roadNorm; // normalized centerline height of the bench
            const fuhgl::math::Vec2 gc =
                {clamp01(pt.x / net.sizeX + 0.5f) * (gw - 1), clamp01(pt.z / net.sizeZ + 0.5f) * (gh - 1)};
            const int ccx = static_cast<int>(gc.x), ccz = static_cast<int>(gc.y);
            for (int dz = -cr; dz <= cr; ++dz)
                for (int dx = -cr; dx <= cr; ++dx) {
                    const int x = ccx + dx, z = ccz + dz;
                    // never touch the outermost ring (island falloff) or water (bridges cross it)
                    if (x <= 0 || z <= 0 || x >= gw - 1 || z >= gh - 1) continue;
                    const std::size_t idx = static_cast<std::size_t>(x) + gw * z;
                    float& h = heightmap[idx];
                    if (h < net.waterLevel) continue;
                    const float wx = (static_cast<float>(x) / (gw - 1) - 0.5f) * net.sizeX;
                    const float wz = (static_cast<float>(z) / (gh - 1) - 0.5f) * net.sizeZ;
                    const float d = std::hypot(wx - pt.x, wz - pt.z);
                    const float m = 1.f - smoothstep(hw * 0.85f, radius, d);
                    if (m <= 0.f) continue;
                    const float dh = h - roadH;
                    if (dh > 0.002f) {
                        // uphill side: cut the bench into the slope (Hanganschnitt)
                        h -= std::min(dh, p.benchCut * roadNorm) * m;
                    } else if (dh < -0.002f) {
                        // downhill side: fill the embankment (Damm)
                        h += std::min(-dh, p.embankment * roadNorm) * m;
                    }
                    h = clamp01(h);
                }
        }
    }
}

void RoadGenerator::buildGeometry(RoadNetwork& net, const RoadInput& in,
                                  const RoadGenParams& p) const {
    if (!in.heightmap || in.gridW < 2 || in.gridH < 2) return;
    Rng rng(p.seed ^ 0xBEEFu);
    const fuhgl::math::Vec3 up{0.f, 1.f, 0.f};
    const std::size_t maxProps = 400;

    // cross-section profiles per class: (t in [-1,1] across the half width, y offset, isRut)
    struct ProfPt { float t, y; bool rut; };
    auto profileOf = [&](RoadClass cls) {
        std::vector<ProfPt> prof;
        if (cls == RoadClass::Highway) {
            // crowned carriageway (rain runs off) + shoulders + drainage ditches
            const float car = p.highwayWidth * 0.5f / halfWidth(p, cls); // carriageway fraction of t
            const float pts[7] = {-1.f, -car, -car * 0.5f, 0.f, car * 0.5f, car, 1.f};
            const float ys[7] = {-0.07f, -0.02f, p.crownHeight * 0.75f, p.crownHeight,
                                 p.crownHeight * 0.75f, -0.02f, -0.07f};
            for (int i = 0; i < 7; ++i) prof.push_back({pts[i], ys[i], false});
        } else if (cls == RoadClass::Country) {
            // two wheel ruts + grass ridge in the middle
            prof.push_back({-1.f, 0.f, false});
            prof.push_back({-p.rutOffset, -p.rutDepth, true});
            prof.push_back({0.f, 0.035f, false});
            prof.push_back({p.rutOffset, -p.rutDepth, true});
            prof.push_back({1.f, 0.f, false});
        } else {
            // trail: one narrow trodden depression
            prof.push_back({-1.f, 0.f, false});
            prof.push_back({0.f, -0.05f, true});
            prof.push_back({1.f, 0.f, false});
        }
        return prof;
    };

    for (RoadPath& path : net.paths) {
        if (path.points.size() < 2) continue;
        const std::vector<ProfPt> prof = profileOf(path.cls);
        const float hw = roadHalfWidth(p, path.cls);
        // refresh the path heights to the (possibly carved) terrain so the road sits on the bench
        for (auto& pt : path.points) pt.y = heightWorld(in, pt.x, pt.z) + 0.07f; // ribbon rides above the terrain mesh (no z-fighting)
        // bridge runs get a flat deck above the water
        std::vector<float> deckY(path.points.size(), 0.f);
        for (std::size_t i = 0; i < path.points.size(); ++i) {
            float deck = path.points[i].y;
            if (path.bridge[i]) {
                const float waterWorld = in.waterLevel * in.heightScale;
                deck = std::max(deck, waterWorld + 0.7f);
            }
            deckY[i] = deck;
        }
        // smooth the deck over short bridge runs (constant per run)
        for (std::size_t i = 0; i < path.points.size(); ++i) {
            if (!path.bridge[i]) continue;
            std::size_t j = i;
            float mx = 0.f;
            while (j < path.points.size() && path.bridge[j]) { mx = std::max(mx, deckY[j]); ++j; }
            for (std::size_t k = i; k < j; ++k) deckY[k] = mx;
            i = j;
        }
        // a point belongs to the bridge mesh when any adjacent segment is a bridge, so
        // emission and segment indexing use the same per-point flag
        std::vector<char> isBridgePt(path.points.size(), 0);
        for (std::size_t i = 0; i < path.points.size(); ++i)
            isBridgePt[i] = (path.bridge[i] || (i > 0 && path.bridge[i - 1]) ||
                             (i + 1 < path.points.size() && path.bridge[i + 1])) ? 1 : 0;
        // extrude the profile along the spline (bridge points go into the bridge mesh,
        // so each point's vertex base is recorded per target mesh)
        std::vector<std::uint32_t> baseRoad(path.points.size(), 0), baseBridge(path.points.size(), 0);
        for (std::size_t i = 0; i < path.points.size(); ++i) {
            const fuhgl::math::Vec3& c = path.points[i];
            const fuhgl::math::Vec3& prev = path.points[(i > 0 ? i - 1 : 0)];
            const fuhgl::math::Vec3& next =
                path.points[std::min(i + 1, path.points.size() - 1)];
            float tx = next.x - prev.x, tz = next.z - prev.z;
            const float tl = std::hypot(tx, tz);
            if (tl > 1e-4f) { tx /= tl; tz /= tl; }
            const float nx = -tz, nz = tx; // left normal in XZ
            const bool br = isBridgePt[i] != 0;
            fuhgl::render::MeshData& md = br ? net.bridgeMesh : net.roadMesh;
            std::uint32_t& base = br ? baseBridge[i] : baseRoad[i];
            base = static_cast<std::uint32_t>(md.vertexCount());
            const int b = biomeAt(in, c.x, c.z);
            for (const ProfPt& pp : prof) {
                fuhgl::math::Vec3 col;
                float uvScale = 0.4f;
                roadClassStyle(path.cls, b, pp.rut, col, uvScale);
                const float off = pp.t * hw;
                const float px = c.x + nx * off, pz = c.z + nz * off;
                // per-vertex terrain sampling: on a cross-slope the uphill ribbon edge must
                // not sink below the terrain surface (bridge decks stay flat)
                const float y = br ? deckY[i] + pp.y
                                   : heightWorld(in, px, pz) + 0.07f + pp.y;
                const fuhgl::math::Vec3 pos{px, y, pz};
                pushVertex(md, pos, up, col, pos.x * uvScale, pos.z * uvScale);
            }
        }
        for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
            const bool br = isBridgePt[i] && isBridgePt[i + 1];
            fuhgl::render::MeshData& md = br ? net.bridgeMesh : net.roadMesh;
            const std::uint32_t a = br ? baseBridge[i] : baseRoad[i];
            const std::uint32_t b2 = br ? baseBridge[i + 1] : baseRoad[i + 1];
            for (std::size_t k = 0; k + 1 < prof.size(); ++k) {
                md.indices.push_back(a + static_cast<std::uint32_t>(k));
                md.indices.push_back(a + static_cast<std::uint32_t>(k) + 1);
                md.indices.push_back(b2 + static_cast<std::uint32_t>(k));
                md.indices.push_back(a + static_cast<std::uint32_t>(k) + 1);
                md.indices.push_back(b2 + static_cast<std::uint32_t>(k) + 1);
                md.indices.push_back(b2 + static_cast<std::uint32_t>(k));
            }
        }
        // bridge posts down to the ground/water every few samples
        if (path.cls == RoadClass::Highway || path.cls == RoadClass::Country)
            for (std::size_t i = 2; i + 1 < path.points.size(); i += 4) {
                if (!path.bridge[i]) continue;
                const fuhgl::math::Vec3& c = path.points[i];
                const float ground = heightWorld(in, c.x, c.z);
                const float postH = std::max(0.3f, deckY[i] - ground);
                fuhgl::render::MeshData post =
                    fuhgl::render::shapes::boxData({0.09f, postH * 0.5f, 0.09f}, {0.30f, 0.24f, 0.18f});
                shiftY(post, postH * 0.5f);
                appendTransformed(net.bridgeMesh, post,
                                  fuhgl::math::translate({c.x, ground, c.z}));
            }

        // --- props along the spline (milestones / lanterns / guard walls) ---
        float sinceMilestone = 0.f, sinceLantern = 0.f;
        int wallEvery = 0;
        for (std::size_t i = 0; i < path.points.size(); ++i) {
            if (net.props.size() >= maxProps) break;
            const fuhgl::math::Vec3& c = path.points[i];
            const fuhgl::math::Vec3& prev = path.points[(i > 0 ? i - 1 : 0)];
            const fuhgl::math::Vec3& next =
                path.points[std::min(i + 1, path.points.size() - 1)];
            float tx = next.x - prev.x, tz = next.z - prev.z;
            const float tl = std::hypot(tx, tz);
            if (tl > 1e-4f) { tx /= tl; tz /= tl; }
            const float nx = -tz, nz = tx;
            const float segLen = std::hypot(c.x - prev.x, c.z - prev.z);
            const bool nearSite = std::any_of(
                in.sites.begin(), in.sites.end(), [&](const RoadSite& s) {
                    return std::hypot(s.pos.x - c.x, s.pos.y - c.z) < p.lanternRadius;
                });
            const float hwFull = halfWidth(p, path.cls);
            // milestone (Meilenstein) every milestoneEvery on highway/country
            if (path.cls != RoadClass::Trail) {
                sinceMilestone += segLen;
                if (sinceMilestone >= p.milestoneEvery) {
                    sinceMilestone = 0.f;
                    const float side = (rng.nextf() < 0.5f) ? -1.f : 1.f;
                    const float j = 1.f + (rng.nextf() - 0.5f) * 2.f * p.propJitter;
                    RoadProp prop;
                    prop.kind = RoadProp::Kind::Milestone;
                    const float ox = c.x + nx * side * (hwFull + 0.5f) * j;
                    const float oz = c.z + nz * side * (hwFull + 0.5f) * j;
                    prop.position = {ox, heightWorld(in, ox, oz), oz};
                    prop.rotY = std::atan2(tx, tz);
                    fuhgl::render::MeshData body =
                        fuhgl::render::shapes::boxData({0.13f, 0.45f, 0.13f}, {0.55f, 0.54f, 0.50f});
                    shiftY(body, 0.45f);
                    fuhgl::render::MeshData cap =
                        fuhgl::render::shapes::boxData({0.17f, 0.07f, 0.17f}, {0.62f, 0.60f, 0.55f});
                    shiftY(cap, 0.93f);
                    appendTransformed(prop.mesh, body, fuhgl::math::Mat4());
                    appendTransformed(prop.mesh, cap, fuhgl::math::Mat4());
                    net.props.push_back(std::move(prop));
                }
            }
            // lantern (Laterne) near settlements on highway/country
            if ((path.cls != RoadClass::Trail) && nearSite) {
                sinceLantern += segLen;
                if (sinceLantern >= p.lanternEvery) {
                    sinceLantern = 0.f;
                    const float side = (rng.nextf() < 0.5f) ? -1.f : 1.f;
                    const float j = 1.f + (rng.nextf() - 0.5f) * 2.f * p.propJitter;
                    RoadProp prop;
                    prop.kind = RoadProp::Kind::Lantern;
                    const float ox = c.x + nx * side * (hwFull + 0.4f) * j;
                    const float oz = c.z + nz * side * (hwFull + 0.4f) * j;
                    prop.position = {ox, heightWorld(in, ox, oz), oz};
                    prop.rotY = std::atan2(tx, tz);
                    fuhgl::render::MeshData pole =
                        fuhgl::render::shapes::boxData({0.05f, 1.15f, 0.05f}, {0.12f, 0.12f, 0.13f});
                    shiftY(pole, 1.15f);
                    fuhgl::render::MeshData head =
                        fuhgl::render::shapes::boxData({0.13f, 0.09f, 0.13f}, {0.95f, 0.82f, 0.45f});
                    shiftY(head, 2.34f);
                    appendTransformed(prop.mesh, pole, fuhgl::math::Mat4());
                    appendTransformed(prop.mesh, head, fuhgl::math::Mat4());
                    net.props.push_back(std::move(prop));
                }
            }
            // guard wall (Steinmauer) where the ground drops steeply at the road edge
            if (++wallEvery >= 3 && !path.bridge[i]) {
                wallEvery = 0;
                for (int side = -1; side <= 1; side += 2) {
                    if (net.props.size() >= maxProps) break;
                    const float ox = c.x + nx * side * (hwFull + 1.1f);
                    const float oz = c.z + nz * side * (hwFull + 1.1f);
                    const float hEdge = heightWorld(in, ox, oz);
                    if (c.y - hEdge > p.wallMinDrop) {
                        RoadProp prop;
                        prop.kind = RoadProp::Kind::Wall;
                        prop.position = {c.x + nx * side * (hwFull + 0.45f),
                                         std::min(hEdge, c.y) + 0.15f,
                                         c.z + nz * side * (hwFull + 0.45f)};
                        prop.rotY = std::atan2(tx, tz);
                        const float len = std::max(0.8f, segLen * 3.f);
                        fuhgl::render::MeshData wall =
                            fuhgl::render::shapes::boxData({0.16f, 0.18f, len * 0.5f},
                                                           {0.48f, 0.47f, 0.44f});
                        appendTransformed(prop.mesh, wall, fuhgl::math::Mat4());
                        net.props.push_back(std::move(prop));
                        break;
                    }
                }
            }
        }
    }

    // --- way signs (Wegweiser) at junctions: closest sample pair of two different paths ---
    {
        int signs = 0;
        const int maxSigns = 16;
        for (std::size_t a = 0; a < net.paths.size() && signs < maxSigns; ++a)
            for (std::size_t b = a + 1; b < net.paths.size() && signs < maxSigns; ++b) {
                float bestD = 2.2f;
                fuhgl::math::Vec3 best{0.f, 0.f, 0.f};
                bool found = false;
                for (const auto& pa : net.paths[a].points)
                    for (const auto& pb : net.paths[b].points) {
                        const float d = std::hypot(pa.x - pb.x, pa.z - pb.z);
                        if (d < bestD) {
                            bestD = d;
                            best = {(pa.x + pb.x) * 0.5f, std::max(pa.y, pb.y), (pa.z + pb.z) * 0.5f};
                            found = true;
                        }
                    }
                if (!found) continue;
                RoadProp prop;
                prop.kind = RoadProp::Kind::Sign;
                prop.position = best;
                prop.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
                fuhgl::render::MeshData post =
                    fuhgl::render::shapes::boxData({0.045f, 0.85f, 0.045f}, {0.25f, 0.20f, 0.14f});
                shiftY(post, 0.85f);
                fuhgl::render::MeshData board =
                    fuhgl::render::shapes::boxData({0.42f, 0.16f, 0.03f}, {0.65f, 0.55f, 0.30f});
                shiftY(board, 1.78f);
                appendTransformed(prop.mesh, post, fuhgl::math::Mat4());
                appendTransformed(prop.mesh, board, fuhgl::math::Mat4());
                net.props.push_back(std::move(prop));
                ++signs;
            }
    }
}

} // namespace fuhglgraphics::procedural
