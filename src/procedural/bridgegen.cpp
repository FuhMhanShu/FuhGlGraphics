// G38 bridge generation (BridgeGenerator): detection of bridge runs over water / deep ravines
// with shore anchors (Widerlager), span measurement, bridge-type selection (span + tech level
// + biome -> wood beam / stone arch / truss / suspension), the three-level geometry along the
// road spline (deck slab with curbs + drainage gutters + parapets, substructure of arches /
// truss / catenary cables, piers raycast down to the riverbed), abutment blocks + shore
// terrain blending, and entrance props (stone lions, watchtower, toll house, bollards).
// Pure CPU, deterministic per seed — see docs/ROAD_GENERATION.md section 5.

#include <fuhglgraphics/procedural/bridgegen.hpp>

#include <fuhgl/render.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fuhglgraphics::procedural {

namespace {

using fuhgl::math::Vec2;
using fuhgl::math::Vec3;

/// @brief Deterministic splitmix32 RNG (same stream scheme as roadgen/worldgen).
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

/// @brief World XZ -> grid coords (float), clamped.
Vec2 worldToGrid(const RoadInput& in, float x, float z) {
    const float u = clamp01(x / in.sizeX + 0.5f) * (in.gridW - 1);
    const float v = clamp01(z / in.sizeZ + 0.5f) * (in.gridH - 1);
    return {u, v};
}

/// @brief World-space height (bilinear, clamped) from the normalized input heightmap.
float heightWorld(const RoadInput& in, float x, float z) {
    if (!in.heightmap || in.gridW < 2 || in.gridH < 2) return 0.f;
    const Vec2 g = worldToGrid(in, x, z);
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

/// @brief Vertical raycast from (x, z) straight down onto the terrain heightmap — the ground
/// of the river / ravine at that spot (piers always reach exactly into the ground with it).
float raycastGround(const RoadInput& in, float x, float z) { return heightWorld(in, x, z); }

/// @brief Biome code at a world position (nearest cell), -1 when no biome map is set.
int biomeAt(const RoadInput& in, float x, float z) {
    if (!in.biome || in.biome->empty() || in.gridW < 1) return -1;
    const Vec2 g = worldToGrid(in, x, z);
    const std::size_t idx =
        static_cast<std::size_t>(std::min(g.x, in.gridW - 1.f)) +
        static_cast<std::size_t>(in.gridW) * static_cast<std::size_t>(std::min(g.y, in.gridH - 1.f));
    return (*in.biome)[std::min(idx, in.biome->size() - 1)];
}

/// @brief Flow accumulation at a world position (nearest cell), 0 when no flow map is set.
float flowAt(const RoadInput& in, float x, float z) {
    if (!in.flow || in.flow->empty() || in.gridW < 1) return 0.f;
    const Vec2 g = worldToGrid(in, x, z);
    const std::size_t idx =
        static_cast<std::size_t>(std::min(g.x, in.gridW - 1.f)) +
        static_cast<std::size_t>(in.gridW) * static_cast<std::size_t>(std::min(g.y, in.gridH - 1.f));
    return (*in.flow)[std::min(idx, in.flow->size() - 1)];
}

/// @brief Carriageway half width of a road class (same rule as RoadGenerator's profile extrusion).
float classHalfWidth(const RoadGenParams& p, RoadClass cls) {
    switch (cls) {
        case RoadClass::Highway: return p.highwayWidth * 0.5f;
        case RoadClass::Country: return p.countryWidth * 0.5f;
        case RoadClass::Trail:   return p.trailWidth * 0.5f;
    }
    return p.trailWidth * 0.5f;
}

Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float len3(const Vec3& v) { return std::sqrt(dot3(v, v)); }
Vec3 norm3(const Vec3& v) {
    const float l = len3(v);
    return l > 1e-9f ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0.f, 1.f, 0.f};
}

void pushVertex(fuhgl::render::MeshData& md, const Vec3& p, const Vec3& n, const Vec3& c,
                float u, float v) {
    md.vertices.push_back(p.x); md.vertices.push_back(p.y); md.vertices.push_back(p.z);
    md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(u); md.vertices.push_back(v);
}

/// @brief Appends `src` transformed by `m` into `dst` (MeshData, 11 floats/vertex).
void appendTransformed(fuhgl::render::MeshData& dst, const fuhgl::render::MeshData& src,
                       const fuhgl::math::Mat4& m) {
    const fuhgl::math::Mat3 m3(m);
    const std::uint32_t base = static_cast<std::uint32_t>(dst.vertexCount());
    for (std::size_t i = 0; i < src.vertexCount(); ++i) {
        const float* v = &src.vertices[i * 11];
        const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{v[0], v[1], v[2], 1.f};
        const Vec3 n = norm3(m3 * Vec3{v[3], v[4], v[5]});
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

/// @brief One flat-shaded quad p0-p1-p2-p3 (CCW seen from outside); the winding is flipped when
/// the geometric normal points toward `insideRef` (closed tubes stay outward-facing).
void addQuad(fuhgl::render::MeshData& md, const Vec3& p0, const Vec3& p1, const Vec3& p2,
             const Vec3& p3, const Vec3& color, const Vec3& insideRef) {
    Vec3 n = norm3(cross3(Vec3{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z},
                          Vec3{p3.x - p0.x, p3.y - p0.y, p3.z - p0.z}));
    Vec3 fc{(p0.x + p1.x + p2.x + p3.x) * 0.25f, (p0.y + p1.y + p2.y + p3.y) * 0.25f,
            (p0.z + p1.z + p2.z + p3.z) * 0.25f};
    bool flip = dot3(n, Vec3{fc.x - insideRef.x, fc.y - insideRef.y, fc.z - insideRef.z}) < 0.f;
    if (flip) n = Vec3{-n.x, -n.y, -n.z};
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    pushVertex(md, p0, n, color, 0.f, 0.f);
    pushVertex(md, p1, n, color, 1.f, 0.f);
    pushVertex(md, p2, n, color, 1.f, 1.f);
    pushVertex(md, p3, n, color, 0.f, 1.f);
    if (!flip) {
        md.indices.push_back(base); md.indices.push_back(base + 1); md.indices.push_back(base + 2);
        md.indices.push_back(base); md.indices.push_back(base + 2); md.indices.push_back(base + 3);
    } else {
        md.indices.push_back(base); md.indices.push_back(base + 2); md.indices.push_back(base + 1);
        md.indices.push_back(base); md.indices.push_back(base + 3); md.indices.push_back(base + 2);
    }
}

/// @brief Oriented beam (4-sided tube) from `a` to `b` with square cross-section `2*halfThick`.
void addBeam(fuhgl::render::MeshData& md, const Vec3& a, const Vec3& b, float halfThick,
             const Vec3& color) {
    const Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
    const float len = len3(d);
    if (len < 1e-5f) return;
    const Vec3 f{d.x / len, d.y / len, d.z / len};
    Vec3 side = cross3(f, Vec3{0.f, 1.f, 0.f});
    if (len3(side) < 1e-4f) side = {1.f, 0.f, 0.f};
    side = norm3(side);
    const Vec3 up = norm3(cross3(side, f));
    const Vec3 mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
    Vec3 c[2][4];
    for (int e = 0; e < 2; ++e) {
        const Vec3& p = e ? b : a;
        c[e][0] = {p.x + side.x * halfThick + up.x * halfThick,
                   p.y + side.y * halfThick + up.y * halfThick,
                   p.z + side.z * halfThick + up.z * halfThick};
        c[e][1] = {p.x - side.x * halfThick + up.x * halfThick,
                   p.y - side.y * halfThick + up.y * halfThick,
                   p.z - side.z * halfThick + up.z * halfThick};
        c[e][2] = {p.x - side.x * halfThick - up.x * halfThick,
                   p.y - side.y * halfThick - up.y * halfThick,
                   p.z - side.z * halfThick - up.z * halfThick};
        c[e][3] = {p.x + side.x * halfThick - up.x * halfThick,
                   p.y + side.y * halfThick - up.y * halfThick,
                   p.z + side.z * halfThick - up.z * halfThick};
    }
    for (int fIdx = 0; fIdx < 4; ++fIdx) {
        const int f2 = (fIdx + 1) % 4;
        addQuad(md, c[0][fIdx], c[0][f2], c[1][f2], c[1][fIdx], color, mid);
    }
}

/// @brief Arch drop of the soffit at arc position `arc` (0..spanLen): half-sine bays between the
/// piers / abutments — deepest at the springing (pier), shallowest at the crown (mid-bay).
float archDropAt(float arc, float spanLen, const BridgeParams& bp) {
    if (spanLen <= 0.01f) return 0.f;
    const int nBays = std::max(1, static_cast<int>(std::lround(spanLen / bp.pierEvery)));
    const float bay = spanLen / nBays;
    const float u = std::fmod(std::max(0.f, arc), bay) / bay;
    return bp.archHeight * (1.f - std::sin(fuhgl::math::kPi * u));
}

/// @brief Catenary parameter `a` of y = a*cosh(x/a) with sag `depth` over half-span `halfLen`
/// (a*(cosh(halfLen/a) - 1) = depth), solved by bisection — the main-cable shape of a
/// suspension bridge between the pylons.
float catenaryA(float halfLen, float depth) {
    if (halfLen <= 1e-4f || depth <= 1e-4f) return std::max(halfLen, 1e-3f);
    float lo = 1e-3f, hi = std::max(1e4f, halfLen * 100.f);
    for (int it = 0; it < 80; ++it) {
        const float mid = 0.5f * (lo + hi);
        const float f = mid * (std::cosh(halfLen / mid) - 1.f) - depth;
        if (f > 0.f) lo = mid; else hi = mid;
    }
    return 0.5f * (lo + hi);
}

} // namespace

std::vector<BridgeSpan> BridgeGenerator::analyzeRuns(const RoadPath& path, const RoadInput& in,
                                                     const RoadGenParams& p) {
    std::vector<BridgeSpan> spans;
    const std::size_t n = path.points.size();
    if (n < 2 || !in.heightmap) return spans;
    const BridgeParams& bp = p.bridges;
    const float waterWorld = in.waterLevel * in.heightScale;
    const float anchorH = waterWorld + bp.floodMargin; // stable shore point: above water + flood margin

    std::size_t i = 0;
    while (i < n) {
        if (!path.bridge[i]) { ++i; continue; }
        const std::size_t runA = i;
        while (i < n && path.bridge[i]) ++i;
        const std::size_t runB = i - 1; // flagged run [runA..runB]

        // shore anchors (Widerlager points): first probe the bank height within the
        // anchor-search window on both sides, then walk outward to the first stable point
        // above water + flood margin — or, for a dry ravine, above (bank height - ravineDepth/2)
        auto bankHeight = [&](std::size_t from, int dir) {
            float bank = -1e9f, d = 0.f;
            std::size_t k = from;
            while (true) {
                const std::size_t nk = dir < 0 ? (k == 0 ? 0 : k - 1) : (k + 1 >= n ? n - 1 : k + 1);
                if (nk == k) break;
                d += std::hypot(path.points[nk].x - path.points[k].x,
                                path.points[nk].z - path.points[k].z);
                if (d > bp.maxAnchorSearch) break;
                bank = std::max(bank, heightWorld(in, path.points[nk].x, path.points[nk].z));
                k = nk;
            }
            return bank;
        };
        const float anchorH2 = std::max(
            anchorH, std::max(bankHeight(runA, -1), bankHeight(runB, +1)) - bp.ravineDepth * 0.5f);
        std::size_t first = runA, last = runB;
        float d = 0.f;
        while (first > 0) {
            const Vec3& q = path.points[first - 1];
            const float hq = heightWorld(in, q.x, q.z);
            d += std::hypot(q.x - path.points[first].x, q.z - path.points[first].z);
            if (d > bp.maxAnchorSearch) break;
            --first;
            if (hq >= anchorH2) break; // this point is the stable shore anchor
        }
        d = 0.f;
        while (last + 1 < n) {
            const Vec3& q = path.points[last + 1];
            const float hq = heightWorld(in, q.x, q.z);
            d += std::hypot(q.x - path.points[last].x, q.z - path.points[last].z);
            if (d > bp.maxAnchorSearch) break;
            ++last;
            if (hq >= anchorH2) break;
        }

        BridgeSpan span;
        span.first = first;
        span.last = last;
        float spanLen = 0.f;
        for (std::size_t k = first; k < last && k + 1 < n; ++k)
            spanLen += std::hypot(path.points[k + 1].x - path.points[k].x,
                                  path.points[k + 1].z - path.points[k].z);
        span.span = spanLen;
        const Vec3& pa = path.points[first];
        const Vec3& pb = path.points[last];
        span.anchorA = {pa.x, heightWorld(in, pa.x, pa.z), pa.z};
        span.anchorB = {pb.x, heightWorld(in, pb.x, pb.z), pb.z};
        // flat deck at the anchors, at least water + clearance above the water level
        span.deckY = std::max(std::max(span.anchorA.y, span.anchorB.y),
                              waterWorld + bp.deckClearance) + 0.07f;
        // navigable river (high flow at the run center) -> parabolic clearance rise in the middle
        const Vec3& mid = path.points[(first + last) / 2];
        span.navigable = flowAt(in, mid.x, mid.z) >= bp.navigableFlow;
        span.rise = span.navigable ? bp.clearanceRise : 0.f;
        span.biome = biomeAt(in, mid.x, mid.z);
        // deepest ground below the deck along the run (raycast down at every flagged point)
        float maxDepth = 0.f;
        for (std::size_t k = runA; k <= runB && k < n; ++k)
            maxDepth = std::max(maxDepth, span.deckY - raycastGround(in, path.points[k].x,
                                                                    path.points[k].z));
        span.maxDepth = maxDepth;

        // bridge type from span, tech level (Wohlstand) and local resources (biome)
        const bool forest = span.biome == 4 || span.biome == 5 || span.biome == 6;
        const bool rock = span.biome == 8;
        const bool deep = span.maxDepth > bp.ravineDepth;
        if (span.span < bp.woodSpanMax && (forest || bp.techLevel < 0.55f))
            span.type = BridgeType::WoodBeam;   // short span / village / forest: Holzsteg
        else if (span.span <= bp.archSpanMax && !deep && (rock || bp.techLevel < 0.7f))
            span.type = BridgeType::StoneArch;  // 10..60 m / stone region: Steinbogenbruecke
        else if (deep)
            span.type = BridgeType::Suspension; // deep ravine: Haengebruecke, no piers in the deep water
        else
            span.type = BridgeType::Truss;      // > 60 m / high tech level: Fachwerkbruecke
        spans.push_back(span);
    }
    return spans;
}

float BridgeGenerator::deckHeightAt(const BridgeSpan& span, float s) {
    const float u = clamp01(s);
    return span.deckY + span.rise * 4.f * u * (1.f - u); // Parabel-Bogen (schiffbare Durchfahrt)
}

void BridgeGenerator::buildSpan(fuhgl::render::MeshData& md, std::vector<RoadProp>& props,
                                const RoadPath& path, const RoadInput& in, const RoadGenParams& p,
                                const BridgeSpan& span, std::uint32_t seed) const {
    const std::size_t n = path.points.size();
    if (span.last <= span.first || span.last >= n || n < 2) return;
    const BridgeParams& bp = p.bridges;
    Rng rng(seed);
    const float hw = classHalfWidth(p, path.cls);
    const float slab = bp.slabThickness;

    // --- colors per construction type ---
    Vec3 colDeck, colStruct, colParapet, colCurb, colGutter, colSteel;
    switch (span.type) {
        case BridgeType::WoodBeam:
            colDeck = {0.46f, 0.34f, 0.21f}; colStruct = {0.30f, 0.21f, 0.12f};
            colParapet = {0.42f, 0.30f, 0.17f}; colCurb = colParapet; colGutter = {0.24f, 0.17f, 0.10f};
            colSteel = colStruct;
            break;
        case BridgeType::StoneArch:
            colDeck = {0.52f, 0.51f, 0.47f}; colStruct = {0.43f, 0.42f, 0.39f};
            colParapet = {0.57f, 0.56f, 0.52f}; colCurb = {0.60f, 0.59f, 0.55f};
            colGutter = {0.36f, 0.35f, 0.33f}; colSteel = colStruct;
            break;
        case BridgeType::Truss:
        case BridgeType::Suspension:
        default:
            colDeck = {0.28f, 0.28f, 0.30f}; colStruct = {0.50f, 0.50f, 0.48f};
            colParapet = {0.18f, 0.19f, 0.22f}; colCurb = {0.35f, 0.35f, 0.38f};
            colGutter = {0.15f, 0.16f, 0.18f}; colSteel = {0.16f, 0.17f, 0.20f};
            break;
    }

    // --- per-station arc length, deck height, tangent/normal ---
    const std::size_t cnt = span.last - span.first + 1;
    std::vector<float> arc(cnt, 0.f);
    for (std::size_t k = 1; k < cnt; ++k)
        arc[k] = arc[k - 1] + std::hypot(path.points[span.first + k].x - path.points[span.first + k - 1].x,
                                          path.points[span.first + k].z - path.points[span.first + k - 1].z);
    const float spanLen = std::max(arc.back(), 1e-3f);
    auto sAt = [&](std::size_t k) { return clamp01(arc[k] / spanLen); };
    std::vector<Vec3> deckY(cnt), tang(cnt), norm(cnt), bottomY(cnt);
    for (std::size_t k = 0; k < cnt; ++k) {
        const std::size_t gi = span.first + k;
        const Vec3& c = path.points[gi];
        const Vec3& prev = path.points[gi > span.first ? gi - 1 : gi];
        const Vec3& next = path.points[gi + 1 < n ? gi + 1 : gi];
        float tx = next.x - prev.x, tz = next.z - prev.z;
        const float tl = std::hypot(tx, tz);
        if (tl > 1e-4f) { tx /= tl; tz /= tl; }
        tang[k] = {tx, 0.f, tz};
        norm[k] = {-tz, 0.f, tx};
        deckY[k] = {c.x, deckHeightAt(span, sAt(k)), c.z};
        const float drop = span.type == BridgeType::StoneArch ? archDropAt(arc[k], spanLen, bp) : 0.f;
        bottomY[k] = {c.x, deckY[k].y - slab - drop, c.z};
    }
    // interpolated station position at arc length `a` (for piers / pylons / cable samples)
    auto pointAtArc = [&](float a, Vec3& c, Vec3& nr, float& dy) {
        a = std::min(std::max(a, 0.f), arc.back());
        std::size_t k = 0;
        while (k + 1 < cnt && arc[k + 1] < a) ++k;
        const float seg = std::max(arc[k + 1] - arc[k], 1e-5f);
        const float t = (a - arc[k]) / seg;
        c = {path.points[span.first + k].x +
                 (path.points[span.first + k + 1].x - path.points[span.first + k].x) * t,
             0.f,
             path.points[span.first + k].z +
                 (path.points[span.first + k + 1].z - path.points[span.first + k].z) * t};
        nr = {norm[k].x + (norm[k + 1].x - norm[k].x) * t, 0.f,
              norm[k].z + (norm[k + 1].z - norm[k].z) * t};
        dy = deckY[k].y + (deckY[k + 1].y - deckY[k].y) * t;
    };

    // --- 1. bridge deck: closed cross-section loop extruded along the spline ---
    // (t across the half width, y offset vs deck; BOT = soffit bottom, varies per station)
    constexpr float BOT = -1e9f;
    struct SecPt { float t, y; int role; }; // role: 0 deck, 1 structure, 2 parapet, 3 curb, 4 gutter
    const float ph = span.type == BridgeType::WoodBeam ? 0.5f : bp.parapetHeight;
    const float ch = bp.curbHeight;
    const SecPt prof[14] = {
        {-1.00f, BOT, 1}, { 1.00f, BOT, 1},   // 0/1: slab bottom (arch soffit)
        { 1.00f, 0.f, 1},                       // 2: right wall top
        { 1.00f, ph, 2}, { 0.88f, ph, 2}, { 0.88f, 0.05f, 2}, // 3-5: right parapet (Brüstung)
        { 0.80f, 0.02f, 4},                    // 6: right drainage gutter (Entwässerungsrinne)
        { 0.74f, ch, 3}, {-0.74f, ch, 3},      // 7/8: curbs (Bordsteine)
        {-0.80f, 0.02f, 4},                    // 9: left gutter
        {-0.88f, 0.05f, 2}, {-0.88f, ph, 2}, {-1.00f, ph, 2}, // 10-12: left parapet
        {-1.00f, 0.f, 1},                      // 13: left wall top (closes to 0)
    };
    auto secColor = [&](int role) -> Vec3 {
        switch (role) {
            case 1: return colStruct;
            case 2: return colParapet;
            case 3: return colCurb;
            case 4: return colGutter;
            default: return colDeck;
        }
    };
    std::vector<Vec3> secA(14), secB(14);
    for (std::size_t k = 0; k + 1 < cnt; ++k) {
        const Vec3& c = path.points[span.first + k];
        const Vec3& c2 = path.points[span.first + k + 1];
        for (int j = 0; j < 14; ++j) {
            const float yA = prof[j].y == BOT ? bottomY[k].y : deckY[k].y + prof[j].y;
            const float yB = prof[j].y == BOT ? bottomY[k + 1].y : deckY[k + 1].y + prof[j].y;
            secA[j] = {c.x + norm[k].x * prof[j].t * hw, yA, c.z + norm[k].z * prof[j].t * hw};
            secB[j] = {c2.x + norm[k + 1].x * prof[j].t * hw, yB, c2.z + norm[k + 1].z * prof[j].t * hw};
        }
        const Vec3 mid{(c.x + c2.x) * 0.5f, (deckY[k].y + deckY[k + 1].y) * 0.5f,
                       (c.z + c2.z) * 0.5f};
        for (int j = 0; j < 14; ++j) {
            const int j2 = (j + 1) % 14;
            addQuad(md, secA[j], secA[j2], secB[j2], secB[j], secColor(prof[j].role), mid);
        }
    }

    // --- 2. substructure per bridge type ---
    if (span.type == BridgeType::WoodBeam) {
        // wooden piles (eingerammte Holzpfähle) in pairs + cross beams (Querträger) under the deck
        float next = 0.f;
        for (std::size_t k = 0; k < cnt; ++k) {
            if (k > 0 && arc[k] < next) continue;
            next = arc[k] + bp.pileEvery;
            Vec3 c, nr; float dy;
            pointAtArc(arc[k], c, nr, dy);
            for (int side = -1; side <= 1; side += 2) {
                const float px = c.x + nr.x * 0.8f * hw * side;
                const float pz = c.z + nr.z * 0.8f * hw * side;
                const float ground = raycastGround(in, px, pz);
                if (dy - slab * 0.5f - ground > 0.15f)
                    addBeam(md, {px, ground, pz}, {px, dy - slab * 0.5f, pz}, 0.09f, colSteel);
            }
            const Vec3 l{c.x - nr.x * hw, dy - slab, c.z - nr.z * hw};
            const Vec3 r{c.x + nr.x * hw, dy - slab, c.z + nr.z * hw};
            addBeam(md, l, r, 0.07f, colStruct);
        }
    } else if (span.type == BridgeType::StoneArch) {
        // massive piers in the riverbed at the bay boundaries — raycast straight down to the ground
        const int nBays = std::max(1, static_cast<int>(std::lround(spanLen / bp.pierEvery)));
        const float bay = spanLen / nBays;
        for (int b = 1; b < nBays; ++b) {
            Vec3 c, nr; float dy;
            pointAtArc(b * bay, c, nr, dy);
            const float ground = raycastGround(in, c.x, c.z);
            if (dy - ground > 0.2f)
                addBeam(md, {c.x, ground, c.z}, {c.x, dy + 0.05f, c.z}, hw * 0.8f, colStruct);
            // pier cap (Kämpfer) slightly wider than the deck
            addBeam(md, {c.x - nr.x * hw * 1.05f, dy - 0.05f, c.z - nr.z * hw * 1.05f},
                    {c.x + nr.x * hw * 1.05f, dy - 0.05f, c.z + nr.z * hw * 1.05f}, 0.12f, colCurb);
        }
    } else if (span.type == BridgeType::Truss) {
        // steel truss (Fachwerk) on both sides: verticals + top chord + alternating diagonals
        const float th = std::min(bp.trussHeight, spanLen * 0.06f + 1.2f);
        for (std::size_t k = 0; k < cnt; ++k) {
            const Vec3& c = path.points[span.first + k];
            for (int side = -1; side <= 1; side += 2) {
                const Vec3 base{c.x + norm[k].x * 0.95f * hw * side, deckY[k].y,
                                c.z + norm[k].z * 0.95f * hw * side};
                const Vec3 top{base.x, deckY[k].y + th, base.z};
                addBeam(md, base, top, 0.05f, colSteel);
            }
            if (k + 1 < cnt) {
                const Vec3& c2 = path.points[span.first + k + 1];
                for (int side = -1; side <= 1; side += 2) {
                    const Vec3 t0{c.x + norm[k].x * 0.95f * hw * side, deckY[k].y + th,
                                  c.z + norm[k].z * 0.95f * hw * side};
                    const Vec3 t1{c2.x + norm[k + 1].x * 0.95f * hw * side, deckY[k + 1].y + th,
                                  c2.z + norm[k + 1].z * 0.95f * hw * side};
                    addBeam(md, t0, t1, 0.05f, colSteel); // top chord
                    // diagonal brace (alternating direction per segment)
                    const bool flip = (k % 2) == 1;
                    const Vec3 b0{c.x + norm[k].x * 0.95f * hw * side, deckY[k].y,
                                  c.z + norm[k].z * 0.95f * hw * side};
                    const Vec3 b1{c2.x + norm[k + 1].x * 0.95f * hw * side, deckY[k + 1].y,
                                  c2.z + norm[k + 1].z * 0.95f * hw * side};
                    addBeam(md, flip ? t0 : b0, flip ? b1 : t1, 0.035f, colSteel);
                }
            }
        }
    } else { // Suspension: pylons at the shore anchors + catenary main cables + vertical hangers
        const float pylonTop = bp.pylonHeight;
        Vec3 pylonTopPt[2];
        for (int end = 0; end < 2; ++end) {
            const std::size_t k = end == 0 ? 0 : cnt - 1;
            const std::size_t gi = span.first + k;
            const Vec3& c = path.points[gi];
            const float dy = deckY[k].y;
            for (int side = -1; side <= 1; side += 2) {
                const float px = c.x + norm[k].x * 0.85f * hw * side;
                const float pz = c.z + norm[k].z * 0.85f * hw * side;
                const float ground = raycastGround(in, px, pz);
                addBeam(md, {px, ground - 0.5f, pz}, {px, dy + pylonTop, pz}, 0.14f, colSteel);
            }
            // crossbeam on top of the pylon + portal beam at deck level
            const Vec3 l{c.x - norm[k].x * 0.85f * hw, dy + pylonTop, c.z - norm[k].z * 0.85f * hw};
            const Vec3 r{c.x + norm[k].x * 0.85f * hw, dy + pylonTop, c.z + norm[k].z * 0.85f * hw};
            addBeam(md, l, r, 0.11f, colSteel);
            addBeam(md, {l.x, dy + 1.2f, l.z}, {r.x, dy + 1.2f, r.z}, 0.07f, colSteel);
            pylonTopPt[end] = {(l.x + r.x) * 0.5f, dy + pylonTop, (l.z + r.z) * 0.5f};
        }
        // main cables: Kettenlinie y = a*cosh(x/a) between the pylon tops (sag at mid-span),
        // but never lower than deck + 0.6 m (clearance) — sampled every hangerEvery
        const float y0 = pylonTopPt[0].y, y1 = pylonTopPt[1].y;
        const float chordMid = (y0 + y1) * 0.5f;
        float deckMid = 0.f;
        { Vec3 c, nr; float dy; pointAtArc(spanLen * 0.5f, c, nr, dy); deckMid = dy; }
        const float sagTarget = std::max(chordMid - bp.cableSag, deckMid + 0.6f);
        const float depth = std::max(chordMid - sagTarget, 0.05f);
        const float a = catenaryA(spanLen * 0.5f, depth);
        const float coshHalf = std::cosh(spanLen * 0.5f / a);
        auto cableY = [&](float u) { // u in [0,1] along the span
            const float chord = y0 + (y1 - y0) * u;
            const float shape =
                (std::cosh((u - 0.5f) * spanLen / a) - 1.f) / std::max(coshHalf - 1.f, 1e-6f);
            return chord - depth * shape;
        };
        const int nHang = std::max(2, static_cast<int>(std::ceil(spanLen / bp.hangerEvery)));
        for (int side = -1; side <= 1; side += 2) {
            Vec3 prevC{0.f, 0.f, 0.f};
            for (int k = 0; k <= nHang; ++k) {
                const float u = static_cast<float>(k) / nHang;
                Vec3 c, nr; float dy;
                pointAtArc(u * spanLen, c, nr, dy);
                const Vec3 cp{c.x + nr.x * 0.92f * hw * side, cableY(u),
                              c.z + nr.z * 0.92f * hw * side};
                if (k > 0) addBeam(md, prevC, cp, 0.05f, colSteel); // main cable segment
                // vertical hanger: main cable -> deck edge
                const Vec3 deckPt{c.x + nr.x * 0.92f * hw * side, dy, c.z + nr.z * 0.92f * hw * side};
                if (cp.y - deckPt.y > 0.12f) addBeam(md, cp, deckPt, 0.025f, colSteel);
                prevC = cp;
            }
        }
    }

    // --- 3. abutments (Widerlager): massive blocks sunk deep into both banks ---
    for (int end = 0; end < 2; ++end) {
        const std::size_t k = end == 0 ? 0 : cnt - 1;
        const std::size_t gi = span.first + k;
        const Vec3& c = path.points[gi];
        Vec3 outward = end == 0 ? Vec3{-tang[k].x, 0.f, -tang[k].z} : tang[k];
        const float dy = deckY[k].y;
        const float ground = raycastGround(in, c.x, c.z);
        const float top = dy + 0.10f;
        const float bottom = std::min(dy - bp.abutmentDepth, ground - 0.3f);
        const Vec3 center{c.x + outward.x * bp.abutmentSize * 0.35f, (top + bottom) * 0.5f,
                          c.z + outward.z * bp.abutmentSize * 0.35f};
        fuhgl::render::MeshData block = fuhgl::render::shapes::boxData(
            {hw * 1.15f, (top - bottom) * 0.5f, bp.abutmentSize * 0.5f}, colStruct);
        appendTransformed(md, block,
                          fuhgl::math::translate(center) *
                              fuhgl::math::rotateY(std::atan2(tang[k].x, tang[k].z)));
    }

    // --- 4. entrance props (Steinlöwen / Wachturm / Zollhäuschen / Prellsteine) ---
    const std::size_t maxProps = 400;
    auto placeProp = [&](RoadProp::Kind kind, const Vec3& pos, float rotY) {
        if (props.size() >= maxProps) return;
        RoadProp prop;
        prop.kind = kind;
        prop.position = pos;
        prop.rotY = rotY;
        const auto box = [](float hx, float hy, float hz, const Vec3& col, float y0) {
            fuhgl::render::MeshData b = fuhgl::render::shapes::boxData({hx, hy, hz}, col);
            shiftY(b, y0 + hy);
            return b;
        };
        switch (kind) {
            case RoadProp::Kind::Bollard: // Prellstein
                appendTransformed(prop.mesh, box(0.15f, 0.32f, 0.15f, {0.40f, 0.39f, 0.36f}, 0.f),
                                  fuhgl::math::Mat4());
                appendTransformed(prop.mesh, box(0.19f, 0.06f, 0.19f, {0.46f, 0.45f, 0.42f}, 0.32f),
                                  fuhgl::math::Mat4());
                break;
            case RoadProp::Kind::StoneLion: // Steinlöwe: base + body + head
                appendTransformed(prop.mesh, box(0.30f, 0.10f, 0.42f, {0.55f, 0.54f, 0.50f}, 0.f),
                                  fuhgl::math::Mat4());
                appendTransformed(prop.mesh, box(0.22f, 0.36f, 0.28f, {0.58f, 0.57f, 0.53f}, 0.10f),
                                  fuhgl::math::Mat4());
                appendTransformed(prop.mesh, box(0.17f, 0.17f, 0.17f, {0.60f, 0.59f, 0.55f}, 0.46f),
                                  fuhgl::math::Mat4());
                break;
            case RoadProp::Kind::Watchtower: // Wachturm
                appendTransformed(prop.mesh, box(0.45f, 2.4f, 0.45f, {0.40f, 0.30f, 0.18f}, 0.f),
                                  fuhgl::math::Mat4());
                appendTransformed(prop.mesh, box(0.75f, 0.10f, 0.75f, {0.45f, 0.34f, 0.20f}, 4.75f),
                                  fuhgl::math::Mat4());
                appendTransformed(prop.mesh, box(0.85f, 0.28f, 0.85f, {0.30f, 0.22f, 0.14f}, 4.85f),
                                  fuhgl::math::Mat4());
                break;
            case RoadProp::Kind::TollHouse: // Zollhäuschen
                appendTransformed(prop.mesh, box(0.95f, 0.75f, 0.75f, {0.75f, 0.68f, 0.55f}, 0.f),
                                  fuhgl::math::Mat4());
                appendTransformed(prop.mesh, box(1.15f, 0.22f, 0.95f, {0.45f, 0.25f, 0.18f}, 0.75f),
                                  fuhgl::math::Mat4());
                break;
            default:
                break;
        }
        props.push_back(std::move(prop));
    };
    const bool bigBridge = span.span > bp.archSpanMax || span.type == BridgeType::Suspension;
    for (int end = 0; end < 2; ++end) {
        const std::size_t k = end == 0 ? 0 : cnt - 1;
        const std::size_t gi = span.first + k;
        const Vec3& c = path.points[gi];
        const Vec3 outward = end == 0 ? Vec3{-tang[k].x, 0.f, -tang[k].z} : tang[k];
        const float rotY = std::atan2(tang[k].x, tang[k].z);
        const float jitter = (rng.nextf() - 0.5f) * 0.6f;
        const RoadProp::Kind gateKind =
            span.type == BridgeType::WoodBeam ? RoadProp::Kind::Bollard : RoadProp::Kind::StoneLion;
        for (int side = -1; side <= 1; side += 2) {
            const float px = c.x + norm[k].x * (hw + 0.8f + jitter) * side + outward.x * 0.6f;
            const float pz = c.z + norm[k].z * (hw + 0.8f + jitter) * side + outward.z * 0.6f;
            placeProp(gateKind, {px, raycastGround(in, px, pz), pz}, rotY);
        }
        // big bridges get a watchtower (and highways a toll house) at the first entrance
        if (end == 0 && bigBridge) {
            const float px = c.x - norm[k].x * (hw + 1.8f) + outward.x * 1.2f;
            const float pz = c.z - norm[k].z * (hw + 1.8f) + outward.z * 1.2f;
            placeProp(RoadProp::Kind::Watchtower, {px, raycastGround(in, px, pz), pz}, rotY);
            if (path.cls == RoadClass::Highway) {
                const float qx = c.x + norm[k].x * (hw + 2.0f) + outward.x * 1.2f;
                const float qz = c.z + norm[k].z * (hw + 2.0f) + outward.z * 1.2f;
                placeProp(RoadProp::Kind::TollHouse, {qx, raycastGround(in, qx, qz), qz}, rotY);
            }
        }
    }
}

void BridgeGenerator::blendShoreTerrain(std::vector<float>& heightmap, const RoadNetwork& net,
                                        const RoadGenParams& p) {
    if (heightmap.empty() || net.gridW < 3 || net.gridH < 3 || net.paths.empty() ||
        net.heightScale <= 0.f)
        return;
    const int gw = net.gridW, gh = net.gridH;
    const float cx = net.sizeX / (gw - 1), cz = net.sizeZ / (gh - 1);
    const float cell = std::max(1e-4f, std::min(cx, cz));
    const float roadNorm = 1.f / net.heightScale; // world y -> normalized sample
    const float blend = std::max(p.bridges.abutmentBlend, 0.5f);
    const int cr = static_cast<int>(std::ceil(blend / cell)) + 1;
    for (const RoadPath& path : net.paths) {
        for (const BridgeSpan& span : path.spans) {
            for (int end = 0; end < 2; ++end) {
                const Vec3& a = end == 0 ? span.anchorA : span.anchorB;
                const float deckN = span.deckY * roadNorm; // deck approach height (normalized)
                const float u = clamp01(a.x / net.sizeX + 0.5f) * (gw - 1);
                const float v = clamp01(a.z / net.sizeZ + 0.5f) * (gh - 1);
                const int ccx = static_cast<int>(u), ccz = static_cast<int>(v);
                for (int dz = -cr; dz <= cr; ++dz)
                    for (int dx = -cr; dx <= cr; ++dx) {
                        const int x = ccx + dx, z = ccz + dz;
                        // never touch the outermost ring or water cells
                        if (x <= 0 || z <= 0 || x >= gw - 1 || z >= gh - 1) continue;
                        const std::size_t idx = static_cast<std::size_t>(x) + gw * z;
                        float& h = heightmap[idx];
                        if (h < net.waterLevel) continue;
                        if (h >= deckN) continue; // Aufschüttung only raises, never cuts
                        const float wx = (static_cast<float>(x) / (gw - 1) - 0.5f) * net.sizeX;
                        const float wz = (static_cast<float>(z) / (gh - 1) - 0.5f) * net.sizeZ;
                        const float d = std::hypot(wx - a.x, wz - a.z);
                        if (d > blend) continue;
                        // Smoothstep: sanfte Angleichung an die Brückenauffahrt, keine Kanten
                        const float wgt = 1.f - smoothstep(blend * 0.25f, blend, d);
                        h = clamp01(h + (deckN - h) * wgt);
                    }
            }
        }
    }
}

} // namespace fuhglgraphics::procedural
