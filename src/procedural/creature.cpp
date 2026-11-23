// G30 procedural creatures: skeleton graph -> world-space SDF prims (ellipsoid per node +
// tapered capsule per parent link, broadphase AABBs) -> smooth-min join + simplex noise
// detail -> G25 marching cubes with analytic gradient normals, per-vertex curvature (SDF
// Laplacian) in the color channel and cylindrical UVs -> CPU bake of albedo/normal maps from
// 3D object-space noise (seamless in u). Pure CPU, deterministic per seed.

#include <fuhglgraphics/procedural/creature.hpp>

#include <fuhglgraphics/procedural/marching_cubes.hpp>
#include <fuhglgraphics/procedural/noise.hpp>
#include <fuhglgraphics/procedural/sdf.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>

namespace fuhglgraphics::procedural {

using fuhgl::math::Vec3;

namespace {

constexpr float kTwoPi = 2.f * fuhgl::math::kPi;

float clamp01(float v) { return fuhgl::math::clamp(v, 0.f, 1.f); }

/// @brief Tapered capsule SDF: segment a->b, radius lerp ra->rb.
float capsuleSdf(const Vec3& p, const Vec3& a, const Vec3& b, float ra, float rb) {
    const Vec3 ab = b - a;
    const float l2 = fuhgl::math::dot(ab, ab);
    const float t = l2 > 1e-9f ? fuhgl::math::clamp(fuhgl::math::dot(p - a, ab) / l2, 0.f, 1.f) : 0.f;
    const float r = ra + (rb - ra) * t;
    return fuhgl::math::length((p - a) - ab * t) - r;
}

/// @brief Approximate ellipsoid SDF centered at c with radii r*ell (scaled-sphere estimate,
/// exact enough for meshing): negative inside.
float ellipsoidSdf(const Vec3& p, const Vec3& c, float r, const Vec3& ell) {
    const Vec3 q{(p.x - c.x) / (r * ell.x), (p.y - c.y) / (r * ell.y), (p.z - c.z) / (r * ell.z)};
    const float m = std::fmin(ell.x, std::fmin(ell.y, ell.z));
    return (fuhgl::math::length(q) - 1.f) * r * m;
}

/// @brief Deterministic 3D cell hash -> feature point in [0,1]^3.
Vec3 cellHash(int ix, int iy, int iz, unsigned seed) {
    auto mix = [](std::uint32_t z) {
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return (z ^ (z >> 15));
    };
    std::uint32_t h = seed + 0x9E3779B9u * static_cast<std::uint32_t>(ix * 73856093u ^
                                 static_cast<std::uint32_t>(iy) * 19349663u ^
                                 static_cast<std::uint32_t>(iz) * 83492791u);
    const float fx = static_cast<float>(mix(h)) / 4294967296.f;
    const float fy = static_cast<float>(mix(h + 1u)) / 4294967296.f;
    const float fz = static_cast<float>(mix(h + 2u)) / 4294967296.f;
    return {fx, fy, fz};
}

/// @brief Worley F1 (distance to nearest feature point), range ~[0, 1.5].
float worley3(const Vec3& p, unsigned seed) {
    const int ix = static_cast<int>(std::floor(p.x));
    const int iy = static_cast<int>(std::floor(p.y));
    const int iz = static_cast<int>(std::floor(p.z));
    float best = 8.f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Vec3 f = cellHash(ix + dx, iy + dy, iz + dz, seed);
                const Vec3 d = Vec3{static_cast<float>(dx) + f.x - (p.x - ix),
                                    static_cast<float>(dy) + f.y - (p.y - iy),
                                    static_cast<float>(dz) + f.z - (p.z - iz)};
                best = std::fmin(best, fuhgl::math::length(d));
            }
    return best;
}

/// @brief Analytic SDF gradient (central differences).
Vec3 sdfGradient(const std::function<float(const Vec3&)>& f, const Vec3& p, float h) {
    const float fx = f({p.x + h, p.y, p.z}) - f({p.x - h, p.y, p.z});
    const float fy = f({p.x, p.y + h, p.z}) - f({p.x, p.y - h, p.z});
    const float fz = f({p.x, p.y, p.z + h}) - f({p.x, p.y, p.z - h});
    return {fx / (2.f * h), fy / (2.f * h), fz / (2.f * h)};
}

/// @brief SDF Laplacian (central differences) — approx 2*mean curvature at the surface.
float sdfLaplacian(const std::function<float(const Vec3&)>& f, const Vec3& p, float h) {
    const float c = f(p);
    float sum = -6.f * c;
    sum += f({p.x + h, p.y, p.z}) + f({p.x - h, p.y, p.z});
    sum += f({p.x, p.y + h, p.z}) + f({p.x, p.y - h, p.z});
    sum += f({p.x, p.y, p.z + h}) + f({p.x, p.y, p.z - h});
    return sum / (h * h);
}

} // namespace

// --- Skeleton -------------------------------------------------------------------

int Skeleton::addNode(const std::string& name, int parent, const Vec3& offset, float radius,
                      const Vec3& ellipsoid, BodyPart part, float blend) {
    SkeletonNode n;
    n.name = name;
    n.parent = parent;
    n.offset = offset;
    n.radius = radius;
    n.ellipsoid = ellipsoid;
    n.blend = blend;
    n.part = part;
    nodes.push_back(n);
    m_finalized = false;
    return static_cast<int>(nodes.size()) - 1;
}

void Skeleton::finalize() {
    const int n = static_cast<int>(nodes.size());
    m_world.assign(n, Vec3{0.f, 0.f, 0.f});
    for (int i = 0; i < n; ++i) {
        const SkeletonNode& nd = nodes[i];
        m_world[i] = (nd.parent >= 0 && nd.parent < i) ? m_world[nd.parent] + nd.offset : nd.offset;
    }
    m_prims.clear();
    m_prims.reserve(n * 2);
    for (int i = 0; i < n; ++i) {
        const SkeletonNode& nd = nodes[i];
        const Vec3 c = m_world[i];
        // ellipsoid prim at the node
        Prim e;
        e.a = c;
        e.ra = nd.radius;
        e.capsule = false;
        e.ell = nd.ellipsoid;
        e.blend = nd.blend;
        e.part = nd.part;
        const Vec3 ext = Vec3{nd.radius * nd.ellipsoid.x, nd.radius * nd.ellipsoid.y,
                              nd.radius * nd.ellipsoid.z};
        e.aabbMin = c - ext;
        e.aabbMax = c + ext;
        m_prims.push_back(e);
        // tapered capsule to the parent (limbs / neck / tail links)
        if (nd.parent >= 0 && nd.parent < i) {
            Prim cp;
            cp.a = c;
            cp.b = m_world[nd.parent];
            cp.ra = nd.radius * 0.85f;
            cp.rb = nodes[nd.parent].radius * 0.85f;
            cp.capsule = true;
            cp.ell = {1.f, 1.f, 1.f};
            cp.blend = nd.blend;
            cp.part = nd.part;
            const float r = std::fmax(cp.ra, cp.rb);
            cp.aabbMin = Vec3{std::fmin(c.x, cp.b.x) - r, std::fmin(c.y, cp.b.y) - r,
                              std::fmin(c.z, cp.b.z) - r};
            cp.aabbMax = Vec3{std::fmax(c.x, cp.b.x) + r, std::fmax(c.y, cp.b.y) + r,
                              std::fmax(c.z, cp.b.z) + r};
            m_prims.push_back(cp);
        }
    }
    m_finalized = true;
}

Vec3 Skeleton::worldPos(int node) const {
    return (node >= 0 && node < static_cast<int>(m_world.size())) ? m_world[node] : Vec3{0.f, 0.f, 0.f};
}

void Skeleton::bounds(Vec3& mn, Vec3& mx) const {
    mn = Vec3{1e9f, 1e9f, 1e9f};
    mx = Vec3{-1e9f, -1e9f, -1e9f};
    for (const Prim& p : m_prims) {
        mn = {std::fmin(mn.x, p.aabbMin.x), std::fmin(mn.y, p.aabbMin.y), std::fmin(mn.z, p.aabbMin.z)};
        mx = {std::fmax(mx.x, p.aabbMax.x), std::fmax(mx.y, p.aabbMax.y), std::fmax(mx.z, p.aabbMax.z)};
    }
}

// --- SDF ------------------------------------------------------------------------

namespace {

/// @brief Hot-path SDF evaluation sharing one Noise instance (perm table built once).
float creatureSdfImpl(const Skeleton& skel, const CreatureParams& pr, const Vec3& p,
                      const Noise& nz) {
    float d = 1e9f;
    const float margin = pr.smoothK + pr.detailAmp * 2.f;
    for (const Skeleton::Prim& prim : skel.prims()) {
        // broadphase: skip prims whose expanded AABB does not contain p
        if (p.x < prim.aabbMin.x - margin || p.x > prim.aabbMax.x + margin ||
            p.y < prim.aabbMin.y - margin || p.y > prim.aabbMax.y + margin ||
            p.z < prim.aabbMin.z - margin || p.z > prim.aabbMax.z + margin)
            continue;
        float pd = prim.capsule ? capsuleSdf(p, prim.a, prim.b, prim.ra, prim.rb)
                                : ellipsoidSdf(p, prim.a, prim.ra, prim.ell);
        d = sdf::smoothMin(d, pd, pr.smoothK * prim.blend);
    }
    // geometry detail: simplex fBm in [-1, 1] modulates the field (scales/ribs/folds)
    d += pr.detailAmp * nz.fbm3(p.x * pr.detailFreq, p.y * pr.detailFreq, p.z * pr.detailFreq,
                                pr.detailOctaves, Noise::Base::Simplex);
    return d;
}

} // namespace

float creatureSdf(const Skeleton& skel, const CreatureParams& pr, const Vec3& p) {
    const Noise nz(pr.seed);
    return creatureSdfImpl(skel, pr, p, nz);
}

// --- meshing --------------------------------------------------------------------

CreatureMesh meshCreature(const Skeleton& skel, const CreatureParams& pr, int resolution,
                          const Vec3& baseColor, float curvatureGain) {
    CreatureMesh out;
    if (!skel.finalized() || resolution < 8) return out;
    skel.bounds(out.bmin, out.bmax);
    const float margin = 0.25f + pr.smoothK + pr.detailAmp * 2.f;
    out.bmin = out.bmin - Vec3{margin, margin, margin};
    out.bmax = out.bmax + Vec3{margin, margin, margin};
    const Noise nz(pr.seed);
    const auto field = [&](const Vec3& p) { return creatureSdfImpl(skel, pr, p, nz); };
    const VolumeGrid grid = sampleField(field, out.bmin, out.bmax, resolution, resolution, resolution);
    out.mesh = marchingCubes(grid, 0.f, baseColor);
    if (out.mesh.indices.empty()) return out;

    // post-process: analytic normals, curvature shading into the color channel, cylindrical UVs
    const float h = (out.bmax.x - out.bmin.x) / static_cast<float>(resolution - 1);
    const float invSpanZ = 1.f / std::fmax(1e-6f, out.bmax.z - out.bmin.z);
    for (std::uint32_t i = 0; i < out.mesh.vertexCount(); ++i) {
        float* v = &out.mesh.vertices[i * 11];
        const Vec3 p{v[0], v[1], v[2]};
        Vec3 n = sdfGradient(field, p, h * 0.5f);
        const float nl = fuhgl::math::length(n);
        n = nl > 1e-9f ? n / nl : Vec3{0.f, 1.f, 0.f};
        const float H = 0.5f * sdfLaplacian(field, p, h); // ~mean curvature at the surface
        float shade = fuhgl::math::clamp(1.f + curvatureGain * H * h, 0.05f, 2.f);
        v[3] = n.x; v[4] = n.y; v[5] = n.z;
        v[6] = baseColor.x * shade;
        v[7] = baseColor.y * shade;
        v[8] = baseColor.z * shade;
        const float u = std::atan2(p.y, p.x) / kTwoPi + 0.5f; // around the body axis (+Z)
        const float vv = (p.z - out.bmin.z) * invSpanZ;
        v[9] = u;
        v[10] = vv;
    }
    return out;
}

// --- texture baking ---------------------------------------------------------------

CreatureMaps bakeCreatureMaps(const Skeleton& skel, const CreatureParams& pr, int size) {
    CreatureMaps out;
    if (!skel.finalized() || size < 8) return out;
    skel.bounds(out.bmin, out.bmax);
    const float margin = 0.25f + pr.smoothK + pr.detailAmp * 2.f;
    out.bmin = out.bmin - Vec3{margin, margin, margin};
    out.bmax = out.bmax + Vec3{margin, margin, margin};
    const int S = size;
    out.size = S;
    out.albedo.assign(static_cast<std::size_t>(S) * S * 4, 0);
    out.normal.assign(static_cast<std::size_t>(S) * S * 4, 0);
    std::vector<float> height(static_cast<std::size_t>(S) * S, 0.f);

    const Noise nz(pr.seed);
    const auto field = [&](const Vec3& p) { return creatureSdfImpl(skel, pr, p, nz); };
    const float maxR = std::sqrt(std::fmax(out.bmax.x * out.bmax.x, out.bmin.x * out.bmin.x) +
                                 std::fmax(out.bmax.y * out.bmax.y, out.bmin.y * out.bmin.y)) + margin;
    const float cy = 0.5f * (out.bmin.y + out.bmax.y);
    const float spanZ = std::fmax(1e-6f, out.bmax.z - out.bmin.z);

    // reconstruct the surface point of texel (u, v): ray from the body axis outward,
    // bisected on the SDF — so the noise below is evaluated at the true 3D object-space point
    const auto surfacePoint = [&](float u, float v, Vec3& ps) {
        const float a = (u - 0.5f) * kTwoPi;
        const Vec3 dir{std::cos(a), std::sin(a), 0.f};
        const Vec3 o{0.f, 0.f, out.bmin.z + v * spanZ};
        float tIn = -1.f, tOut = -1.f;
        float t = 0.f;
        const float step = maxR / 24.f;
        float prev = field(o);
        if (prev < 0.f) tIn = 0.f;
        for (int s = 1; s <= 24; ++s) {
            t = s * step;
            const float f = field(o + dir * t);
            if (tIn < 0.f && f < 0.f) tIn = t;
            if (tIn >= 0.f && f >= 0.f) { tOut = t; break; }
            prev = f;
        }
        if (tIn < 0.f) { ps = o + dir * (0.3f * maxR); return; } // ray misses the body
        if (tOut < 0.f) tOut = tIn + step;
        for (int it = 0; it < 16; ++it) { // bisection
            const float tm = 0.5f * (tIn + tOut);
            if (field(o + dir * tm) < 0.f) tIn = tm; else tOut = tm;
        }
        ps = o + dir * (0.5f * (tIn + tOut));
    };

    const unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    const int rowsPerThread = (S + static_cast<int>(nThreads) - 1) / static_cast<int>(nThreads);
    for (unsigned tIdx = 0; tIdx < nThreads; ++tIdx) {
        const int j0 = static_cast<int>(tIdx) * rowsPerThread;
        const int j1 = std::min(S, j0 + rowsPerThread);
        if (j0 >= j1) break;
        threads.emplace_back([&, j0, j1] {
            const Noise nz(pr.seed);
            for (int j = j0; j < j1; ++j) {
                const float v = (j + 0.5f) / S;
                for (int i = 0; i < S; ++i) {
                    const float u = (i + 0.5f) / S;
                    Vec3 ps;
                    surfacePoint(u, v, ps);
                    const float r = std::fmax(1e-3f, std::sqrt(ps.x * ps.x + ps.y * ps.y));
                    const float ny = (ps.y - cy) / r; // -1 bottom .. +1 top of the cross-section
                    // palette: belly lighter, back stripes, scale-cell grooves, fbm variation
                    Vec3 col = fuhgl::math::Vec3{0, 0, 0};
                    const float belly = fuhgl::math::smoothstep(0.1f, -0.35f, ny);
                    col = pr.bellyColor * belly + pr.bodyColor * (1.f - belly);
                    const float back = fuhgl::math::smoothstep(-0.1f, 0.35f, ny);
                    const float wob = nz.fbm3(ps.x * 2.f, ps.y * 2.f, ps.z * 2.f, 2, Noise::Base::Simplex);
                    const float st = 0.5f + 0.5f * std::sin(ps.z * pr.stripeFreq * kTwoPi + 2.f * wob);
                    const float stripe = fuhgl::math::smoothstep(0.55f, 0.75f, st) * back;
                    col = col * (1.f - 0.85f * stripe) + pr.accentColor * (0.85f * stripe);
                    const float w = worley3(ps * pr.scaleFreq, pr.seed);
                    // worley F1: small near cell centers, large at cell borders -> darken the
                    // borders so the cells read as scales; per-cell tint via a second worley
                    const float groove = fuhgl::math::smoothstep(0.10f, 0.38f, w);
                    const float cellTint = 0.80f + 0.35f * worley3(ps * pr.scaleFreq, pr.seed ^ 0x5bd1e995u);
                    col = col * cellTint * (1.f - groove);
                    const float var = 0.88f + 0.24f * nz.fbm3(ps.x * pr.detailFreq, ps.y * pr.detailFreq,
                                                              ps.z * pr.detailFreq, 3, Noise::Base::Simplex);
                    col = col * var;
                    const float hgt = 0.5f * nz.fbm3(ps.x * pr.detailFreq * 1.3f, ps.y * pr.detailFreq * 1.3f,
                                                      ps.z * pr.detailFreq * 1.3f, 3, Noise::Base::Simplex) +
                                      0.5f * groove;
                    height[static_cast<std::size_t>(j) * S + i] = hgt;
                    unsigned char* o = &out.albedo[(static_cast<std::size_t>(j) * S + i) * 4];
                    o[0] = static_cast<unsigned char>(clamp01(col.x) * 255.f);
                    o[1] = static_cast<unsigned char>(clamp01(col.y) * 255.f);
                    o[2] = static_cast<unsigned char>(clamp01(col.z) * 255.f);
                    o[3] = 255;
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    // tangent-space normal map from the height grid (u wraps, v clamps)
    for (int j = 0; j < S; ++j)
        for (int i = 0; i < S; ++i) {
            const std::size_t idx = static_cast<std::size_t>(j) * S + i;
            const float hL = height[static_cast<std::size_t>(j) * S + (i - 1 + S) % S];
            const float hR = height[static_cast<std::size_t>(j) * S + (i + 1) % S];
            const float hD = height[static_cast<std::size_t>(std::max(0, j - 1)) * S + i];
            const float hU = height[static_cast<std::size_t>(std::min(S - 1, j + 1)) * S + i];
            const float dHdu = (hR - hL) * 0.5f * S;
            const float dHdv = (hU - hD) * 0.5f * S;
            Vec3 n{-dHdu * pr.bumpStrength, -dHdv * pr.bumpStrength, 1.f};
            n = n / std::fmax(1e-9f, fuhgl::math::length(n));
            unsigned char* o = &out.normal[idx * 4];
            o[0] = static_cast<unsigned char>(clamp01(n.x * 0.5f + 0.5f) * 255.f);
            o[1] = static_cast<unsigned char>(clamp01(n.y * 0.5f + 0.5f) * 255.f);
            o[2] = static_cast<unsigned char>(clamp01(n.z * 0.5f + 0.5f) * 255.f);
            o[3] = 255;
        }
    return out;
}

// --- presets ----------------------------------------------------------------------

Skeleton makeQuadruped() {
    Skeleton s;
    const int torso = s.addNode("torso", -1, {0.f, 1.0f, 0.f}, 0.42f, {1.f, 0.82f, 1.7f}, BodyPart::Torso);
    const int neck = s.addNode("neck", torso, {0.f, 0.28f, 0.55f}, 0.20f, {1.f, 1.f, 1.25f}, BodyPart::Neck);
    const int head = s.addNode("head", neck, {0.f, 0.14f, 0.30f}, 0.17f, {0.85f, 0.75f, 1.15f}, BodyPart::Head);
    s.addNode("jaw", head, {0.f, -0.08f, 0.22f}, 0.10f, {0.9f, 0.5f, 1.0f}, BodyPart::Jaw);
    s.addNode("hornL", head, {-0.09f, 0.12f, -0.05f}, 0.05f, {0.5f, 1.4f, 0.5f}, BodyPart::Horn, 0.7f);
    s.addNode("hornR", head, {0.09f, 0.12f, -0.05f}, 0.05f, {0.5f, 1.4f, 0.5f}, BodyPart::Horn, 0.7f);
    for (int side = 0; side < 2; ++side) {
        const float sx = side == 0 ? -0.30f : 0.30f;
        const int sh = s.addNode(side == 0 ? "shoulderL" : "shoulderR", torso, {sx, -0.12f, 0.38f},
                                0.14f, {1.f, 1.f, 1.f}, BodyPart::LegUpper);
        const int el = s.addNode(side == 0 ? "elbowL" : "elbowR", sh, {0.f, -0.34f, 0.02f},
                                 0.10f, {1.f, 1.f, 1.f}, BodyPart::LegLower);
        s.addNode(side == 0 ? "footL" : "footR", el, {0.f, -0.32f, 0.10f}, 0.075f,
                  {0.9f, 0.45f, 1.3f}, BodyPart::Foot);
        const int hip = s.addNode(side == 0 ? "hipL" : "hipR", torso, {sx, -0.14f, -0.42f},
                                  0.15f, {1.f, 1.f, 1.f}, BodyPart::LegUpper);
        const int kn = s.addNode(side == 0 ? "kneeL" : "kneeR", hip, {0.f, -0.36f, -0.02f},
                                 0.11f, {1.f, 1.f, 1.f}, BodyPart::LegLower);
        s.addNode(side == 0 ? "footHL" : "footHR", kn, {0.f, -0.34f, 0.10f}, 0.08f,
                  {0.9f, 0.45f, 1.3f}, BodyPart::Foot);
    }
    const int t1 = s.addNode("tail1", torso, {0.f, 0.05f, -0.55f}, 0.16f, {1.f, 0.8f, 1.f}, BodyPart::Tail);
    const int t2 = s.addNode("tail2", t1, {0.f, 0.03f, -0.30f}, 0.12f, {1.f, 0.8f, 1.f}, BodyPart::Tail);
    const int t3 = s.addNode("tail3", t2, {0.f, 0.02f, -0.26f}, 0.08f, {1.f, 0.7f, 1.f}, BodyPart::Tail);
    s.addNode("tail4", t3, {0.f, 0.01f, -0.22f}, 0.05f, {1.f, 0.6f, 1.f}, BodyPart::Tail);
    s.finalize();
    return s;
}

Skeleton makeReptile() {
    Skeleton s;
    const int torso = s.addNode("torso", -1, {0.f, 0.55f, 0.f}, 0.34f, {1.15f, 0.5f, 1.9f}, BodyPart::Torso);
    const int neck = s.addNode("neck", torso, {0.f, 0.10f, 0.62f}, 0.14f, {1.f, 0.9f, 1.2f}, BodyPart::Neck);
    const int head = s.addNode("head", neck, {0.f, 0.05f, 0.28f}, 0.13f, {1.25f, 0.45f, 1.25f}, BodyPart::Head);
    s.addNode("jaw", head, {0.f, -0.06f, 0.24f}, 0.08f, {1.1f, 0.4f, 1.1f}, BodyPart::Jaw);
    for (int side = 0; side < 2; ++side) {
        const float sx = side == 0 ? -0.42f : 0.42f;
        // sprawled legs: upper segment goes sideways, then down
        const int sh = s.addNode(side == 0 ? "shoulderL" : "shoulderR", torso, {sx * 0.7f, -0.06f, 0.45f},
                                 0.10f, {1.f, 1.f, 1.f}, BodyPart::LegUpper);
        const int el = s.addNode(side == 0 ? "elbowL" : "elbowR", sh, {sx * 0.35f, -0.22f, 0.05f},
                                 0.075f, {1.f, 1.f, 1.f}, BodyPart::LegLower);
        s.addNode(side == 0 ? "footL" : "footR", el, {0.f, -0.26f, 0.08f}, 0.06f,
                  {0.9f, 0.4f, 1.4f}, BodyPart::Foot);
        const int hip = s.addNode(side == 0 ? "hipL" : "hipR", torso, {sx * 0.75f, -0.08f, -0.5f},
                                  0.11f, {1.f, 1.f, 1.f}, BodyPart::LegUpper);
        const int kn = s.addNode(side == 0 ? "kneeL" : "kneeR", hip, {sx * 0.35f, -0.24f, -0.05f},
                                 0.08f, {1.f, 1.f, 1.f}, BodyPart::LegLower);
        s.addNode(side == 0 ? "footHL" : "footHR", kn, {0.f, -0.28f, 0.08f}, 0.065f,
                  {0.9f, 0.4f, 1.4f}, BodyPart::Foot);
    }
    int prev = torso;
    const float tz[6] = {-0.6f, -0.42f, -0.36f, -0.30f, -0.25f, -0.20f};
    const float tr[6] = {0.15f, 0.12f, 0.09f, 0.065f, 0.045f, 0.03f};
    for (int i = 0; i < 6; ++i) {
        prev = s.addNode("tail" + std::to_string(i + 1), prev, {0.f, 0.01f, tz[i]}, tr[i],
                         {1.f, 0.75f, 1.f}, BodyPart::Tail);
    }
    s.finalize();
    return s;
}

} // namespace fuhglgraphics::procedural
