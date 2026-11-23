// Unit tests for G30 procedural creatures (label "unit"): skeleton graph + finalize (world
// positions, prim count, bounds), creatureSdf (sign inside/outside, determinism, continuity
// along a limb line), meshCreature (valid indexed mesh, unit gradient normals, uv in [0,1],
// curvature shading in the color channel — neck of a two-sphere dumbbell darker than the pole,
// determinism), bakeCreatureMaps (sizes, alpha, determinism, variation), presets differ.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/creature.hpp>

#include <cmath>

namespace {

using fuhglgraphics::procedural::CreatureMesh;
using fuhglgraphics::procedural::CreatureParams;
using fuhglgraphics::procedural::Skeleton;
using fuhglgraphics::procedural::creatureSdf;
using fuhglgraphics::procedural::meshCreature;
using fuhglgraphics::procedural::bakeCreatureMaps;
using fuhgl::math::Vec3;

float lum(const float* v) { return 0.2126f * v[6] + 0.7152f * v[7] + 0.0722f * v[8]; }

void testSkeleton() {
    Skeleton q = fuhglgraphics::procedural::makeQuadruped();
    CHECK(q.nodes.size() == 22); // torso neck head jaw 2 horns 12 leg 4 tail
    CHECK(q.finalized());
    CHECK(q.prims().size() == q.nodes.size() * 2 - 1); // ellipsoid per node + capsule per link
    // world positions accumulate parent offsets
    int torso = -1, neck = -1;
    for (int i = 0; i < static_cast<int>(q.nodes.size()); ++i) {
        if (q.nodes[i].name == "torso") torso = i;
        if (q.nodes[i].name == "neck") neck = i;
    }
    CHECK(torso >= 0 && neck >= 0);
    const Vec3 expect = q.worldPos(torso) + q.nodes[neck].offset;
    const Vec3 got = q.worldPos(neck);
    CHECK_NEAR(got.x, expect.x, 1e-6f);
    CHECK_NEAR(got.y, expect.y, 1e-6f);
    CHECK_NEAR(got.z, expect.z, 1e-6f);
    Vec3 mn, mx;
    q.bounds(mn, mx);
    CHECK(mx.x - mn.x > 1.f && mx.y - mn.y > 0.5f && mx.z - mn.z > 1.5f); // body spans along +Z
    CHECK(mn.y > 0.f);                                                     // feet above ground-ish
}

void testSdf() {
    const Skeleton q = fuhglgraphics::procedural::makeQuadruped();
    CreatureParams pr;
    pr.seed = 7u;
    const Vec3 torsoCenter = q.worldPos(0);
    CHECK(creatureSdf(q, pr, torsoCenter) < 0.f);          // inside the torso
    CHECK(creatureSdf(q, pr, {100.f, 100.f, 100.f}) > 0.f);  // far outside
    // determinism
    CHECK(creatureSdf(q, pr, {0.3f, 1.1f, -0.2f}) == creatureSdf(q, pr, {0.3f, 1.1f, -0.2f}));
    // continuity along the torso->head line (no jumps despite smin + noise)
    const Vec3 a = q.worldPos(0), b = q.worldPos(2); // torso -> head
    float prev = creatureSdf(q, pr, a);
    float maxJump = 0.f;
    for (int i = 1; i <= 200; ++i) {
        const float t = i / 200.f;
        const float f = creatureSdf(q, pr, a + (b - a) * t);
        CHECK(std::isfinite(f));
        maxJump = std::fmax(maxJump, std::fabs(f - prev));
        prev = f;
    }
    CHECK(maxJump < 0.2f);
    // noise modulation bounded by detailAmp
    CreatureParams flat = pr;
    flat.detailAmp = 0.f;
    const float d0 = creatureSdf(q, flat, {0.31f, 1.07f, -0.11f});
    const float d1 = creatureSdf(q, pr, {0.31f, 1.07f, -0.11f});
    CHECK(std::fabs(d1 - d0) <= pr.detailAmp * 1.05f + 1e-4f);
}

void testMesh() {
    const Skeleton q = fuhglgraphics::procedural::makeQuadruped();
    CreatureParams pr;
    pr.seed = 3u;
    const CreatureMesh cm = meshCreature(q, pr, 48, {1.f, 1.f, 1.f}, 6.f);
    CHECK(!cm.mesh.indices.empty());
    CHECK(cm.mesh.indices.size() % 3 == 0);
    CHECK(cm.mesh.vertices.size() == cm.mesh.vertexCount() * 11);
    bool uvOk = true, colOk = true, finite = true;
    float minLum = 1e9f, maxLum = -1e9f;
    for (std::uint32_t i = 0; i < cm.mesh.vertexCount(); ++i) {
        const float* v = &cm.mesh.vertices[i * 11];
        for (int k = 0; k < 11; ++k) finite = finite && std::isfinite(v[k]);
        const float nl = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
        CHECK_NEAR(nl, 1.f, 1e-3f); // analytic gradient normals
        uvOk = uvOk && v[9] >= -1e-4f && v[9] <= 1.f + 1e-4f && v[10] >= -1e-4f && v[10] <= 1.f + 1e-4f;
        const float l = lum(v);
        colOk = colOk && l > 0.f && l <= 2.f;
        minLum = std::fmin(minLum, l);
        maxLum = std::fmax(maxLum, l);
    }
    CHECK(finite && uvOk && colOk);
    CHECK(maxLum > minLum + 0.05f); // curvature shading actually varies
    for (const std::uint32_t idx : cm.mesh.indices) CHECK(idx < cm.mesh.vertexCount());
    // determinism
    const CreatureMesh cm2 = meshCreature(q, pr, 48, {1.f, 1.f, 1.f}, 6.f);
    CHECK(cm2.mesh.vertices == cm.mesh.vertices);
}

void testCurvatureSign() {
    // two overlapping spheres (dumbbell): the smin neck is concave -> darker vertex color
    // than the convex poles, when curvature is written into the color channel
    Skeleton s;
    s.addNode("a", -1, {0.f, 0.f, 0.f}, 0.5f, {1.f, 1.f, 1.f}, fuhglgraphics::procedural::BodyPart::Torso);
    s.addNode("b", 0, {0.f, 0.f, 0.55f}, 0.5f, {1.f, 1.f, 1.f}, fuhglgraphics::procedural::BodyPart::Head);
    s.finalize();
    CreatureParams pr;
    pr.seed = 1u;
    pr.smoothK = 0.35f;
    pr.detailAmp = 0.f; // clean geometry for the sign check
    const CreatureMesh cm = meshCreature(s, pr, 48, {1.f, 1.f, 1.f}, 6.f);
    CHECK(!cm.mesh.indices.empty());
    // neck vertex: closest to the axis point at the segment midpoint, near the axis;
    // pole vertex: the farthest +z vertex (convex tip of sphere b)
    float bestNeck = 1e9f, neckLum = 0.f;
    float maxZ = -1e9f, poleLum = 0.f;
    for (std::uint32_t i = 0; i < cm.mesh.vertexCount(); ++i) {
        const float* v = &cm.mesh.vertices[i * 11];
        const Vec3 p{v[0], v[1], v[2]};
        const float radial = std::sqrt(p.x * p.x + p.y * p.y);
        const float dn = std::fabs(p.z - 0.275f) + radial;
        if (dn < bestNeck) { bestNeck = dn; neckLum = lum(v); }
        if (p.z > maxZ) { maxZ = p.z; poleLum = lum(v); }
    }
    CHECK(bestNeck < 0.6f);  // a vertex near the neck exists
    CHECK(neckLum < poleLum); // concave neck darker than the convex pole
}

void testBake() {
    const Skeleton q = fuhglgraphics::procedural::makeQuadruped();
    CreatureParams pr;
    pr.seed = 5u;
    const auto maps = bakeCreatureMaps(q, pr, 32);
    CHECK(maps.size == 32);
    CHECK(maps.albedo.size() == 32u * 32u * 4u);
    CHECK(maps.normal.size() == 32u * 32u * 4u);
    bool alphaOk = true, varies = false;
    const unsigned first = maps.albedo[0];
    for (std::size_t i = 0; i < maps.albedo.size(); i += 4) {
        alphaOk = alphaOk && maps.albedo[i + 3] == 255 && maps.normal[i + 3] == 255;
        if (maps.albedo[i] != first) varies = true;
    }
    CHECK(alphaOk);
    CHECK(varies); // real pattern, not a flat fill
    const auto maps2 = bakeCreatureMaps(q, pr, 32);
    CHECK(maps2.albedo == maps.albedo && maps2.normal == maps.normal); // deterministic
}

void testPresetsDiffer() {
    const Skeleton q = fuhglgraphics::procedural::makeQuadruped();
    const Skeleton r = fuhglgraphics::procedural::makeReptile();
    Vec3 qmn, qmx, rmn, rmx;
    q.bounds(qmn, qmx);
    r.bounds(rmn, rmx);
    // reptile is flatter and longer (tail) than the quadruped
    CHECK((rmx.y - rmn.y) < (qmx.y - qmn.y));
    CHECK((rmx.z - rmn.z) > (qmx.z - qmn.z));
}

} // namespace

void runProceduralCreatureTests() {
    testSkeleton();
    testSdf();
    testMesh();
    testCurvatureSign();
    testBake();
    testPresetsDiffer();
}
