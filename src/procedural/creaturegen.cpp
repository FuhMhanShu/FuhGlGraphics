// G32 procedural creature pipeline (design: docs/CREATURE_GENERATION.md): allometric
// blueprint -> bilateral skeleton graph -> 3-layer anatomy SDF (bone/muscle/skin + gravity
// sag, simplex + worley detail in the field) -> marching-cubes surface with curvature and
// material masks -> Gray-Scott Turing pattern + countershading bake (UV-free, object space)
// -> analytic two-bone IK gait with spine traveling wave and breathing.
// Pure CPU, deterministic per seed, no GL.

#include <fuhglgraphics/procedural/creaturegen.hpp>

#include <fuhglgraphics/procedural/marching_cubes.hpp>
#include <fuhglgraphics/procedural/noise.hpp>
#include <fuhglgraphics/procedural/sdf.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace fuhglgraphics::procedural {

using fuhgl::math::Vec3;

namespace {

constexpr float kTwoPi = 2.f * fuhgl::math::kPi;

float clamp01(float v) { return fuhgl::math::clamp(v, 0.f, 1.f); }

/// @brief Scalar lerp (fuhgl::math::lerp is vector-only).
float lerpF(float a, float b, float t) { return a + (b - a) * t; }

/// @brief Keratin parts stay sharp (near-hard min) and get the keratin material.
bool isKeratinPart(BodyPart p) {
    return p == BodyPart::Horn || p == BodyPart::Foot || p == BodyPart::Jaw;
}

SurfaceMaterial materialOf(BodyPart p) {
    if (isKeratinPart(p)) return SurfaceMaterial::Keratin;
    if (p == BodyPart::Eye) return SurfaceMaterial::Eye;
    return SurfaceMaterial::Skin;
}

/// @brief Tapered capsule SDF: segment a->b, radius lerp ra->rb.
float capsuleSdf(const Vec3& p, const Vec3& a, const Vec3& b, float ra, float rb) {
    const Vec3 ab = b - a;
    const float l2 = fuhgl::math::dot(ab, ab);
    const float t = l2 > 1e-9f ? fuhgl::math::clamp(fuhgl::math::dot(p - a, ab) / l2, 0.f, 1.f) : 0.f;
    const float r = ra + (rb - ra) * t;
    return fuhgl::math::length((p - a) - ab * t) - r;
}

/// @brief Muscle strand capsule with a fusiform (spindle) radius profile along the segment:
/// thin at the tendons, thick at the muscle belly.
float muscleCapsuleSdf(const Vec3& p, const Vec3& a, const Vec3& b, float ra, float rb) {
    const Vec3 ab = b - a;
    const float l2 = fuhgl::math::dot(ab, ab);
    const float t = l2 > 1e-9f ? fuhgl::math::clamp(fuhgl::math::dot(p - a, ab) / l2, 0.f, 1.f) : 0.f;
    const float bell = 0.55f + 0.45f * std::pow(std::fmax(0.f, std::sin(fuhgl::math::kPi * t)), 0.7f);
    const float r = (ra + (rb - ra) * t) * bell;
    return fuhgl::math::length((p - a) - ab * t) - r;
}

/// @brief Approximate ellipsoid SDF centered at c with radii r*ell (scaled-sphere estimate).
float ellipsoidSdf(const Vec3& p, const Vec3& c, float r, const Vec3& ell) {
    const Vec3 q{(p.x - c.x) / (r * ell.x), (p.y - c.y) / (r * ell.y), (p.z - c.z) / (r * ell.z)};
    const float m = std::fmin(ell.x, std::fmin(ell.y, ell.z));
    return (fuhgl::math::length(q) - 1.f) * r * m;
}

float primDistance(const Skeleton::Prim& prim, const Vec3& p) {
    return prim.capsule ? capsuleSdf(p, prim.a, prim.b, prim.ra, prim.rb)
                        : ellipsoidSdf(p, prim.a, prim.ra, prim.ell);
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
    return {static_cast<float>(mix(h)) / 4294967296.f,
            static_cast<float>(mix(h + 1u)) / 4294967296.f,
            static_cast<float>(mix(h + 2u)) / 4294967296.f};
}

/// @brief Worley F1 (distance to nearest feature point).
float worley3(const Vec3& p, unsigned seed) {
    const int ix = static_cast<int>(std::floor(p.x));
    const int iy = static_cast<int>(std::floor(p.y));
    const int iz = static_cast<int>(std::floor(p.z));
    float best = 8.f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Vec3 f = cellHash(ix + dx, iy + dy, iz + dz, seed);
                const Vec3 d{static_cast<float>(dx) + f.x - (p.x - ix),
                             static_cast<float>(dy) + f.y - (p.y - iy),
                             static_cast<float>(dz) + f.z - (p.z - iz)};
                best = std::fmin(best, fuhgl::math::length(d));
            }
    return best;
}

/// @brief Analytic SDF gradient (central differences).
Vec3 sdfGradient(const std::function<float(const Vec3&)>& f, const Vec3& p, float h) {
    return {(f({p.x + h, p.y, p.z}) - f({p.x - h, p.y, p.z})) / (2.f * h),
            (f({p.x, p.y + h, p.z}) - f({p.x, p.y - h, p.z})) / (2.f * h),
            (f({p.x, p.y, p.z + h}) - f({p.x, p.y, p.z - h})) / (2.f * h)};
}

/// @brief SDF Laplacian (central differences) — ~2x mean curvature at the surface.
float sdfLaplacian(const std::function<float(const Vec3&)>& f, const Vec3& p, float h) {
    const float c = f(p);
    float sum = -6.f * c;
    sum += f({p.x + h, p.y, p.z}) + f({p.x - h, p.y, p.z});
    sum += f({p.x, p.y + h, p.z}) + f({p.x, p.y - h, p.z});
    sum += f({p.x, p.y, p.z + h}) + f({p.x, p.y, p.z - h});
    return sum / (h * h);
}

} // namespace

// --- 1. evolutionary blueprint & allometry -----------------------------------------

Blueprint deriveBlueprint(const Ecology& eco, unsigned seed) {
    Blueprint bp;
    bp.seed = seed;
    const float m = fuhgl::math::clamp(eco.bodyMassKg, 0.05f, 5000.f);
    const float lm = std::log(m); // log-domain keeps the power laws numerically stable
    // size-driven allometric power laws (sub-isometric: real animals grow slimmer)
    bp.bodyLength = 0.55f * std::exp(0.27f * lm);  // body length ~ M^0.27
    bp.bodyRadius = 0.19f * std::exp(0.29f * lm);  // body radius ~ M^0.29
    bp.boneRadius = 0.011f * std::exp(0.36f * lm); // Galileo: bone radius ~ M^0.36
    bp.legLength  = 0.30f * std::exp(0.25f * lm);  // leg length ~ M^0.25
    bp.neckLength = 0.20f * bp.bodyLength;
    bp.headSize   = 0.55f * bp.bodyRadius;
    bp.tailLength = 0.60f * bp.bodyLength;
    // diet -> eye position: predators frontal (binocular overlap), prey lateral (panoramic)
    switch (eco.diet) {
        case Diet::Carnivore: bp.eyeForward = 0.85f; break;
        case Diet::Herbivore: bp.eyeForward = 0.10f; break;
        case Diet::Omnivore:  bp.eyeForward = 0.45f; break;
    }
    bp.eyeSize = 0.28f * bp.headSize;
    // climate -> Bergmann/Allen: compact body + fat + fur in the cold, large ears in heat
    float earScale = 1.f, fatMul = 1.f, fur = 0.5f, compact = 1.f;
    switch (eco.climate) {
        case Climate::Polar:     earScale = 0.4f; fatMul = 2.5f; fur = 1.0f;  compact = 1.15f; break;
        case Climate::Temperate: earScale = 1.0f; fatMul = 1.0f; fur = 0.5f;  compact = 1.00f; break;
        case Climate::Tropical:  earScale = 1.6f; fatMul = 0.4f; fur = 0.15f; compact = 0.90f; break;
        case Climate::Arid:      earScale = 1.8f; fatMul = 0.3f; fur = 0.25f; compact = 0.92f; break;
    }
    bp.bodyRadius *= compact;
    bp.fatThickness = 0.03f * bp.bodyRadius * fatMul;
    bp.furDensity = fur;
    bp.earSize = 0.10f * bp.headSize * earScale;
    // horns on large non-carnivores
    bp.horns = eco.diet != Diet::Carnivore && m > 30.f;
    bp.hornLength = 0.9f * bp.headSize;
    // locomotion -> leg pairs, torso orientation, tail length
    switch (eco.locomotion) {
        case Locomotion::Quadruped:  bp.legPairs = 2; bp.upright = false; break;
        case Locomotion::Biped:      bp.legPairs = 1; bp.upright = true;
                                     bp.tailLength = 0.15f * bp.bodyLength; break;
        case Locomotion::Serpentine: bp.legPairs = 0; bp.upright = false;
                                     bp.bodyRadius *= 0.7f;
                                     bp.tailLength = 1.6f * bp.bodyLength; break;
    }
    return bp;
}

Skeleton buildBlueprintSkeleton(const Blueprint& bp) {
    Skeleton s;
    const Noise nz(bp.seed);
    // controlled micro-asymmetry: a few percent of the local segment length, seeded
    const auto jitter = [&](Vec3& off, float segLen, int nodeIdx) {
        const float amp = 0.02f * segLen;
        off.x += nz.simplex3(nodeIdx * 1.7f, off.y * 3.f, off.z * 3.f) * amp;
        off.y += nz.simplex3(nodeIdx * 2.3f + 9.f, off.x * 3.f, off.z * 3.f) * amp * 0.5f;
    };
    const float R = bp.bodyRadius;
    const float ellZ = std::fmax(1.f, bp.bodyLength / (2.f * R));
    const Vec3 torsoPos{0.f, bp.legPairs > 0 ? bp.legLength : 0.4f * R, 0.f};
    const int torso = s.addNode("torso", -1, torsoPos, R,
                                bp.upright ? Vec3{0.95f, 1.6f, ellZ * 0.6f}
                                           : Vec3{1.f, 0.85f, ellZ},
                                BodyPart::Torso);
    const int neck = s.addNode("neck", torso, {0.f, 0.25f * R, 0.45f * bp.bodyLength},
                               0.45f * R, {1.f, 1.f, 1.3f}, BodyPart::Neck);
    const int head = s.addNode("head", neck, {0.f, 0.18f * R, bp.neckLength},
                               bp.headSize, {0.9f, 0.8f, 1.2f}, BodyPart::Head);
    s.addNode("jaw", head, {0.f, -0.35f * bp.headSize, 0.7f * bp.headSize},
              0.55f * bp.headSize, {0.9f, 0.5f, 1.f}, BodyPart::Jaw);
    // eyes: prey lateral, predator frontal (eyeForward blends the placement)
    const float ex = lerpF(0.55f, 0.25f, bp.eyeForward) * bp.headSize;
    const float ez = lerpF(0.05f, 0.60f, bp.eyeForward) * bp.headSize;
    s.addNode("eyeL", head, {-ex, 0.15f * bp.headSize, ez}, bp.eyeSize, {1.f, 1.f, 1.f}, BodyPart::Eye);
    s.addNode("eyeR", head, { ex, 0.15f * bp.headSize, ez}, bp.eyeSize, {1.f, 1.f, 1.f}, BodyPart::Eye);
    if (bp.horns) {
        const float hr = 0.12f * bp.headSize;
        const Vec3 he{0.5f, std::fmax(1.f, bp.hornLength / (2.f * hr)), 0.5f};
        s.addNode("hornL", head, {-0.3f * bp.headSize, 0.5f * bp.headSize, -0.2f * bp.headSize},
                  hr, he, BodyPart::Horn, 0.3f);
        s.addNode("hornR", head, { 0.3f * bp.headSize, 0.5f * bp.headSize, -0.2f * bp.headSize},
                  hr, he, BodyPart::Horn, 0.3f);
    }
    s.addNode("earL", head, {-0.6f * bp.headSize, 0.45f * bp.headSize, -0.3f * bp.headSize},
              0.5f * bp.earSize, {0.4f, 1.2f, 0.4f}, BodyPart::Head);
    s.addNode("earR", head, { 0.6f * bp.headSize, 0.45f * bp.headSize, -0.3f * bp.headSize},
              0.5f * bp.earSize, {0.4f, 1.2f, 0.4f}, BodyPart::Head);
    // legs: front pair (shoulder) and rear pair (hip); right side is mirrored to the left
    for (int pair = 0; pair < bp.legPairs; ++pair) {
        const float z = (pair == 0 ? 0.35f : -0.35f) * bp.bodyLength;
        for (int side = 0; side < 2; ++side) {
            const std::string suf = side == 0 ? "L" : "R";
            Vec3 o1{(side == 0 ? -0.45f : 0.45f) * R, -0.15f * R, z};
            Vec3 o2{0.f, -0.55f * bp.legLength, 0.03f * bp.legLength};
            Vec3 o3{0.f, -0.45f * bp.legLength, 0.12f * bp.legLength};
            const int base = static_cast<int>(s.nodes.size());
            jitter(o1, R, base);
            jitter(o2, bp.legLength, base + 1);
            jitter(o3, bp.legLength, base + 2);
            const int upper = s.addNode(std::string(pair == 0 ? "shoulder" : "hip") + suf, torso,
                                        o1, 1.8f * bp.boneRadius, {1.f, 1.f, 1.f},
                                        BodyPart::LegUpper);
            const int lower = s.addNode(std::string(pair == 0 ? "elbow" : "knee") + suf, upper,
                                        o2, 1.3f * bp.boneRadius, {1.f, 1.f, 1.f},
                                        BodyPart::LegLower);
            s.addNode(std::string(pair == 0 ? "footF" : "footH") + suf, lower,
                      o3, bp.boneRadius, {0.9f, 0.4f, 1.4f}, BodyPart::Foot);
        }
    }
    // tail chain, tapering
    const int tailSegs = fuhgl::math::clamp(static_cast<int>(bp.tailLength / (0.25f * R)), 3, 8);
    int prev = torso;
    for (int i = 0; i < tailSegs; ++i) {
        const float t = static_cast<float>(i + 1) / tailSegs;
        const float r = R * (0.55f - 0.5f * t);
        Vec3 off{0.f, 0.02f * R, -bp.tailLength / tailSegs};
        jitter(off, bp.tailLength / tailSegs, static_cast<int>(s.nodes.size()));
        prev = s.addNode("tail" + std::to_string(i + 1), prev, off, r, {1.f, 0.8f, 1.f},
                         BodyPart::Tail);
    }
    s.finalize();
    return s;
}

// --- 2. anatomically layered SDF ----------------------------------------------------

namespace {

/// @brief Hot-path layered SDF. `midY`/`bodyR` come from the skeleton bounds (computed once
/// by the caller): the skin-layer gravity sag uses them for the belly mask.
float anatomySdfImpl(const Skeleton& skel, const Anatomy& an, const Vec3& p, const Noise& nz,
                     float midY, float bodyR) {
    const float margin = an.skinBlendK + 2.f * (an.detailAmp + an.scaleAmp) + 0.05f;
    float dBone = 1e9f, dMuscle = 1e9f, dSkin = 1e9f;
    for (const Skeleton::Prim& prim : skel.prims()) {
        if (p.x < prim.aabbMin.x - margin || p.x > prim.aabbMax.x + margin ||
            p.y < prim.aabbMin.y - margin || p.y > prim.aabbMax.y + margin ||
            p.z < prim.aabbMin.z - margin || p.z > prim.aabbMax.z + margin)
            continue;
        const bool keratin = isKeratinPart(prim.part);
        if (prim.capsule) {
            const float pd = capsuleSdf(p, prim.a, prim.b, prim.ra, prim.rb);
            dSkin = sdf::smoothMin(dSkin, pd, an.skinBlendK);
            const float pm = muscleCapsuleSdf(p, prim.a, prim.b, prim.ra, prim.rb);
            dMuscle = keratin ? std::fmin(dMuscle, pm) // keratin stays sharp (k ~ 0)
                              : sdf::smoothMin(dMuscle, pm, an.muscleBlendK);
        } else {
            const float pd = ellipsoidSdf(p, prim.a, prim.ra, prim.ell);
            dSkin = sdf::smoothMin(dSkin, pd, an.skinBlendK);
            const float pb = keratin ? pd // horn/claw anchors at full radius, hard union
                                     : ellipsoidSdf(p, prim.a, prim.ra * an.boneRadiusScale,
                                                    prim.ell);
            dBone = keratin ? std::fmin(dBone, pb)
                            : sdf::smoothMin(dBone, pb, an.boneBlendK);
        }
    }
    // gravity sag of the skin layer on the underside, decaying with height above ground
    const float under = fuhgl::math::smoothstep(0.3f, -0.3f, (p.y - midY) / bodyR);
    dSkin -= an.sagAmp * std::exp(-std::fmax(0.f, p.y) / std::fmax(1e-3f, an.sagHeight)) * under;
    float d = sdf::smoothMin(dBone, sdf::smoothMin(dMuscle, dSkin, an.muscleBlendK), an.skinBlendK);
    // fine detail modulated directly into the field: simplex fBm folds + worley scale cones
    d += an.detailAmp * nz.fbm3(p.x * an.detailFreq, p.y * an.detailFreq, p.z * an.detailFreq,
                                an.detailOctaves, Noise::Base::Simplex);
    const float w = worley3(p * an.scaleFreq, an.seed);
    d -= an.scaleAmp * std::pow(clamp01(1.f - w), 2.f);
    return d;
}

} // namespace

float anatomySdf(const Skeleton& skel, const Anatomy& an, const Vec3& p) {
    const Noise nz(an.seed);
    Vec3 mn, mx;
    skel.bounds(mn, mx);
    return anatomySdfImpl(skel, an, p, nz, 0.5f * (mn.y + mx.y),
                          std::fmax(1e-3f, 0.5f * (mx.x - mn.x)));
}

// --- 3. surface extraction & masks --------------------------------------------------

SurfaceMasks extractSurface(const Skeleton& skel, const Anatomy& an, int resolution,
                            const Vec3& baseColor) {
    SurfaceMasks out;
    if (!skel.finalized() || resolution < 8) return out;
    skel.bounds(out.bmin, out.bmax);
    const float margin = 0.25f + an.skinBlendK + 2.f * (an.detailAmp + an.scaleAmp);
    out.bmin = out.bmin - Vec3{margin, margin, margin};
    out.bmax = out.bmax + Vec3{margin, margin, margin};
    Vec3 bmn, bmx;
    skel.bounds(bmn, bmx);
    const float midY = 0.5f * (bmn.y + bmx.y);
    const float bodyR = std::fmax(1e-3f, 0.5f * (bmx.x - bmn.x));
    const Noise nz(an.seed);
    const auto field = [&](const Vec3& p) { return anatomySdfImpl(skel, an, p, nz, midY, bodyR); };
    const VolumeGrid grid =
        sampleField(field, out.bmin, out.bmax, resolution, resolution, resolution);
    out.mesh = marchingCubes(grid, 0.f, baseColor);
    if (out.mesh.indices.empty()) return out;

    // per-vertex analytic data: gradient normals, Laplacian curvature, material from the
    // nearest prim, curvature shading in the color channel, cylindrical UVs (seamless in u)
    const float h = (out.bmax.x - out.bmin.x) / static_cast<float>(resolution - 1);
    const float invSpanZ = 1.f / std::fmax(1e-6f, out.bmax.z - out.bmin.z);
    out.curvature.assign(out.mesh.vertexCount(), 0.f);
    out.material.assign(out.mesh.vertexCount(), 0);
    for (std::uint32_t i = 0; i < out.mesh.vertexCount(); ++i) {
        float* v = &out.mesh.vertices[i * 11];
        const Vec3 p{v[0], v[1], v[2]};
        Vec3 n = sdfGradient(field, p, h * 0.5f);
        const float nl = fuhgl::math::length(n);
        n = nl > 1e-9f ? n / nl : Vec3{0.f, 1.f, 0.f};
        const float H = 0.5f * sdfLaplacian(field, p, h); // ~mean curvature, <0 concave
        out.curvature[i] = H;
        const float shade = fuhgl::math::clamp(1.f + 6.f * H * h, 0.05f, 2.f);
        v[3] = n.x; v[4] = n.y; v[5] = n.z;
        v[6] = baseColor.x * shade;
        v[7] = baseColor.y * shade;
        v[8] = baseColor.z * shade;
        v[9] = std::atan2(p.y, p.x) / kTwoPi + 0.5f;
        v[10] = (p.z - out.bmin.z) * invSpanZ;
        float best = 1e9f;
        std::uint8_t mat = 0;
        for (const Skeleton::Prim& prim : skel.prims()) {
            const float pd = primDistance(prim, p);
            if (pd < best) {
                best = pd;
                mat = static_cast<std::uint8_t>(materialOf(prim.part));
            }
        }
        out.material[i] = mat;
    }
    return out;
}

// --- 4. UV-free texturing & materiality ---------------------------------------------

std::vector<float> reactionDiffusion(const Pattern& pt) {
    const int W = std::max(8, pt.gridU);
    const int H = std::max(8, pt.gridV);
    std::vector<float> U(static_cast<std::size_t>(W) * H, 1.f);
    std::vector<float> V(static_cast<std::size_t>(W) * H, 0.f);
    std::vector<float> U2(U.size()), V2(V.size());
    // seed: jitter everywhere + a few blobs of V
    const Noise nz(pt.seed);
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i)
            V[static_cast<std::size_t>(j) * W + i] =
                clamp01(0.1f * nz.simplex2(i * 0.35f, j * 0.35f));
    for (int b = 0; b < 3; ++b) {
        const int bi = static_cast<int>(0.5f * (nz.simplex2(b * 7.1f, 3.3f) + 1.f) * (W - 1));
        const int bj = static_cast<int>(0.5f * (nz.simplex2(5.1f, b * 9.7f) + 1.f) * (H - 1));
        for (int dj = -3; dj <= 3; ++dj)
            for (int di = -3; di <= 3; ++di)
                if (di * di + dj * dj <= 9) {
                    const int i = (bi + di + W) % W, j = fuhgl::math::clamp(bj + dj, 0, H - 1);
                    V[static_cast<std::size_t>(j) * W + i] = 0.5f;
                }
    }
    // Gray-Scott explicit Euler on the cylindrical domain: periodic in u (seamless wrap),
    // clamped in v; anisotropy > 1 weakens the v-diffusion -> stripes along the body axis
    const float wv = 1.f / std::fmax(0.25f, pt.anisotropy);
    for (int it = 0; it < pt.iterations; ++it) {
        for (int j = 0; j < H; ++j) {
            for (int i = 0; i < W; ++i) {
                const std::size_t c = static_cast<std::size_t>(j) * W + i;
                const float Uc = U[c], Vc = V[c];
                const float ul = U[c - (i > 0 ? 1 : -(W - 1))]; // wrap in u
                const float ur = U[c + (i < W - 1 ? 1 : -(W - 1))];
                const float uu = U[static_cast<std::size_t>(std::max(0, j - 1)) * W + i];
                const float ud = U[static_cast<std::size_t>(std::min(H - 1, j + 1)) * W + i];
                const float lapU = (ul + ur - 2.f * Uc) + wv * (uu + ud - 2.f * Uc);
                const float vl = V[c - (i > 0 ? 1 : -(W - 1))];
                const float vr = V[c + (i < W - 1 ? 1 : -(W - 1))];
                const float vu = V[static_cast<std::size_t>(std::max(0, j - 1)) * W + i];
                const float vd = V[static_cast<std::size_t>(std::min(H - 1, j + 1)) * W + i];
                const float lapV = (vl + vr - 2.f * Vc) + wv * (vu + vd - 2.f * Vc);
                U2[c] = fuhgl::math::clamp(Uc + pt.diffU * lapU - Uc * Vc * Vc +
                                           pt.feed * (1.f - Uc), 0.f, 1.f);
                V2[c] = fuhgl::math::clamp(Vc + pt.diffV * lapV + Uc * Vc * Vc -
                                           (pt.feed + pt.kill) * Vc, 0.f, 1.f);
            }
        }
        U.swap(U2);
        V.swap(V2);
    }
    // normalize V by its max so the threshold is relative (robust across F/k choices)
    float vmax = 1e-6f;
    for (float v : V) vmax = std::fmax(vmax, v);
    std::vector<float> mask(U.size());
    for (std::size_t c = 0; c < U.size(); ++c)
        mask[c] = fuhgl::math::smoothstep(pt.threshold - pt.softness,
                                          pt.threshold + pt.softness, V[c] / vmax);
    return mask;
}

BakedMaps bakeMaterial(const Skeleton& skel, const Anatomy& an, const Palette& pal,
                       const Pattern& pt, int size) {
    BakedMaps out;
    if (!skel.finalized() || size < 8) return out;
    skel.bounds(out.bmin, out.bmax);
    const float margin = 0.25f + an.skinBlendK + 2.f * (an.detailAmp + an.scaleAmp);
    out.bmin = out.bmin - Vec3{margin, margin, margin};
    out.bmax = out.bmax + Vec3{margin, margin, margin};
    const int S = size;
    out.size = S;
    out.albedo.assign(static_cast<std::size_t>(S) * S * 4, 0);
    out.normal.assign(static_cast<std::size_t>(S) * S * 4, 0);
    out.material.assign(static_cast<std::size_t>(S) * S, 0);
    std::vector<float> height(static_cast<std::size_t>(S) * S, 0.f);

    const Noise nz(an.seed);
    Vec3 bmn, bmx;
    skel.bounds(bmn, bmx);
    const float midY = 0.5f * (bmn.y + bmx.y);
    const float bodyR = std::fmax(1e-3f, 0.5f * (bmx.x - bmn.x));
    const auto field = [&](const Vec3& p) { return anatomySdfImpl(skel, an, p, nz, midY, bodyR); };
    const std::vector<float> pat = reactionDiffusion(pt);
    const int PW = std::max(8, pt.gridU), PH = std::max(8, pt.gridV);

    const float maxR = std::sqrt(std::fmax(out.bmax.x * out.bmax.x, out.bmin.x * out.bmin.x) +
                                 std::fmax(out.bmax.y * out.bmax.y, out.bmin.y * out.bmin.y)) + margin;
    const float cy = 0.5f * (out.bmin.y + out.bmax.y);
    const float spanZ = std::fmax(1e-6f, out.bmax.z - out.bmin.z);
    const float h = std::fmax(1e-3f, (out.bmax.x - out.bmin.x) / S);

    // surface point of texel (u, v): ray from the body axis outward, bisected on the SDF —
    // all noise below is evaluated at the true 3D object-space point (seamless in u)
    const auto surfacePoint = [&](float u, float v, Vec3& ps) {
        const float a = (u - 0.5f) * kTwoPi;
        const Vec3 dir{std::cos(a), std::sin(a), 0.f};
        const Vec3 o{0.f, 0.f, out.bmin.z + v * spanZ};
        float tIn = -1.f, tOut = -1.f;
        if (field(o) < 0.f) tIn = 0.f;
        const float step = maxR / 24.f;
        for (int s = 1; s <= 24 && tOut < 0.f; ++s) {
            const float t = s * step;
            const float f = field(o + dir * t);
            if (tIn < 0.f && f < 0.f) tIn = t;
            if (tIn >= 0.f && f >= 0.f) tOut = t;
        }
        if (tIn < 0.f) { ps = o + dir * (0.3f * maxR); return; } // ray misses the body
        if (tOut < 0.f) tOut = tIn + step;
        for (int it = 0; it < 16; ++it) { // bisection
            const float tm = 0.5f * (tIn + tOut);
            if (field(o + dir * tm) < 0.f) tIn = tm; else tOut = tm;
        }
        ps = o + dir * (0.5f * (tIn + tOut));
    };

    for (int j = 0; j < S; ++j) {
        const float v = (j + 0.5f) / S;
        const int pj = fuhgl::math::clamp(static_cast<int>(v * PH), 0, PH - 1);
        for (int i = 0; i < S; ++i) {
            const float u = (i + 0.5f) / S;
            const int pi = fuhgl::math::clamp(static_cast<int>(u * PW), 0, PW - 1);
            Vec3 ps;
            surfacePoint(u, v, ps);
            const float r = std::fmax(1e-3f, std::sqrt(ps.x * ps.x + ps.y * ps.y));
            const float ny = (ps.y - cy) / r; // -1 belly .. +1 back of the cross-section
            // countershading: dark back, light belly
            Vec3 col = pt.countershading
                           ? fuhgl::math::lerp(pal.ventral, pal.dorsal,
                                               fuhgl::math::smoothstep(-0.1f, 0.35f, ny))
                           : pal.dorsal;
            // Turing pattern (spots/stripes) from the reaction-diffusion mask
            const float m = pat[static_cast<std::size_t>(pj) * PW + pi];
            col = fuhgl::math::lerp(col, pal.accent, m);
            // curvature darkening in folds (concave creases darken)
            const float H = 0.5f * sdfLaplacian(field, ps, h);
            col = col * fuhgl::math::clamp(1.f - pt.curvatureDarkening *
                                           std::fmax(0.f, -H) * h, 0.2f, 1.f);
            // material transition: keratin (horns/claws/hooves/jaw) overrides the palette
            float best = 1e9f;
            SurfaceMaterial mat = SurfaceMaterial::Skin;
            for (const Skeleton::Prim& prim : skel.prims()) {
                const float pd = primDistance(prim, ps);
                if (pd < best) {
                    best = pd;
                    mat = materialOf(prim.part);
                }
            }
            const float var = 0.88f + 0.24f * nz.fbm3(ps.x * 2.f, ps.y * 2.f, ps.z * 2.f, 2,
                                                      Noise::Base::Simplex);
            if (mat == SurfaceMaterial::Keratin)
                col = pal.keratin * (0.9f + 0.2f * var);
            else
                col = col * var;
            const float w = worley3(ps * an.scaleFreq, an.seed);
            const float groove = fuhgl::math::smoothstep(0.10f, 0.38f, w);
            const float hgt = 0.5f * nz.fbm3(ps.x * an.detailFreq * 1.3f, ps.y * an.detailFreq * 1.3f,
                                             ps.z * an.detailFreq * 1.3f, 3, Noise::Base::Simplex) +
                              0.5f * groove +
                              pal.furDensity * 0.3f * nz.fbm3(ps.x * 25.f, ps.y * 25.f, ps.z * 25.f, 2,
                                                             Noise::Base::Simplex);
            const std::size_t idx = static_cast<std::size_t>(j) * S + i;
            height[idx] = hgt;
            unsigned char* o = &out.albedo[idx * 4];
            o[0] = static_cast<unsigned char>(clamp01(col.x) * 255.f);
            o[1] = static_cast<unsigned char>(clamp01(col.y) * 255.f);
            o[2] = static_cast<unsigned char>(clamp01(col.z) * 255.f);
            o[3] = 255;
            out.material[idx] = static_cast<unsigned char>(mat);
        }
    }

    // tangent-space normal map from the height grid (u wraps, v clamps)
    for (int j = 0; j < S; ++j)
        for (int i = 0; i < S; ++i) {
            const std::size_t idx = static_cast<std::size_t>(j) * S + i;
            const float hL = height[static_cast<std::size_t>(j) * S + (i - 1 + S) % S];
            const float hR = height[static_cast<std::size_t>(j) * S + (i + 1) % S];
            const float hD = height[static_cast<std::size_t>(std::max(0, j - 1)) * S + i];
            const float hU = height[static_cast<std::size_t>(std::min(S - 1, j + 1)) * S + i];
            Vec3 n{-(hR - hL) * 0.5f * S * pt.bumpStrength,
                   -(hU - hD) * 0.5f * S * pt.bumpStrength, 1.f};
            n = n / std::fmax(1e-9f, fuhgl::math::length(n));
            unsigned char* o = &out.normal[idx * 4];
            o[0] = static_cast<unsigned char>(clamp01(n.x * 0.5f + 0.5f) * 255.f);
            o[1] = static_cast<unsigned char>(clamp01(n.y * 0.5f + 0.5f) * 255.f);
            o[2] = static_cast<unsigned char>(clamp01(n.z * 0.5f + 0.5f) * 255.f);
            o[3] = 255;
        }
    return out;
}

// --- 5. procedural locomotion & kinematics ------------------------------------------

bool solveTwoBoneIk(const Vec3& root, const Vec3& target, float l1, float l2,
                    const Vec3& bendAxis, Vec3& midOut) {
    const Vec3 toTarget = target - root;
    const float dist = fuhgl::math::length(toTarget);
    if (dist < 1e-6f || l1 < 1e-6f || l2 < 1e-6f) return false;
    // clamp to the reachable range (law of cosines needs |l1-l2| <= d <= l1+l2)
    const float d = fuhgl::math::clamp(dist, std::fabs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
    const Vec3 dir = toTarget / dist;
    const float cosA = fuhgl::math::clamp((l1 * l1 + d * d - l2 * l2) / (2.f * l1 * d), -1.f, 1.f);
    const float alpha = std::acos(cosA);
    Vec3 axis = fuhgl::math::cross(dir, bendAxis);
    if (fuhgl::math::length(axis) < 1e-5f)
        axis = std::fabs(dir.y) < 0.9f ? fuhgl::math::cross(dir, Vec3{0.f, 1.f, 0.f})
                                       : fuhgl::math::cross(dir, Vec3{1.f, 0.f, 0.f});
    const fuhgl::math::Quat q = fuhgl::math::fromAxisAngle(alpha, axis);
    midOut = root + q * dir * l1;
    return true;
}

std::vector<Vec3> creatureGaitPose(const Skeleton& skel, const Gait& gait, float time,
                                   const TerrainFn& terrain) {
    const int n = static_cast<int>(skel.nodes.size());
    if (!skel.finalized() || n == 0) return {};
    const TerrainFn terr = terrain ? terrain : TerrainFn([](float, float) { return 0.f; });
    Vec3 dir{gait.moveDir.x, 0.f, gait.moveDir.z};
    if (fuhgl::math::length(dir) < 1e-6f) dir = {0.f, 0.f, 1.f};
    dir = fuhgl::math::normalize(dir);
    std::vector<Vec3> pose(n);
    for (int i = 0; i < n; ++i) pose[i] = skel.worldPos(i);
    std::vector<std::vector<int>> children(n);
    for (int i = 0; i < n; ++i)
        if (skel.nodes[i].parent >= 0) children[skel.nodes[i].parent].push_back(i);

    const float phase = kTwoPi * gait.cycleHz * time;

    // traveling wave along spine/tail first (arc length from the root along the parent
    // chain, envelope growing toward the tail tip), then breathing of the thorax
    std::vector<float> arc(n, 0.f);
    for (int i = 0; i < n; ++i) {
        const int par = skel.nodes[i].parent;
        arc[i] = par >= 0 ? arc[par] + fuhgl::math::length(skel.nodes[i].offset) : 0.f;
    }
    float tailStart = 1e9f, tailEnd = 0.f;
    for (int i = 0; i < n; ++i)
        if (skel.nodes[i].part == BodyPart::Tail) {
            tailStart = std::fmin(tailStart, arc[i]);
            tailEnd = std::fmax(tailEnd, arc[i]);
        }
    const float waveK = kTwoPi / std::fmax(1e-3f, gait.spineWaveLength);
    for (int i = 0; i < n; ++i) {
        float env = 0.15f; // subtle sway on the spine
        if (skel.nodes[i].part == BodyPart::Tail && tailEnd > tailStart)
            env = std::pow((arc[i] - tailStart) / (tailEnd - tailStart), 1.5f);
        pose[i].x += gait.spineWaveAmp * env * std::sin(waveK * arc[i] - phase);
    }
    const float breath = gait.breathAmp * std::sin(kTwoPi * gait.breathHz * time);
    if (std::fabs(breath) > 1e-6f) {
        const Vec3 spine = pose[0];
        for (int i = 0; i < n; ++i) {
            const BodyPart part = skel.nodes[i].part;
            if (part == BodyPart::Torso || part == BodyPart::Neck) {
                pose[i].x = spine.x + (pose[i].x - spine.x) * (1.f + breath);
                pose[i].z = spine.z + (pose[i].z - spine.z) * (1.f + 0.5f * breath);
            }
        }
    }

    // leg chains (LegUpper -> LegLower -> Foot) IK-solved last, so the feet land exactly on
    // the terrain footholds (root = posed upper joint, target from the rest foot + stride)
    int legIdx = 0;
    for (int i = 0; i < n; ++i) {
        if (skel.nodes[i].part != BodyPart::LegUpper) continue;
        int lower = -1, foot = -1;
        for (int c : children[i])
            if (skel.nodes[c].part == BodyPart::LegLower) { lower = c; break; }
        if (lower >= 0)
            for (int c : children[lower])
                if (skel.nodes[c].part == BodyPart::Foot) { foot = c; break; }
        if (lower < 0 || foot < 0) continue;
        // default gait: lateral sequence walk (alternating sides, 90 deg offset)
        const float ph = !gait.legPhases.empty()
                             ? gait.legPhases[legIdx % gait.legPhases.size()]
                             : 0.25f * (legIdx / 2) + 0.5f * (legIdx % 2);
        ++legIdx;
        const float ang = phase + kTwoPi * ph;
        Vec3 target = skel.worldPos(foot) + dir * (gait.strideLength * std::cos(ang));
        target.y = terr(target.x, target.z) + gait.liftHeight * std::fmax(0.f, std::sin(ang));
        const float l1 = fuhgl::math::length(skel.worldPos(lower) - skel.worldPos(i));
        const float l2 = fuhgl::math::length(skel.worldPos(foot) - skel.worldPos(lower));
        const float side = skel.worldPos(i).x >= pose[0].x ? 1.f : -1.f;
        Vec3 mid;
        if (solveTwoBoneIk(pose[i], target, l1, l2, Vec3{side, 0.f, 0.f}, mid)) {
            pose[lower] = mid;
            pose[foot] = target;
        }
    }
    return pose;
}

} // namespace fuhglgraphics::procedural
