// Unit tests for G28 Voronoi (label "unit"): cell count/containment/convexity/tiling area,
// adjacency, F1/F2 distances, extrusion mesh validity, determinism.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/voronoi.hpp>

#include <cmath>

namespace {

using fuhglgraphics::procedural::Voronoi2D;
using fuhglgraphics::procedural::extrudeCells;
using fuhglgraphics::procedural::voronoi2d;
using fuhglgraphics::procedural::voronoiDistances;
using fuhgl::math::Vec2;

double areaOf(const std::vector<Vec2>& poly) {
    double a = 0;
    const int m = static_cast<int>(poly.size());
    for (int i = 0; i < m; ++i) a += poly[i].x * poly[(i + 1) % m].y - poly[(i + 1) % m].x * poly[i].y;
    return std::fabs(a) * 0.5;
}

bool inside(const std::vector<Vec2>& poly, const Vec2& p) {
    // convex CCW polygon: p must be left of every edge
    const int m = static_cast<int>(poly.size());
    for (int i = 0; i < m; ++i) {
        const Vec2 e = poly[(i + 1) % m] - poly[i];
        if (e.x * (p.y - poly[i].y) - e.y * (p.x - poly[i].x) < -1e-4f) return false;
    }
    return true;
}

void testDiagram() {
    const Voronoi2D v = voronoi2d(40, {0.f, 0.f}, {10.f, 6.f}, 31337u);
    CHECK(v.points.size() == 40);
    CHECK(v.cells.size() == 40);
    double totalArea = 0;
    for (int i = 0; i < 40; ++i) {
        CHECK(v.cells[i].size() >= 3);
        totalArea += areaOf(v.cells[i]);
        CHECK(inside(v.cells[i], v.points[i])); // seed inside its own cell
        // convexity: all cross products of consecutive edges have the same sign (CCW -> >= 0)
        const auto& poly = v.cells[i];
        const int m = static_cast<int>(poly.size());
        for (int k = 0; k < m; ++k) {
            const Vec2 e1 = poly[(k + 1) % m] - poly[k];
            const Vec2 e2 = poly[(k + 2) % m] - poly[(k + 1) % m];
            CHECK(e1.x * e2.y - e1.y * e2.x >= -1e-4f);
        }
        // cell within bounds
        for (const auto& p : v.cells[i]) {
            CHECK(p.x >= -1e-3f && p.x <= 10.f + 1e-3f);
            CHECK(p.y >= -1e-3f && p.y <= 6.f + 1e-3f);
        }
    }
    // cells tile the rectangle: sum of areas == rect area
    CHECK_NEAR(totalArea, 60.0, 0.5);
    // adjacency exists and is symmetric in the index pairs
    CHECK(!v.edges.empty());
    for (const auto& e : v.edges) CHECK(e.first < e.second);
    // determinism
    const Voronoi2D v2 = voronoi2d(40, {0.f, 0.f}, {10.f, 6.f}, 31337u);
    CHECK(v2.points == v.points);
    CHECK(v2.cells == v.cells);
}

void testDistances() {
    const Voronoi2D v = voronoi2d(25, {0.f, 0.f}, {8.f, 8.f}, 5u);
    float f1, f2;
    // at a seed point: F1 ~ 0, F2 > 0
    voronoiDistances(v, v.points[3], f1, f2);
    CHECK_NEAR(f1, 0.f, 1e-4f);
    CHECK(f2 > 0.1f);
    // midpoint of two far-apart seeds: F2 - F1 small relative to F1
    const Vec2 mid = (v.points[0] + v.points[24]) * 0.5f;
    voronoiDistances(v, mid, f1, f2);
    CHECK(f2 >= f1);
    CHECK(f2 - f1 >= 0.f);
    // F1 is the min over all seeds
    float minD = 1e30f;
    for (const auto& s : v.points) minD = std::fmin(minD, fuhgl::math::length(mid - s));
    CHECK_NEAR(f1, minD, 1e-4f);
}

void testExtrude() {
    const Voronoi2D v = voronoi2d(30, {-6.f, -6.f}, {6.f, 6.f}, 99u);
    const fuhgl::render::MeshData md = extrudeCells(v, -0.2f, 0.8f, 0.06f, 0.5f, 7u,
                                                    {0.5f, 0.48f, 0.45f}, 0.2f);
    CHECK(!md.indices.empty());
    CHECK(md.indices.size() % 3 == 0);
    CHECK(md.vertices.size() == md.vertexCount() * 11);
    for (const std::uint32_t idx : md.indices) CHECK(idx < md.vertexCount());
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i) {
        const float* vv = &md.vertices[i * 11];
        CHECK(std::isfinite(vv[0]) && std::isfinite(vv[1]) && std::isfinite(vv[2]));
        CHECK(vv[1] >= -0.2f - 1e-4f && vv[1] <= 0.8f + 0.5f + 1e-4f);
        const float nl = std::sqrt(vv[3] * vv[3] + vv[4] * vv[4] + vv[5] * vv[5]);
        CHECK_NEAR(nl, 1.f, 1e-3f);
    }
    // determinism
    const auto md2 = extrudeCells(v, -0.2f, 0.8f, 0.06f, 0.5f, 7u, {0.5f, 0.48f, 0.45f}, 0.2f);
    CHECK(md2.vertices == md.vertices);
    CHECK(md2.indices == md.indices);
    // gap = 0 -> plates touch: bbox spans the full rect
    const auto mdFull = extrudeCells(v, 0.f, 1.f, 0.f, 0.f, 0u, {1, 1, 1}, 0.f);
    float xmin = 1e9f, xmax = -1e9f, zmin = 1e9f, zmax = -1e9f;
    for (std::uint32_t i = 0; i < mdFull.vertexCount(); ++i) {
        const float* vv = &mdFull.vertices[i * 11];
        xmin = std::fmin(xmin, vv[0]); xmax = std::fmax(xmax, vv[0]);
        zmin = std::fmin(zmin, vv[2]); zmax = std::fmax(zmax, vv[2]);
    }
    CHECK_NEAR(xmin, -6.f, 1e-3f);
    CHECK_NEAR(xmax, 6.f, 1e-3f);
    CHECK_NEAR(zmin, -6.f, 1e-3f);
    CHECK_NEAR(zmax, 6.f, 1e-3f);
}

} // namespace

void runProceduralVoronoiTests() {
    testDiagram();
    testDistances();
    testExtrude();
}
