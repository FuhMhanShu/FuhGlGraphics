// Unit tests for the G32 procedural creature pipeline (label "unit", see
// docs/CREATURE_GENERATION.md): allometry monotonicity and ecological rules of
// deriveBlueprint, bilateral symmetry (+ micro-asymmetry bounds) of buildBlueprintSkeleton,
// anatomySdf sign/determinism/continuity, extractSurface (valid mesh, unit normals, finite
// curvature, keratin material mask), reactionDiffusion (range/determinism/pattern forms),
// bakeMaterial (sizes, alpha, determinism, countershading darker on the back), solveTwoBoneIk
// (reachability, clamped unreachable, degenerate), creatureGaitPose (determinism, feet on the
// terrain heightfield, legs out of phase).

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/creaturegen.hpp>

#include <cmath>
#include <string>

namespace {

using fuhglgraphics::procedural::Anatomy;
using fuhglgraphics::procedural::BakedMaps;
using fuhglgraphics::procedural::Blueprint;
using fuhglgraphics::procedural::Climate;
using fuhglgraphics::procedural::Diet;
using fuhglgraphics::procedural::Ecology;
using fuhglgraphics::procedural::Gait;
using fuhglgraphics::procedural::Locomotion;
using fuhglgraphics::procedural::Palette;
using fuhglgraphics::procedural::Pattern;
using fuhglgraphics::procedural::Skeleton;
using fuhglgraphics::procedural::SurfaceMaterial;
using fuhglgraphics::procedural::anatomySdf;
using fuhglgraphics::procedural::bakeMaterial;
using fuhglgraphics::procedural::buildBlueprintSkeleton;
using fuhglgraphics::procedural::creatureGaitPose;
using fuhglgraphics::procedural::deriveBlueprint;
using fuhglgraphics::procedural::extractSurface;
using fuhglgraphics::procedural::reactionDiffusion;
using fuhglgraphics::procedural::solveTwoBoneIk;
using fuhgl::math::Vec3;

float lum(const unsigned char* p) {
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

Skeleton demoSkeleton() {
    Ecology eco;
    eco.bodyMassKg = 80.f;
    eco.locomotion = Locomotion::Quadruped;
    eco.diet = Diet::Herbivore; // > 30 kg -> horns
    eco.climate = Climate::Temperate;
    return buildBlueprintSkeleton(deriveBlueprint(eco, 42u));
}

void testAllometry() {
    Ecology small, big;
    small.bodyMassKg = 5.f;
    big.bodyMassKg = 500.f;
    const Blueprint bs = deriveBlueprint(small, 1u);
    const Blueprint bb = deriveBlueprint(big, 1u);
    // heavier -> longer body, thicker bones (Galileo), longer legs — all monotonic
    CHECK(bb.bodyLength > bs.bodyLength);
    CHECK(bb.boneRadius > bs.boneRadius);
    CHECK(bb.legLength > bs.legLength);
    // bone radius grows faster than body length would predict under isometry (M^(1/3)):
    // exponent 0.36 > 0.27, so the ratio bone/body grows with mass
    CHECK(bb.boneRadius / bb.bodyLength > bs.boneRadius / bs.bodyLength);
    // diet -> eye position: predator frontal, prey lateral
    Ecology pred = small, prey = small;
    pred.diet = Diet::Carnivore;
    prey.diet = Diet::Herbivore;
    CHECK(deriveBlueprint(pred, 1u).eyeForward > deriveBlueprint(prey, 1u).eyeForward);
    // climate: polar animal has more fat and fur than a tropical one
    Ecology polar = small, tropical = small;
    polar.climate = Climate::Polar;
    tropical.climate = Climate::Tropical;
    const Blueprint bp = deriveBlueprint(polar, 1u);
    const Blueprint bt = deriveBlueprint(tropical, 1u);
    CHECK(bp.fatThickness > bt.fatThickness);
    CHECK(bp.furDensity > bt.furDensity);
    CHECK(bp.earSize < bt.earSize); // Allen's rule
    // locomotion
    Ecology biped = small, serpent = small;
    biped.locomotion = Locomotion::Biped;
    serpent.locomotion = Locomotion::Serpentine;
    CHECK(deriveBlueprint(biped, 1u).legPairs == 1 && deriveBlueprint(biped, 1u).upright);
    CHECK(deriveBlueprint(serpent, 1u).legPairs == 0);
    CHECK(deriveBlueprint(serpent, 1u).tailLength > deriveBlueprint(small, 1u).tailLength);
    // horns: large herbivore yes, small carnivore no
    Ecology bigHerb = big;
    bigHerb.diet = Diet::Herbivore;
    Ecology smallCarn = small;
    smallCarn.diet = Diet::Carnivore;
    CHECK(deriveBlueprint(bigHerb, 1u).horns);
    CHECK(!deriveBlueprint(smallCarn, 1u).horns);
}

void testSymmetry() {
    const Skeleton s = demoSkeleton();
    CHECK(s.finalized());
    CHECK(!s.nodes.empty() && !s.prims().empty());
    // every *L node has an *R partner with mirrored x offset (within the jitter bounds),
    // identical z (z is not jittered) — bilateral symmetry + controlled micro-asymmetry
    int pairs = 0;
    for (int i = 0; i < static_cast<int>(s.nodes.size()); ++i) {
        const std::string& name = s.nodes[i].name;
        if (name.empty() || name.back() != 'L') continue;
        const std::string rName = name.substr(0, name.size() - 1) + "R";
        int j = -1;
        for (int k = 0; k < static_cast<int>(s.nodes.size()); ++k)
            if (s.nodes[k].name == rName) { j = k; break; }
        CHECK(j >= 0);
        if (j < 0) continue;
        ++pairs;
        CHECK(s.nodes[i].parent >= 0 && s.nodes[j].parent >= 0);
        CHECK_NEAR(s.nodes[i].offset.z, s.nodes[j].offset.z, 1e-6f);
        const float tol = 0.05f * (0.5f + s.nodes[i].radius * 20.f);
        CHECK(std::fabs(s.nodes[i].offset.x + s.nodes[j].offset.x) < tol);
    }
    CHECK(pairs >= 6); // eyes, ears, horns, 2 leg chains x 3 nodes
    // spine nodes lie on the symmetry plane (|x| small vs. body size)
    Vec3 mn, mx;
    s.bounds(mn, mx);
    const float bodyR = 0.5f * (mx.x - mn.x);
    for (int i = 0; i < static_cast<int>(s.nodes.size()); ++i) {
        const std::string& name = s.nodes[i].name;
        if (name == "torso" || name == "neck" || name == "head")
            CHECK(std::fabs(s.worldPos(i).x) < 0.05f * bodyR);
    }
}

void testAnatomySdf() {
    const Skeleton s = demoSkeleton();
    Anatomy an;
    an.seed = 7u;
    const Vec3 torso = s.worldPos(0);
    CHECK(anatomySdf(s, an, torso) < 0.f);               // inside the torso
    CHECK(anatomySdf(s, an, {100.f, 100.f, 100.f}) > 0.f); // far outside
    CHECK(anatomySdf(s, an, {0.31f, 1.2f, -0.2f}) == anatomySdf(s, an, {0.31f, 1.2f, -0.2f})); // deterministic
    // continuity along the spine: no jumps despite smin layers + noise detail
    const Vec3 a = s.worldPos(0);
    int head = -1;
    for (int i = 0; i < static_cast<int>(s.nodes.size()); ++i)
        if (s.nodes[i].name == "head") head = i;
    CHECK(head >= 0);
    const Vec3 b = s.worldPos(head);
    float prev = anatomySdf(s, an, a);
    float maxJump = 0.f;
    for (int k = 1; k <= 200; ++k) {
        const float t = static_cast<float>(k) / 200.f;
        const Vec3 p = a + (b - a) * t;
        const float d = anatomySdf(s, an, p);
        maxJump = std::fmax(maxJump, std::fabs(d - prev));
        prev = d;
    }
    const float step = fuhgl::math::length(b - a) / 200.f;
    CHECK(maxJump < 4.f * step + an.detailAmp * 2.f + an.scaleAmp); // Lipschitz-ish bound
    // detail is actually present: field differs from a detail-free variant
    Anatomy flat = an;
    flat.detailAmp = 0.f;
    flat.scaleAmp = 0.f;
    bool differs = false;
    for (int k = 0; k < 20 && !differs; ++k) {
        const Vec3 p = a + Vec3{0.05f * k, 0.02f * k, 0.03f * k};
        differs = anatomySdf(s, an, p) != anatomySdf(s, flat, p);
    }
    CHECK(differs);
}

void testExtractSurface() {
    const Skeleton s = demoSkeleton();
    Anatomy an;
    an.seed = 3u;
    const auto surf = extractSurface(s, an, 40);
    CHECK(!surf.mesh.indices.empty());
    CHECK(surf.mesh.indices.size() % 3 == 0);
    CHECK(surf.mesh.vertexCount() > 100);
    CHECK(surf.curvature.size() == surf.mesh.vertexCount());
    CHECK(surf.material.size() == surf.mesh.vertexCount());
    bool unitNormals = true, finiteCurv = true, sawKeratin = false, sawSkin = false;
    for (std::size_t i = 0; i < surf.mesh.vertexCount(); ++i) {
        const float* v = &surf.mesh.vertices[i * 11];
        const float nl = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
        if (std::fabs(nl - 1.f) > 1e-3f) unitNormals = false;
        if (!std::isfinite(surf.curvature[i])) finiteCurv = false;
        if (surf.material[i] == static_cast<unsigned char>(SurfaceMaterial::Keratin)) sawKeratin = true;
        if (surf.material[i] == static_cast<unsigned char>(SurfaceMaterial::Skin)) sawSkin = true;
        CHECK(surf.material[i] <= 2);
    }
    CHECK(unitNormals);
    CHECK(finiteCurv);
    CHECK(sawSkin);
    CHECK(sawKeratin); // the demo herbivore has horns -> sharp keratin prims on the surface
}

void testReactionDiffusion() {
    Pattern pt;
    pt.seed = 11u;
    pt.iterations = 400;
    const auto m1 = reactionDiffusion(pt);
    const auto m2 = reactionDiffusion(pt);
    CHECK(m1.size() == static_cast<std::size_t>(pt.gridU) * pt.gridV);
    CHECK(m1 == m2); // deterministic
    float mn = 1.f, mx = 0.f;
    double mean = 0.0, var = 0.0;
    for (float v : m1) {
        mn = std::fmin(mn, v);
        mx = std::fmax(mx, v);
        mean += v;
    }
    mean /= static_cast<double>(m1.size());
    for (float v : m1) var += (v - mean) * (v - mean);
    var /= static_cast<double>(m1.size());
    CHECK(mn >= 0.f && mx <= 1.f);
    CHECK(var > 1e-4); // a real Turing pattern formed (spots/stripes), not a flat field
    // anisotropy stretches the pattern along v: stripes variant correlates more along v
    Pattern stripes = pt;
    stripes.anisotropy = 6.f;
    const auto ms = reactionDiffusion(stripes);
    CHECK(ms.size() == m1.size());
    CHECK(ms != m1);
}

void testBakeMaterial() {
    const Skeleton s = demoSkeleton();
    Anatomy an;
    an.seed = 5u;
    Palette pal;
    Pattern pt;
    pt.seed = 9u;
    pt.iterations = 300;
    const int S = 96;
    const BakedMaps maps = bakeMaterial(s, an, pal, pt, S);
    CHECK(maps.size == S);
    CHECK(maps.albedo.size() == static_cast<std::size_t>(S) * S * 4);
    CHECK(maps.normal.size() == maps.albedo.size());
    CHECK(maps.material.size() == static_cast<std::size_t>(S) * S);
    bool alphaOk = true, varies = false;
    const unsigned char first = maps.albedo[0];
    for (std::size_t i = 0; i < maps.albedo.size(); i += 4) {
        alphaOk = alphaOk && maps.albedo[i + 3] == 255 && maps.normal[i + 3] == 255;
        if (maps.albedo[i] != first) varies = true;
    }
    CHECK(alphaOk);
    CHECK(varies);
    const BakedMaps maps2 = bakeMaterial(s, an, pal, pt, S);
    CHECK(maps2.albedo == maps.albedo && maps2.normal == maps.normal); // deterministic
    // countershading: average luminance on the back (u ~ 0.75, +y of the cross-section)
    // is darker than on the belly (u ~ 0.25)
    double backSum = 0.0, bellySum = 0.0;
    int cnt = 0;
    for (int j = 0; j < S; ++j)
        for (int i = 0; i < S; ++i) {
            const float u = (i + 0.5f) / S;
            const std::size_t idx = (static_cast<std::size_t>(j) * S + i) * 4;
            if (u > 0.70f && u < 0.80f) backSum += lum(&maps.albedo[idx]);
            if (u > 0.20f && u < 0.30f) bellySum += lum(&maps.albedo[idx]);
            ++cnt;
        }
    backSum /= cnt / 8.0;
    bellySum /= cnt / 8.0;
    CHECK(backSum < bellySum);
}

void testIk() {
    Vec3 mid;
    // reachable target: root (0,1,0), target (0,0,0.5), l1 = l2 = 0.6
    CHECK(solveTwoBoneIk({0.f, 1.f, 0.f}, {0.f, 0.f, 0.5f}, 0.6f, 0.6f, {1.f, 0.f, 0.f}, mid));
    CHECK_NEAR(fuhgl::math::length(mid - Vec3{0.f, 1.f, 0.f}), 0.6f, 1e-4f);
    CHECK_NEAR(fuhgl::math::length(Vec3{0.f, 0.f, 0.5f} - mid), 0.6f, 1e-4f);
    // unreachable: clamped to full stretch, still a valid solution
    CHECK(solveTwoBoneIk({0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, 0.5f, 0.5f, {0.f, 1.f, 0.f}, mid));
    CHECK_NEAR(fuhgl::math::length(mid), 0.5f, 1e-4f);
    // degenerate: zero-length target
    CHECK(!solveTwoBoneIk({0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 0.5f, 0.5f, {0.f, 1.f, 0.f}, mid));
    // bend plane: (mid - root) stays perpendicular to the rotation axis cross(dir, bendAxis),
    // i.e. the solution lies in the plane spanned by the target direction and the bend axis
    const Vec3 ikRoot{0.f, 1.f, 0.f}, ikTarget{0.3f, 0.f, 0.2f};
    CHECK(solveTwoBoneIk(ikRoot, ikTarget, 0.6f, 0.6f, {0.f, 0.f, 1.f}, mid));
    const Vec3 ikDir = fuhgl::math::normalize(ikTarget - ikRoot);
    const Vec3 ikAxis = fuhgl::math::cross(ikDir, Vec3{0.f, 0.f, 1.f});
    CHECK_NEAR(fuhgl::math::dot(mid - ikRoot, ikAxis), 0.f, 1e-4f);
}

void testGaitPose() {
    const Skeleton s = demoSkeleton();
    Gait gait;
    gait.cycleHz = 1.2f;
    gait.strideLength = 0.4f;
    gait.liftHeight = 0.1f;
    gait.spineWaveAmp = 0.05f;
    gait.breathAmp = 0.03f;
    gait.breathHz = 0.4f;
    const auto flat = [](float, float) { return 0.f; };
    const auto hilly = [](float x, float z) { return 0.15f * std::sin(2.f * x) * std::cos(1.7f * z); };
    const auto p1 = creatureGaitPose(s, gait, 0.3f, hilly);
    const auto p2 = creatureGaitPose(s, gait, 0.3f, hilly);
    CHECK(p1.size() == s.nodes.size());
    CHECK(p1 == p2); // deterministic
    // feet rest on the terrain heightfield (within the swing lift)
    for (int i = 0; i < static_cast<int>(s.nodes.size()); ++i) {
        if (s.nodes[i].part != fuhglgraphics::procedural::BodyPart::Foot) continue;
        const float ground = hilly(p1[i].x, p1[i].z);
        CHECK(p1[i].y >= ground - 1e-4f);
        CHECK(p1[i].y <= ground + gait.liftHeight + 1e-4f);
    }
    // legs are out of phase: two different legs at the same time are at different heights
    // or positions (not all feet identical)
    bool differ = false;
    for (int i = 0; i < static_cast<int>(s.nodes.size()) && !differ; ++i)
        for (int k = i + 1; k < static_cast<int>(s.nodes.size()); ++k)
            if (s.nodes[i].part == fuhglgraphics::procedural::BodyPart::Foot &&
                s.nodes[k].part == fuhglgraphics::procedural::BodyPart::Foot &&
                fuhgl::math::length(p1[i] - p1[k]) > 1e-6f)
                differ = true;
    CHECK(differ);
    // animation actually moves: pose at t and t + half cycle differ
    const auto p3 = creatureGaitPose(s, gait, 0.3f + 0.5f / gait.cycleHz, flat);
    bool moved = false;
    for (std::size_t i = 0; i < p1.size(); ++i)
        if (fuhgl::math::length(p1[i] - p3[i]) > 1e-6f) moved = true;
    CHECK(moved);
    // empty terrain function -> flat ground
    const auto p4 = creatureGaitPose(s, gait, 0.1f, {});
    CHECK(p4.size() == s.nodes.size());
    for (int i = 0; i < static_cast<int>(s.nodes.size()); ++i)
        if (s.nodes[i].part == fuhglgraphics::procedural::BodyPart::Foot)
            CHECK(p4[i].y >= -1e-4f && p4[i].y <= gait.liftHeight + 1e-4f);
}

} // namespace

void runProceduralCreaturegenTests() {
    testAllometry();
    testSymmetry();
    testAnatomySdf();
    testExtractSurface();
    testReactionDiffusion();
    testBakeMaterial();
    testIk();
    testGaitPose();
}
