// Unit tests for G29 SDF (label "unit"): primitive signs/values, smooth-min, mandelbulb DE
// sanity, meshFromSdf on a sphere (closed manifold, correct radius) and on a mandelbulb
// (non-empty, bounded, finite).

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/sdf.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

using fuhglgraphics::procedural::meshFromSdf;
using fuhglgraphics::procedural::sdf::box;
using fuhglgraphics::procedural::sdf::mandelbulb;
using fuhglgraphics::procedural::sdf::plane;
using fuhglgraphics::procedural::sdf::roundedBox;
using fuhglgraphics::procedural::sdf::smoothMin;
using fuhglgraphics::procedural::sdf::sphere;
using fuhglgraphics::procedural::sdf::cylinder;
using fuhgl::math::Vec3;

void testPrimitives() {
    CHECK_NEAR(sphere({0, 0, 0}, 1.f), -1.f, 1e-6f);       // inside -> negative
    CHECK_NEAR(sphere({2, 0, 0}, 1.f), 1.f, 1e-6f);        // outside -> positive
    CHECK_NEAR(sphere({1, 0, 0}, 1.f), 0.f, 1e-6f);        // on surface
    CHECK(box({0, 0, 0}, {1, 1, 1}) < 0.f);
    CHECK_NEAR(box({1, 0, 0}, {1, 1, 1}), 0.f, 1e-6f);
    CHECK(box({3, 0, 0}, {1, 1, 1}) > 1.9f);
    CHECK(roundedBox({0, 0, 0}, {1, 1, 1}, 0.2f) < 0.f);
    CHECK(roundedBox({1.3f, 0, 0}, {1, 1, 1}, 0.2f) > 0.05f);
    CHECK_NEAR(plane({0, 5, 0}, 2.f), 3.f, 1e-6f);
    CHECK(cylinder({0, 0.5f, 0}, 1.f, 2.f) < 0.f);
    CHECK(cylinder({2, 0.5f, 0}, 1.f, 2.f) > 0.9f);
}

void testSmoothMin() {
    CHECK(smoothMin(1.f, 2.f, 0.5f) <= 1.f);
    CHECK(smoothMin(1.f, 2.f, 0.5f) > 0.9f);  // close to min when far apart
    CHECK(smoothMin(1.f, 1.f, 0.5f) < 1.f);   // blends when equal
    CHECK_NEAR(smoothMin(3.f, 5.f, 0.1f), 3.f, 1e-3f);
}

void testMandelbulb() {
    // origin is in the set -> DE ~ 0; far point -> large positive
    CHECK(std::fabs(mandelbulb({0.f, 0.f, 0.f})) < 1e-3f);
    CHECK(mandelbulb({3.f, 0.f, 0.f}) > 1.f);
    CHECK(mandelbulb({0.5f, 0.5f, 0.5f}) < 0.5f); // inside-ish region
    for (int i = 0; i < 200; ++i) {
        const float a = i * 0.13f;
        const Vec3 p{std::cos(a) * 0.8f, std::sin(a * 1.7f) * 0.5f, std::sin(a) * 0.8f};
        CHECK(std::isfinite(mandelbulb(p)));
    }
}

void testMeshSphere() {
    const fuhgl::render::MeshData md = meshFromSdf(
        [](const Vec3& p) { return sphere(p, 0.7f); }, {-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}, 28);
    CHECK(!md.indices.empty());
    CHECK(md.indices.size() % 3 == 0);
    // all vertices near the sphere shell
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i) {
        const float* v = &md.vertices[i * 11];
        const float r = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        CHECK(r > 0.55f && r < 0.85f);
    }
    // closed manifold
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edgeCount;
    for (std::size_t i = 0; i < md.indices.size(); i += 3) {
        const std::uint32_t tri[3] = {md.indices[i], md.indices[i + 1], md.indices[i + 2]};
        for (int e = 0; e < 3; ++e) ++edgeCount[std::minmax(tri[e], tri[(e + 1) % 3])];
    }
    for (const auto& kv : edgeCount) CHECK(kv.second == 2);
}

void testMeshMandelbulb() {
    const fuhgl::render::MeshData md = meshFromSdf(
        [](const Vec3& p) { return mandelbulb(p, 8.f, 10); }, {-1.35f, -1.35f, -1.35f},
        {1.35f, 1.35f, 1.35f}, 48);
    CHECK(md.indices.size() > 500);
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i) {
        const float* v = &md.vertices[i * 11];
        for (int k = 0; k < 11; ++k) CHECK(std::isfinite(v[k]));
        CHECK(std::fabs(v[0]) <= 1.4f && std::fabs(v[1]) <= 1.4f && std::fabs(v[2]) <= 1.4f);
        CHECK(fuhgl::math::length(Vec3{v[0], v[1], v[2]}) < 2.0f); // bulb fits in r ~ 1.3
    }
    for (const std::uint32_t idx : md.indices) CHECK(idx < md.vertexCount());
}

} // namespace

void runProceduralSdfTests() {
    testPrimitives();
    testSmoothMin();
    testMandelbulb();
    testMeshSphere();
    testMeshMandelbulb();
}
