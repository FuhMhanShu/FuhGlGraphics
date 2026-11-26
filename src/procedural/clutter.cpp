// G39a causal clutter scattering: rockfall at cliff feet (slope map), pebbles/driftwood/
// reeds in riverbeds and at the waterline (flow map), litter under trees (parent-child:
// branches, cones, bark, mushrooms on wet north sides, leaf piles), weeds/gravel/roof tiles/
// crates around buildings and at frayed road edges. Poisson-disk + high-frequency noise
// cluster masks -> nests instead of uniform scatter. Pure CPU, deterministic per seed.

#include <fuhglgraphics/procedural/clutter.hpp>

#include <fuhgl/render.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

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
float clamp01(float x) { return std::min(1.f, std::max(0.f, x)); }

fuhgl::math::Vec2 worldToGrid(const ClutterInput& in, float x, float z) {
    return {clamp01(x / in.sizeX + 0.5f) * (in.gridW - 1),
            clamp01(z / in.sizeZ + 0.5f) * (in.gridH - 1)};
}
fuhgl::math::Vec2 gridToWorld(const ClutterInput& in, float u, float v) {
    return {(u / (in.gridW - 1) - 0.5f) * in.sizeX, (v / (in.gridH - 1) - 0.5f) * in.sizeZ};
}

float heightN(const ClutterInput& in, float x, float z) { // normalized bilinear sample
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
    return (h00 * (1.f - tx) + h10 * tx) * (1.f - tz) + (h01 * (1.f - tx) + h11 * tx) * tz;
}
float heightWorld(const ClutterInput& in, float x, float z) {
    return heightN(in, x, z) * in.heightScale;
}

/// @brief Per-cell world slope (dh/dx) of the input heightmap.
std::vector<float> slopeMapOf(const ClutterInput& in) {
    std::vector<float> slope(static_cast<std::size_t>(in.gridW) * in.gridH, 0.f);
    if (!in.heightmap || in.gridW < 3 || in.gridH < 3) return slope;
    const float cx = in.sizeX / (in.gridW - 1), cz = in.sizeZ / (in.gridH - 1);
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

float sampleMap(const std::vector<float>* m, int g, float size, float x, float z) {
    if (!m || m->empty() || g < 2) return 0.f;
    const fuhgl::math::Vec2 gc = {clamp01(x / size + 0.5f) * (g - 1), clamp01(z / size + 0.5f) * (g - 1)};
    return (*m)[static_cast<std::size_t>(gc.x) + g * static_cast<std::size_t>(gc.y)];
}

/// @brief Compact weighted poisson-disk scatter (dart throwing + spatial hash).
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
        if (weight(x, z) < rng.nextf()) continue;
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

// --- tiny mesh builders ---------------------------------------------------------------

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

fuhgl::render::MeshData boxMesh(const fuhgl::math::Vec3& half, const fuhgl::math::Vec3& color) {
    return fuhgl::render::shapes::boxData(half, color);
}

fuhgl::render::MeshData sphereMesh(float r, int seg, const fuhgl::math::Vec3& color) {
    fuhgl::render::MeshData md;
    const int rings = std::max(3, seg);
    for (int ri = 0; ri <= rings; ++ri) {
        const float phi = fuhgl::math::kPi * ri / rings;
        const float y = std::cos(phi) * r, rr = std::sin(phi) * r;
        for (int s = 0; s <= seg; ++s) {
            const float th = 2.f * fuhgl::math::kPi * s / seg;
            const float x = rr * std::cos(th), z = rr * std::sin(th);
            const fuhgl::math::Vec3 n = fuhgl::math::normalize({x, y, z});
            const float v[11] = {x, y, z, n.x, n.y, n.z, color.x, color.y, color.z,
                                 static_cast<float>(s) / seg, static_cast<float>(ri) / rings};
            md.vertices.insert(md.vertices.end(), v, v + 11);
        }
    }
    for (int ri = 0; ri < rings; ++ri)
        for (int s = 0; s < seg; ++s) {
            const std::uint32_t a = static_cast<std::uint32_t>(ri * (seg + 1) + s);
            const std::uint32_t b = a + seg + 1;
            md.indices.push_back(a); md.indices.push_back(a + 1); md.indices.push_back(b);
            md.indices.push_back(a + 1); md.indices.push_back(b + 1); md.indices.push_back(b);
        }
    return md;
}

/// @brief Builds the local mesh of one clutter kind (base at y = 0, unit-ish size).
fuhgl::render::MeshData clutterMesh(ClutterKind kind, Rng& rng) {
    fuhgl::render::MeshData md;
    const float j = 0.7f + 0.6f * rng.nextf(); // per-item size jitter
    switch (kind) {
        case ClutterKind::Rockfall: { // sharp rock: tilted box
            fuhgl::render::MeshData b = boxMesh({0.22f * j, 0.16f * j, 0.18f * j}, {0.42f, 0.40f, 0.37f});
            appendTransformed(md, b, fuhgl::math::rotate(rng.nextf() * 0.6f, {1.f, 0.f, 0.f}) *
                                       fuhgl::math::rotateY(rng.nextf() * 6.28f));
            break;
        }
        case ClutterKind::Pebble: // smooth, half-sunk sphere
            appendTransformed(md, sphereMesh(0.10f * j, 5, {0.62f, 0.60f, 0.55f}),
                              fuhgl::math::translate({0.f, -0.03f, 0.f}));
            break;
        case ClutterKind::Driftwood: {
            fuhgl::render::MeshData b = boxMesh({0.55f, 0.07f, 0.07f}, {0.32f, 0.24f, 0.16f});
            appendTransformed(md, b, fuhgl::math::rotate(rng.nextf() * 0.25f, {1.f, 0.f, 0.f}) *
                                       fuhgl::math::rotateY(rng.nextf() * 6.28f));
            break;
        }
        case ClutterKind::Reed: { // 3 thin blades
            for (int i = 0; i < 3; ++i) {
                const float h = (0.45f + 0.35f * rng.nextf());
                fuhgl::render::MeshData b = boxMesh({0.015f, h * 0.5f, 0.015f}, {0.20f, 0.42f, 0.16f});
                appendTransformed(md, b, fuhgl::math::translate({(rng.nextf() - 0.5f) * 0.12f, h * 0.5f,
                                                                 (rng.nextf() - 0.5f) * 0.12f}) *
                                           fuhgl::math::rotate((rng.nextf() - 0.5f) * 0.3f, {1.f, 0.f, 0.f}));
            }
            break;
        }
        case ClutterKind::Branch: {
            fuhgl::render::MeshData b = boxMesh({0.30f, 0.035f, 0.035f}, {0.36f, 0.27f, 0.17f});
            appendTransformed(md, b, fuhgl::math::rotate(rng.nextf() * 0.5f, {0.f, 0.f, 1.f}) *
                                       fuhgl::math::rotateY(rng.nextf() * 6.28f));
            break;
        }
        case ClutterKind::Cone:
            appendTransformed(md, sphereMesh(0.055f * j, 5, {0.42f, 0.28f, 0.14f}),
                              fuhgl::math::scale({1.f, 1.4f, 1.f}));
            break;
        case ClutterKind::Bark: {
            fuhgl::render::MeshData b = boxMesh({0.09f, 0.012f, 0.20f}, {0.30f, 0.22f, 0.13f});
            appendTransformed(md, b, fuhgl::math::rotateY(rng.nextf() * 6.28f));
            break;
        }
        case ClutterKind::Mushroom: { // stem + red cap
            fuhgl::render::MeshData stem = boxMesh({0.014f, 0.045f, 0.014f}, {0.85f, 0.82f, 0.72f});
            appendTransformed(md, stem, fuhgl::math::translate({0.f, 0.045f, 0.f}));
            fuhgl::render::MeshData cap = sphereMesh(0.045f * j, 5, {0.72f, 0.16f, 0.10f});
            appendTransformed(md, cap, fuhgl::math::translate({0.f, 0.09f, 0.f}) * fuhgl::math::scale({1.f, 0.55f, 1.f}));
            break;
        }
        case ClutterKind::LeafPile:
            appendTransformed(md, sphereMesh(0.22f * j, 5, {0.48f, 0.33f, 0.14f}),
                              fuhgl::math::scale({1.f, 0.22f, 1.f}));
            break;
        case ClutterKind::WeedTuft: { // 3 thin green blades, like reed but shorter/drier
            for (int i = 0; i < 3; ++i) {
                const float h = (0.16f + 0.14f * rng.nextf());
                fuhgl::render::MeshData b = boxMesh({0.012f, h * 0.5f, 0.012f}, {0.30f, 0.48f, 0.16f});
                appendTransformed(md, b, fuhgl::math::translate({(rng.nextf() - 0.5f) * 0.1f, h * 0.5f,
                                                                 (rng.nextf() - 0.5f) * 0.1f}) *
                                           fuhgl::math::rotate((rng.nextf() - 0.5f) * 0.35f, {1.f, 0.f, 0.f}));
            }
            break;
        }
        case ClutterKind::GravelPile: { // 3 small stones
            for (int i = 0; i < 3; ++i)
                appendTransformed(md, sphereMesh(0.05f * j, 4, {0.55f, 0.53f, 0.48f}),
                                  fuhgl::math::translate({(rng.nextf() - 0.5f) * 0.14f, 0.f,
                                                          (rng.nextf() - 0.5f) * 0.14f}));
            break;
        }
        case ClutterKind::RoofTile: {
            fuhgl::render::MeshData b = boxMesh({0.11f, 0.012f, 0.16f}, {0.55f, 0.20f, 0.13f});
            appendTransformed(md, b, fuhgl::math::rotate(rng.nextf() * 0.4f, {1.f, 0.f, 0.f}) *
                                       fuhgl::math::rotateY(rng.nextf() * 6.28f));
            break;
        }
        case ClutterKind::Crate: {
            fuhgl::render::MeshData b = boxMesh({0.16f, 0.16f, 0.16f}, {0.52f, 0.38f, 0.22f});
            appendTransformed(md, b, fuhgl::math::translate({0.f, 0.16f, 0.f}) *
                                       fuhgl::math::rotateY(rng.nextf() * 6.28f));
            break;
        }
    }
    return md;
}

} // namespace

float ClutterGenerator::rockfallWeight(const ClutterInput& in, const ClutterGenParams& p,
                                       float x, float z) {
    if (!in.heightmap) return 0.f;
    if (heightN(in, x, z) < in.waterLevel + 0.04f) return 0.f; // no talus in/at the water
    const auto slope = slopeMapOf(in);
    const fuhgl::math::Vec2 g = worldToGrid(in, x, z);
    const float s = slope[static_cast<std::size_t>(g.x) + in.gridW * static_cast<std::size_t>(g.y)];
    // talus gathers on and right below cliffs: weight ramps up across the cliff slope band
    return smoothstep(p.cliffSlope * 0.55f, p.cliffSlope * 1.35f, s);
}

float ClutterGenerator::riverbedWeight(const ClutterInput& in, float x, float z) {
    if (!in.heightmap || !in.flow) return 0.f;
    const float h = heightN(in, x, z);
    const float fl = sampleMap(in.flow, in.gridW, in.sizeX, x, z);
    // channels + shore: high flow and at/below the waterline
    if (fl < 0.35f) return 0.f;
    if (h > in.waterLevel + 0.04f) return 0.f;
    return smoothstep(0.35f, 0.7f, fl) * smoothstep(in.waterLevel + 0.04f, in.waterLevel - 0.05f, h);
}

float ClutterGenerator::waterlineWeight(const ClutterInput& in, float x, float z) {
    if (!in.heightmap) return 0.f;
    const float h = heightN(in, x, z);
    const float fl = in.flow ? sampleMap(in.flow, in.gridW, in.sizeX, x, z) : 0.f;
    // shore band just above the waterline with at least some flow (reeds line the banks)
    const float band = smoothstep(in.waterLevel - 0.02f, in.waterLevel + 0.01f, h) *
                       smoothstep(in.waterLevel + 0.08f, in.waterLevel + 0.03f, h);
    return band * (0.4f + 0.6f * smoothstep(0.15f, 0.5f, fl));
}

float ClutterGenerator::treeDistance(const ClutterInput& in, float x, float z) {
    float best = 1e9f;
    for (const auto& t : in.treeSpots) best = std::min(best, std::hypot(t.x - x, t.y - z));
    return best;
}

float ClutterGenerator::buildingDistance(const ClutterInput& in, float x, float z) {
    float best = 1e9f;
    for (const auto& b : in.buildingSpots) best = std::min(best, std::hypot(b.x - x, b.y - z));
    return best;
}

float ClutterGenerator::roadEdgeDistance(const ClutterInput& in, float x, float z) {
    if (!in.roadMask || in.roadMask->empty()) return 1e9f;
    const float m = sampleMap(in.roadMask, in.gridW, in.sizeX, x, z);
    // the frayed rim of the road mask (not the solid center)
    if (m < 0.03f || m > 0.6f) return 1e9f;
    return 0.f;
}

float ClutterGenerator::clusterMask(const ClutterGenParams& p, unsigned seed, float x, float z) {
    Noise n(seed ^ 0xC1u);
    const float v = n.fbm2(x * p.clusterNoiseScale + 3.1f, z * p.clusterNoiseScale + 8.7f, 3,
                           Noise::Base::Simplex) * 0.5f + 0.5f;
    // nothing grows below the threshold; above it the nest density rises with the noise
    return smoothstep(p.clusterThreshold - 0.12f, p.clusterThreshold + 0.08f, v);
}

std::vector<ClutterItem> ClutterGenerator::generate(const ClutterInput& in,
                                                    const ClutterGenParams& p) const {
    std::vector<ClutterItem> out;
    if (!in.heightmap || in.gridW < 3 || in.gridH < 3 || p.maxItems <= 0) return out;
    Rng rng(p.seed);
    const float half = in.sizeX * 0.5f;
    const auto slope = slopeMapOf(in);
    auto slopeAt = [&](float x, float z) {
        const fuhgl::math::Vec2 g = worldToGrid(in, x, z);
        return slope[static_cast<std::size_t>(g.x) + in.gridW * static_cast<std::size_t>(g.y)];
    };
    auto biomeAt = [&](float x, float z) -> int {
        if (!in.biome || in.biome->empty()) return -1;
        const fuhgl::math::Vec2 g = worldToGrid(in, x, z);
        return (*in.biome)[static_cast<std::size_t>(g.x) + in.gridW * static_cast<std::size_t>(g.y)];
    };
    auto humidityAt = [&](float x, float z) {
        return in.humidity ? sampleMap(in.humidity, in.gridW, in.sizeX, x, z) : 0.5f;
    };
    auto pushItem = [&](ClutterKind kind, float x, float z, float yOff, float scaleMul) {
        if (static_cast<int>(out.size()) >= p.maxItems) return;
        ClutterItem it;
        it.kind = kind;
        it.mesh = clutterMesh(kind, rng);
        it.position = {x, heightWorld(in, x, z) + yOff, z};
        it.rotY = rng.nextf() * 2.f * fuhgl::math::kPi;
        it.scale = scaleMul;
        out.push_back(std::move(it));
    };

    // 1. rockfall / talus at cliff feet (slope-driven) in nests
    {
        auto w = [&](float x, float z) {
            if (heightN(in, x, z) < in.waterLevel + 0.04f) return 0.f;
            const float s = slopeAt(x, z);
            const float base = smoothstep(p.cliffSlope * 0.55f, p.cliffSlope * 1.35f, s);
            return base * clusterMask(p, p.seed ^ 0xA1u, x, z);
        };
        for (const auto& s : poissonScatter(half, p.minDist * 1.2f, p.rockfallCount, w, rng))
            pushItem(ClutterKind::Rockfall, s.x, s.y, 0.02f, 0.7f + 0.9f * rng.nextf());
    }
    // 2. riverbed: pebbles + driftwood where the flow map is high (clustered nests)
    {
        auto w = [&](float x, float z) {
            return riverbedWeight(in, x, z) * clusterMask(p, p.seed ^ 0xB2u, x, z);
        };
        for (const auto& s : poissonScatter(half, p.minDist, p.pebbleCount + p.driftwoodCount, w, rng)) {
            const float fl = sampleMap(in.flow, in.gridW, in.sizeX, s.x, s.y);
            const bool wood = fl > 0.7f && rng.nextf() < 0.3f;
            pushItem(wood ? ClutterKind::Driftwood : ClutterKind::Pebble, s.x, s.y,
                     wood ? 0.05f : -0.01f, 0.8f + 0.7f * rng.nextf());
        }
    }
    // 3. reeds at the waterline
    {
        auto w = [&](float x, float z) {
            return waterlineWeight(in, x, z) * clusterMask(p, p.seed ^ 0xC3u, x, z);
        };
        for (const auto& s : poissonScatter(half, p.minDist * 0.8f, p.reedCount, w, rng))
            pushItem(ClutterKind::Reed, s.x, s.y, 0.f, 0.8f + 0.6f * rng.nextf());
    }
    // 4. parent-child: litter under trees (branch/cone/bark/leaf pile) + mushrooms on the
    //    wet north side (humidity-driven)
    for (std::size_t ti = 0; ti < in.treeSpots.size(); ++ti) {
        const float tr = ti < in.treeRadii.size() ? in.treeRadii[ti] : 3.f;
        const fuhgl::math::Vec2& t = in.treeSpots[ti];
        const int b = biomeAt(t.x, t.y);
        const bool conifer = b == 6 || b == 7; // taiga / tundra -> cones
        for (int k = 0; k < p.litterPerTree && static_cast<int>(out.size()) < p.maxItems; ++k) {
            const float ang = rng.nextf() * 2.f * fuhgl::math::kPi;
            const float r = 0.4f + rng.nextf() * tr;
            const float x = t.x + std::cos(ang) * r, z = t.y + std::sin(ang) * r;
            if (heightN(in, x, z) < in.waterLevel + 0.01f) continue;
            const float roll = rng.nextf();
            ClutterKind kind = ClutterKind::Branch;
            if (conifer && roll < 0.35f) kind = ClutterKind::Cone;
            else if (roll < 0.55f) kind = ClutterKind::Branch;
            else if (roll < 0.75f) kind = ClutterKind::Bark;
            else kind = ClutterKind::LeafPile;
            pushItem(kind, x, z, 0.02f, 0.7f + 0.6f * rng.nextf());
        }
        // mushrooms: cluster on the wet side (north = -z), humidity-weighted
        const float hum = humidityAt(t.x, t.y);
        for (int k = 0; k < p.mushroomPerTree && static_cast<int>(out.size()) < p.maxItems; ++k) {
            if (rng.nextf() > hum * 0.9f) continue; // only in damp shade
            const float ang = rng.nextf() * 2.f * fuhgl::math::kPi;
            const float r = 0.5f + rng.nextf() * tr * 0.8f;
            const float x = t.x + std::cos(ang) * r;
            const float z = t.y - std::fabs(std::sin(ang)) * r - 0.3f; // north side
            if (heightN(in, x, z) < in.waterLevel + 0.01f) continue;
            pushItem(ClutterKind::Mushroom, x, z, 0.01f, 0.7f + 0.7f * rng.nextf());
        }
    }
    // 5. building clutter: weeds / gravel / roof tiles / crates in a ring around buildings
    for (const auto& b : in.buildingSpots) {
        for (int k = 0; k < 6 && static_cast<int>(out.size()) < p.maxItems; ++k) {
            const float ang = rng.nextf() * 2.f * fuhgl::math::kPi;
            const float r = 2.5f + rng.nextf() * 4.5f;
            const float x = b.x + std::cos(ang) * r, z = b.y + std::sin(ang) * r;
            if (heightN(in, x, z) < in.waterLevel + 0.02f) continue;
            const float roll = rng.nextf();
            ClutterKind kind = ClutterKind::WeedTuft;
            if (roll < 0.4f) kind = ClutterKind::WeedTuft;
            else if (roll < 0.6f) kind = ClutterKind::GravelPile;
            else if (roll < 0.8f) kind = ClutterKind::RoofTile;
            else kind = ClutterKind::Crate;
            pushItem(kind, x, z, 0.02f, 0.8f + 0.5f * rng.nextf());
        }
    }
    // limit the building pass to its budget
    // (the per-building loop above is bounded by maxItems; scale down if needed)
    // 6. road-edge clutter at the frayed road rim
    if (in.roadMask && !in.roadMask->empty()) {
        auto w = [&](float x, float z) {
            if (roadEdgeDistance(in, x, z) > 0.5f) return 0.f;
            if (heightN(in, x, z) < in.waterLevel + 0.02f) return 0.f;
            return 0.6f * clusterMask(p, p.seed ^ 0xD4u, x, z) + 0.15f;
        };
        for (const auto& s : poissonScatter(half, p.minDist * 0.8f, p.roadEdgeClutterCount, w, rng)) {
            const bool gravel = rng.nextf() < 0.25f;
            pushItem(gravel ? ClutterKind::GravelPile : ClutterKind::WeedTuft, s.x, s.y, 0.02f,
                     0.8f + 0.5f * rng.nextf());
        }
    }
    (void)p.buildingClutterCount; // building loop is per-building; budget enforced via maxItems
    return out;
}

} // namespace fuhglgraphics::procedural
