// Pipeline-based procedural city: terrain+zones (G24 fBm heightmap, flattened plateau, lake,
// density/wealth zone grids) -> warped road grid as planar graph (water-masked) -> block cells
// OBB-split into lots (half-plane splits along the longest PCA axis, ear-clipped slabs,
// boundary-touching lots get access, interior lots become parks) -> HouseGenerator per lot with
// the style taken from the density zone. Deterministic per seed; pure CPU; self-contained 2D
// polygon helpers (ear clipping, half-plane clip, OBB via 2x2 PCA) — no external library.

#include <fuhglgraphics/procedural/citygen.hpp>
#include <fuhglgraphics/procedural/lsystem.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fuhglgraphics::procedural {

namespace {

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

float cross2(const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b) { return a.x * b.y - a.y * b.x; }
float dot2(const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b) { return a.x * b.x + a.y * b.y; }

float signedArea(const std::vector<fuhgl::math::Vec2>& poly) {
    float a = 0.f;
    for (std::size_t i = 0; i < poly.size(); ++i) a += cross2(poly[i], poly[(i + 1) % poly.size()]);
    return 0.5f * a;
}

fuhgl::math::Vec2 centroid(const std::vector<fuhgl::math::Vec2>& poly) {
    fuhgl::math::Vec2 c{0.f, 0.f};
    for (const auto& p : poly) c = c + p;
    return c / static_cast<float>(poly.size());
}

float distPointSeg(const fuhgl::math::Vec2& p, const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b) {
    const fuhgl::math::Vec2 ab = b - a;
    const float len2 = dot2(ab, ab);
    const float t = len2 > 0.f ? std::min(1.f, std::max(0.f, dot2(p - a, ab) / len2)) : 0.f;
    const fuhgl::math::Vec2 q = a + ab * t;
    return std::sqrt(dot2(p - q, p - q));
}

void pushFlatVertex(fuhgl::render::MeshData& md, float x, float y, float z,
                    const fuhgl::math::Vec3& c, float uvScale = 1.f) {
    md.vertices.push_back(x); md.vertices.push_back(y); md.vertices.push_back(z);
    md.vertices.push_back(0.f); md.vertices.push_back(1.f); md.vertices.push_back(0.f);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(x * uvScale); md.vertices.push_back(z * uvScale);
}

// --- stage 1: terrain + zones --------------------------------------------------------------

void buildTerrainAndZones(const CityGenParams& p, CityWorld& w) {
    Noise noise(p.seed);
    const int g = std::max(2, p.heightGrid);
    w.gridW = g;
    w.gridH = g;
    w.sizeX = p.worldSize;
    w.sizeZ = p.worldSize;
    w.heightScale = p.heightScale;
    w.waterLevelWorld = p.waterLevel * p.heightScale;
    w.heightmap.resize(static_cast<std::size_t>(g) * g);
    w.density.resize(static_cast<std::size_t>(g) * g);
    w.wealth.resize(static_cast<std::size_t>(g) * g);
    const float plateau = p.waterLevel * 1.05f; // normalized plateau height (just above water)
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(g - 1);
            const float v = static_cast<float>(j) / static_cast<float>(g - 1);
            const float x = (u - 0.5f) * p.worldSize, z = (v - 0.5f) * p.worldSize;
            float nx = x * p.noiseScale, nz = z * p.noiseScale;
            if (p.warp > 0.f) {
                const fuhgl::math::Vec2 wp = noise.warp2(nx, nz, p.warp, 3);
                nx = wp.x;
                nz = wp.y;
            }
            float h = std::pow(noise.ridged2(nx, nz, 6, Noise::Base::Simplex), 0.85f);
            // fade to 0 at the world border (island), then flatten the city plateau
            const float dc = std::hypot(x, z);
            const float edge = std::max(std::fabs(u - 0.5f), std::fabs(v - 0.5f)) * 2.f;
            h *= 1.f - smoothstep(0.82f, 1.f, edge);
            const float flat = smoothstep(p.cityRadius, p.cityRadius * 0.75f, dc);
            h = h * (1.f - flat) + plateau * flat;
            if (h < p.waterLevel) h *= 0.15f; // lake bed
            const std::size_t idx = static_cast<std::size_t>(i) + g * j;
            w.heightmap[idx] = h;
            // zones: density falls off from the center and is modulated by noise (districts),
            // wealth is an independent noise layer
            const float dNorm = 1.f - std::min(1.f, dc / p.cityRadius);
            const float dens = std::min(1.f, std::max(0.f, dNorm * (0.55f + 0.45f * noise.fbm2(x * 0.02f + 31.7f, z * 0.02f, 4, Noise::Base::Simplex))));
            w.density[idx] = dens;
            w.wealth[idx] = std::min(1.f, std::max(0.f, 0.5f + 0.5f * noise.fbm2(x * 0.017f, z * 0.017f + 11.3f, 4, Noise::Base::Perlin)));
        }
}

// --- stage 2: road graph --------------------------------------------------------------------

RoadGraph buildRoads(const CityGenParams& p, const CityWorld& w, fuhgl::render::MeshData& roadMesh,
                        fuhgl::render::MeshData& sidewalkMesh) {
    Noise warpNoise(p.seed ^ 0xB0ADu);
    RoadGraph graph;
    const int n = std::max(2, p.roadGrid);
    const float extent = p.cityRadius * 2.f;
    std::vector<int> nodeId(static_cast<std::size_t>(n) * n, -1);
    const float plateauY = w.waterLevelWorld * 1.05f;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            fuhgl::math::Vec2 pos{(static_cast<float>(i) / (n - 1) - 0.5f) * extent,
                                  (static_cast<float>(j) / (n - 1) - 0.5f) * extent};
            // warp the grid so streets follow the terrain/coast instead of a rigid lattice
            const float cell = extent / (n - 1);
            const fuhgl::math::Vec2 wp = warpNoise.warp2(pos.x * 0.01f, pos.y * 0.01f, cell * 0.35f, 2);
            pos = pos + wp;
            if (w.heightAt(pos.x, pos.y) < w.waterLevelWorld + 0.1f) continue; // no roads in the lake
            nodeId[static_cast<std::size_t>(i) + n * j] = static_cast<int>(graph.nodes.size());
            graph.nodes.push_back(pos);
        }
    auto edge = [&](int i, int j, int i2, int j2) {
        const int a = nodeId[static_cast<std::size_t>(i) + n * j];
        const int b = nodeId[static_cast<std::size_t>(i2) + n * j2];
        if (a >= 0 && b >= 0) graph.edges.emplace_back(a, b);
    };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            if (i + 1 < n) edge(i, j, i + 1, j);
            if (j + 1 < n) edge(i, j, i, j + 1);
        }

    // road mesh: per edge a street quad on a wider sidewalk quad; per node a square.
    // Street quads get irregular (jittered) edges, the chosen surface material with
    // world-space uv, and occasional pothole patches (small dark quads).
    fuhgl::math::Vec3 surfCol;
    float uvScale = 0.25f;
    roadSurfaceStyle(p.roadSurface, p.wetRoads, surfCol, uvScale);
    const fuhgl::math::Vec3 walk{0.55f, 0.55f, 0.52f};
    Rng rng(p.seed ^ 0xB0A2u);
    auto addQuad = [&](fuhgl::render::MeshData& dst, const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b,
                       float width, float y, const fuhgl::math::Vec3& col, float uvs, float jitter) {
        const fuhgl::math::Vec2 dir = b - a;
        const float len = std::sqrt(dot2(dir, dir));
        if (len < 1e-4f) return;
        const fuhgl::math::Vec2 side{-dir.y / len, dir.x / len}; // perpendicular
        const float hw = width * 0.5f;
        const float j = jitter;
        const fuhgl::math::Vec2 p0 = a + side * hw + fuhgl::math::Vec2{(rng.nextf() - 0.5f) * 2.f * j, (rng.nextf() - 0.5f) * 2.f * j};
        const fuhgl::math::Vec2 p1 = a - side * hw + fuhgl::math::Vec2{(rng.nextf() - 0.5f) * 2.f * j, (rng.nextf() - 0.5f) * 2.f * j};
        const fuhgl::math::Vec2 p2 = b - side * hw + fuhgl::math::Vec2{(rng.nextf() - 0.5f) * 2.f * j, (rng.nextf() - 0.5f) * 2.f * j};
        const fuhgl::math::Vec2 p3 = b + side * hw + fuhgl::math::Vec2{(rng.nextf() - 0.5f) * 2.f * j, (rng.nextf() - 0.5f) * 2.f * j};
        const std::uint32_t base = static_cast<std::uint32_t>(dst.vertexCount());
        pushFlatVertex(dst, p0.x, y, p0.y, col, uvs);
        pushFlatVertex(dst, p1.x, y, p1.y, col, uvs);
        pushFlatVertex(dst, p2.x, y, p2.y, col, uvs);
        pushFlatVertex(dst, p3.x, y, p3.y, col, uvs);
        dst.indices.push_back(base); dst.indices.push_back(base + 2); dst.indices.push_back(base + 1);
        dst.indices.push_back(base); dst.indices.push_back(base + 3); dst.indices.push_back(base + 2);
    };
    for (const auto& e : graph.edges) {
        const fuhgl::math::Vec2 a = graph.nodes[e.first], b = graph.nodes[e.second];
        addQuad(sidewalkMesh, a, b, p.roadWidth + 1.6f, plateauY + 0.02f, walk, 1.f, 0.f); // sidewalk underlay
        addQuad(roadMesh, a, b, p.roadWidth, plateauY + 0.05f, surfCol, uvScale, p.roadIrregularity);
        if (rng.nextf() < p.roadDamage) {
            // pothole: small dark square somewhere along the street segment
            const float t = 0.25f + 0.5f * rng.nextf();
            const fuhgl::math::Vec2 c = a + (b - a) * t;
            const float hs = 0.25f + 0.3f * rng.nextf();
            const std::uint32_t base = static_cast<std::uint32_t>(roadMesh.vertexCount());
            const fuhgl::math::Vec3 dark{0.05f, 0.05f, 0.06f};
            pushFlatVertex(roadMesh, c.x - hs, plateauY + 0.055f, c.y - hs, dark, uvScale);
            pushFlatVertex(roadMesh, c.x + hs, plateauY + 0.055f, c.y - hs, dark, uvScale);
            pushFlatVertex(roadMesh, c.x + hs, plateauY + 0.055f, c.y + hs, dark, uvScale);
            pushFlatVertex(roadMesh, c.x - hs, plateauY + 0.055f, c.y + hs, dark, uvScale);
            roadMesh.indices.push_back(base); roadMesh.indices.push_back(base + 2); roadMesh.indices.push_back(base + 1);
            roadMesh.indices.push_back(base); roadMesh.indices.push_back(base + 3); roadMesh.indices.push_back(base + 2);
        }
    }
    for (const auto& nd : graph.nodes) {
        const float h = p.roadWidth * 0.5f + 0.8f;
        const fuhgl::math::Vec2 c = nd;
        const std::uint32_t base = static_cast<std::uint32_t>(sidewalkMesh.vertexCount());
        pushFlatVertex(sidewalkMesh, c.x - h, plateauY + 0.02f, c.y - h, walk);
        pushFlatVertex(sidewalkMesh, c.x + h, plateauY + 0.02f, c.y - h, walk);
        pushFlatVertex(sidewalkMesh, c.x + h, plateauY + 0.02f, c.y + h, walk);
        pushFlatVertex(sidewalkMesh, c.x - h, plateauY + 0.02f, c.y + h, walk);
        sidewalkMesh.indices.push_back(base); sidewalkMesh.indices.push_back(base + 2); sidewalkMesh.indices.push_back(base + 1);
        sidewalkMesh.indices.push_back(base); sidewalkMesh.indices.push_back(base + 3); sidewalkMesh.indices.push_back(base + 2);
    }
    return graph;
}

// --- stage 3: blocks -> lots -----------------------------------------------------------------

void splitIntoLots(const std::vector<fuhgl::math::Vec2>& block, const CityGenParams& p,
                   const std::vector<fuhgl::math::Vec2>& blockBoundary, int depth,
                   std::vector<Lot>& out) {
    const Obb2 obb = computeObb(block);
    const float longSide = std::max(obb.halfU, obb.halfV) * 2.f;
    const float area = std::fabs(signedArea(block));
    if ((longSide <= p.lotMaxSize && depth > 0) || depth >= 6 || area < p.lotMinSize * p.lotMinSize * 0.4f) {
        Lot lot;
        lot.poly = block;
        // access: any vertex close to the block boundary (which is bounded by roads)
        for (const auto& v : block) {
            for (std::size_t i = 0; i < blockBoundary.size(); ++i) {
                if (distPointSeg(v, blockBoundary[i], blockBoundary[(i + 1) % blockBoundary.size()]) < 0.35f) {
                    lot.hasAccess = true;
                    break;
                }
            }
            if (lot.hasAccess) break;
        }
        // interior lots become parks; tiny lots with road access become "path" lots reaching
        // the street ONLY when allowPathCuts is set — otherwise roads stay uninterrupted
        const bool tiny = area < p.lotMinSize * p.lotMinSize * 0.6f;
        if (!lot.hasAccess || (tiny && p.allowPathCuts)) lot.park = true;
        out.push_back(std::move(lot));
        return;
    }
    // split along the longest OBB axis through the center
    const fuhgl::math::Vec2 c = obb.center;
    const fuhgl::math::Vec2 dir = obb.halfU >= obb.halfV ? obb.axisU : obb.axisV;
    const fuhgl::math::Vec2 a = c, b = c + dir;
    std::vector<fuhgl::math::Vec2> left = clipHalfPlane(block, a, b);   // cross(dir, p-a) >= 0
    std::vector<fuhgl::math::Vec2> right = clipHalfPlane(block, b, a);  // the other half
    if (left.size() >= 3 && right.size() >= 3) {
        splitIntoLots(left, p, blockBoundary, depth + 1, out);
        splitIntoLots(right, p, blockBoundary, depth + 1, out);
    } else {
        Lot lot;
        lot.poly = block;
        lot.hasAccess = true;
        out.push_back(std::move(lot));
    }
}

fuhgl::render::MeshData buildLotMesh(const std::vector<Lot>& lots, float y) {
    fuhgl::render::MeshData md;
    for (const Lot& lot : lots) {
        const std::vector<std::uint32_t> tris = earClip(lot.poly);
        const fuhgl::math::Vec3 col = lot.park ? fuhgl::math::Vec3{0.16f, 0.42f, 0.16f}
                                              : fuhgl::math::Vec3{0.48f, 0.47f, 0.44f};
        const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
        for (const auto& v : lot.poly) pushFlatVertex(md, v.x, y, v.y, col);
        for (const std::uint32_t idx : tris) md.indices.push_back(base + idx);
    }
    return md;
}

} // namespace

// --- polygon helpers (public) ----------------------------------------------------------------

std::vector<std::uint32_t> earClip(const std::vector<fuhgl::math::Vec2>& poly) {
    std::vector<std::uint32_t> out;
    const std::size_t n = poly.size();
    if (n < 3) return out;
    std::vector<std::uint32_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = static_cast<std::uint32_t>(i);
    if (signedArea(poly) < 0.f) std::reverse(idx.begin(), idx.end()); // work CCW
    std::size_t guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const std::uint32_t a = idx[(i + idx.size() - 1) % idx.size()];
            const std::uint32_t b = idx[i];
            const std::uint32_t c = idx[(i + 1) % idx.size()];
            const fuhgl::math::Vec2 ab = poly[b] - poly[a], bc = poly[c] - poly[b];
            if (cross2(ab, bc) <= 0.f) continue; // reflex corner
            bool inside = false;
            const fuhgl::math::Vec2 ca = poly[a] - poly[c];
            for (const std::uint32_t o : idx) {
                if (o == a || o == b || o == c) continue;
                // point-in-triangle via three cross products
                const float d1 = cross2(ab, poly[o] - poly[a]);
                const float d2 = cross2(bc, poly[o] - poly[b]);
                const float d3 = cross2(ca, poly[o] - poly[c]);
                if (d1 >= 0.f && d2 >= 0.f && d3 >= 0.f) { inside = true; break; }
            }
            if (inside) continue;
            out.push_back(a); out.push_back(b); out.push_back(c);
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) {
        out.push_back(idx[0]); out.push_back(idx[1]); out.push_back(idx[2]);
    }
    return out;
}

std::vector<fuhgl::math::Vec2> clipHalfPlane(const std::vector<fuhgl::math::Vec2>& poly,
                                             const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b) {
    // keep the left side of a->b: cross(b-a, p-a) >= 0
    std::vector<fuhgl::math::Vec2> out;
    const fuhgl::math::Vec2 d = b - a;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const fuhgl::math::Vec2 p = poly[i], q = poly[(i + 1) % n];
        const float dp = cross2(d, p - a), dq = cross2(d, q - a);
        if (dp >= 0.f) out.push_back(p);
        if ((dp >= 0.f) != (dq >= 0.f)) {
            const float t = dp / (dp - dq);
            out.push_back(p + (q - p) * t);
        }
    }
    return out;
}

Obb2 computeObb(const std::vector<fuhgl::math::Vec2>& pts) {
    Obb2 obb;
    if (pts.empty()) return obb;
    const fuhgl::math::Vec2 c = centroid(pts);
    obb.center = c;
    // PCA: covariance eigenvector = longest axis
    float xx = 0.f, xy = 0.f, yy = 0.f;
    for (const auto& p : pts) {
        const float dx = p.x - c.x, dy = p.y - c.y;
        xx += dx * dx; xy += dx * dy; yy += dy * dy;
    }
    const float tr = xx + yy;
    const float det = xx * yy - xy * xy;
    const float disc = std::sqrt(std::max(0.f, tr * tr * 0.25f - det));
    const float lambda = tr * 0.5f + disc; // largest eigenvalue
    fuhgl::math::Vec2 u{1.f, 0.f};
    if (std::fabs(xy) > 1e-9f) u = {lambda - yy, xy};
    else if (xx >= yy) u = {1.f, 0.f};
    else u = {0.f, 1.f};
    const float lu = std::sqrt(dot2(u, u));
    u = lu > 0.f ? u / lu : fuhgl::math::Vec2{1.f, 0.f};
    obb.axisU = u;
    obb.axisV = {-u.y, u.x};
    float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
    for (const auto& p : pts) {
        const float du = dot2(p - c, obb.axisU), dv = dot2(p - c, obb.axisV);
        minU = std::min(minU, du); maxU = std::max(maxU, du);
        minV = std::min(minV, dv); maxV = std::max(maxV, dv);
    }
    obb.halfU = (maxU - minU) * 0.5f;
    obb.halfV = (maxV - minV) * 0.5f;
    obb.center = c + obb.axisU * ((maxU + minU) * 0.5f) + obb.axisV * ((maxV + minV) * 0.5f);
    return obb;
}

// --- CityWorld samplers -----------------------------------------------------------------------

namespace {
float bilinear(const std::vector<float>& grid, int g, float size, float x, float z) {
    if (g < 2 || grid.size() != static_cast<std::size_t>(g) * g) return 0.f;
    const float u = std::min(1.f, std::max(0.f, x / size + 0.5f));
    const float v = std::min(1.f, std::max(0.f, z / size + 0.5f));
    const float fx = u * (g - 1), fz = v * (g - 1);
    const int x0 = static_cast<int>(fx), z0 = static_cast<int>(fz);
    const int x1 = std::min(g - 1, x0 + 1), z1 = std::min(g - 1, z0 + 1);
    const float tx = fx - x0, tz = fz - z0;
    const float h00 = grid[static_cast<std::size_t>(x0) + g * z0];
    const float h10 = grid[static_cast<std::size_t>(x1) + g * z0];
    const float h01 = grid[static_cast<std::size_t>(x0) + g * z1];
    const float h11 = grid[static_cast<std::size_t>(x1) + g * z1];
    return (h00 * (1.f - tx) + h10 * tx) * (1.f - tz) + (h01 * (1.f - tx) + h11 * tx) * tz;
}
} // namespace

float CityWorld::heightAt(float x, float z) const { return bilinear(heightmap, gridW, sizeX, x, z) * heightScale; }
float CityWorld::densityAt(float x, float z) const { return bilinear(density, gridW, sizeX, x, z); }
float CityWorld::wealthAt(float x, float z) const { return bilinear(wealth, gridW, sizeX, x, z); }

// --- the pipeline -----------------------------------------------------------------------------

CityWorld generateCityWorld(const CityGenParams& p) {
    CityWorld w;
    buildTerrainAndZones(p, w);
    w.roadSurface = p.roadSurface;
    w.wetRoads = p.wetRoads;
    w.roads = buildRoads(p, w, w.roadMesh, w.sidewalkMesh);

    // blocks = grid cells whose four corner nodes survived the water mask; rebuild the
    // (i,j) -> node-id mapping exactly like buildRoads did (same scan order + warp + mask)
    const int n = std::max(2, p.roadGrid);
    std::vector<int> nodeId(static_cast<std::size_t>(n) * n, -1);
    {
        Noise warpNoise(p.seed ^ 0xB0ADu);
        const float extent = p.cityRadius * 2.f;
        const float cell = extent / (n - 1);
        int next = 0;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                fuhgl::math::Vec2 pos{(static_cast<float>(i) / (n - 1) - 0.5f) * extent,
                                      (static_cast<float>(j) / (n - 1) - 0.5f) * extent};
                const fuhgl::math::Vec2 wp = warpNoise.warp2(pos.x * 0.01f, pos.y * 0.01f, cell * 0.35f, 2);
                pos = pos + wp;
                if (w.heightAt(pos.x, pos.y) < w.waterLevelWorld + 0.1f) continue;
                nodeId[static_cast<std::size_t>(i) + n * j] = next++;
            }
    }

    const float plateauY = w.waterLevelWorld * 1.05f;
    Noise climate(p.seed ^ 0xC11u); // climate/soil noise for the house BuildingContexts
    HouseGenerator houseGen;
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const int n00 = nodeId[static_cast<std::size_t>(i) + n * j];
            const int n10 = nodeId[static_cast<std::size_t>(i + 1) + n * j];
            const int n11 = nodeId[static_cast<std::size_t>(i + 1) + n * (j + 1)];
            const int n01 = nodeId[static_cast<std::size_t>(i) + n * (j + 1)];
            if (n00 < 0 || n10 < 0 || n11 < 0 || n01 < 0) continue;
            std::vector<fuhgl::math::Vec2> block = {w.roads.nodes[n00], w.roads.nodes[n10],
                                                    w.roads.nodes[n11], w.roads.nodes[n01]};
            if (signedArea(block) < 0.f) std::reverse(block.begin(), block.end()); // CCW
            std::vector<Lot> blockLots;
            splitIntoLots(block, p, block, 0, blockLots);
            for (Lot& lot : blockLots) {
                const Obb2 obb = computeObb(lot.poly);
                if (!lot.park) {
                    // stage 4: house with the style from the density zone at the lot center.
                    // BuildingContext: density/wealth from the zone maps, climate from noise
                    // (latitude + variation), Whittaker biome -> local material, water
                    // proximity near the lake -> stilts, soil color from noise
                    const float dens = w.densityAt(obb.center.x, obb.center.y);
                    const HouseStyle style = dens < 0.35f ? HouseStyle::Village
                                           : dens < 0.65f ? HouseStyle::Town
                                                          : HouseStyle::Highrise;
                    const std::uint32_t lotSeed =
                        p.seed ^ 0xCAFEu ^ static_cast<std::uint32_t>(obb.center.x * 131.f) ^
                        static_cast<std::uint32_t>(obb.center.y * 57.f);
                    const float cx = obb.center.x, cz = obb.center.y;
                    const float lat = std::fabs(cz) / (p.worldSize * 0.5f);
                    BuildingContext ctx;
                    ctx.temperature = std::min(1.f, std::max(0.f, 1.f - lat * 0.85f +
                        climate.fbm2(cx * 0.008f + 51.3f, cz * 0.008f + 17.9f, 3, Noise::Base::Simplex) * 0.3f));
                    ctx.precipitation = std::min(1.f, std::max(0.f,
                        climate.fbm2(cx * 0.013f + 3.7f, cz * 0.013f + 9.2f, 4, Noise::Base::Simplex) * 0.5f + 0.45f));
                    ctx.humidity = std::min(1.f, std::max(0.f,
                        climate.fbm2(cx * 0.013f + 7.9f, cz * 0.013f + 3.1f, 4, Noise::Base::Simplex) * 0.5f + 0.5f));
                    ctx.biome = static_cast<int>(biomeFromClimate(ctx.temperature, ctx.humidity));
                    ctx.density = dens;
                    ctx.wealth = w.wealthAt(cx, cz);
                    ctx.waterProximity =
                        w.heightAt(cx, cz) < w.waterLevelWorld + 0.8f ? 0.85f : 0.f;
                    ctx.soilColor = fuhgl::math::Vec3{0.36f, 0.29f, 0.21f} *
                                    (0.85f + 0.3f * climate.value2(cx * 0.09f + 5.5f, cz * 0.09f));
                    HouseGenerator::Params hp = HouseGenerator::randomParams(style, 8.f, lotSeed, ctx);
                    hp.seed = lotSeed;
                    hp.width = std::max(2.5f, obb.halfU * 2.f * p.houseCoverage);
                    hp.depth = std::max(2.5f, obb.halfV * 2.f * p.houseCoverage);
                    if (style == HouseStyle::Highrise) hp.floors = 5 + static_cast<int>(dens * 8.f);
                    hp.clutter = p.houseClutter;
                    const HouseMesh hm = houseGen.generate(hp);
                    PlacedMesh pm;
                    pm.mesh = hm.walls;
                    pm.detailMesh = hm.details;
                    pm.roofMesh = hm.roof;
                    pm.glassMesh = hm.glass;
                    pm.interiorMesh = hm.interior;
                    pm.doorMesh = hm.doors;
                    pm.roofType = static_cast<int>(hm.spec.roofType);
                    pm.facade = static_cast<int>(hm.spec.facade);
                    pm.position = {obb.center.x, plateauY + 0.06f, obb.center.y};
                    pm.rotY = std::atan2(obb.axisU.y, obb.axisU.x);
                    w.houses.push_back(std::move(pm));
                } else {
                    // park lot: a couple of bushes on the lawn slab
                    Rng rng(p.seed ^ static_cast<std::uint32_t>(obb.center.x * 17.f) ^
                            static_cast<std::uint32_t>(obb.center.y * 29.f));
                    const int bushes = 1 + static_cast<int>(rng.next() % 3u);
                    for (int b = 0; b < bushes; ++b) {
                        PlacedMesh pm;
                        pm.mesh = LSystem::makeBush().generate();
                        pm.position = {obb.center.x + (rng.nextf() - 0.5f) * obb.halfU,
                                       plateauY + 0.08f,
                                       obb.center.y + (rng.nextf() - 0.5f) * obb.halfV};
                        pm.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
                        pm.scale = 0.5f + 0.5f * rng.nextf();
                        w.houses.push_back(std::move(pm)); // reuse the list for park greenery
                    }
                }
                w.lots.push_back(std::move(lot));
            }
        }
    w.lotMesh = buildLotMesh(w.lots, plateauY + 0.04f);
    return w;
}

} // namespace fuhglgraphics::procedural
