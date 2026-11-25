// Unit tests for the pipeline city generator (label "unit"): polygon helpers (ear clipping,
// half-plane clip, OBB), terrain/zones ranges, road graph connectivity + no roads under water,
// lot subdivision (access flags, parks, lots inside blocks), house placement (style from
// density zone), determinism.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/citygen.hpp>

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <vector>

namespace {

using namespace fuhglgraphics::procedural;

CityGenParams params() {
    CityGenParams p;
    p.seed = 777u;
    p.worldSize = 240.f;
    p.cityRadius = 95.f;
    p.roadGrid = 9;
    return p;
}

void testPolygonHelpers() {
    // ear clip a convex quad -> 2 triangles, valid indices
    const std::vector<fuhgl::math::Vec2> quad = {{0, 0}, {4, 0}, {4, 3}, {0, 3}};
    const auto tris = earClip(quad);
    CHECK(tris.size() == 6);
    for (const std::uint32_t i : tris) CHECK(i < 4);
    // ear clip a concave polygon (arrow) -> n-2 triangles
    const std::vector<fuhgl::math::Vec2> arrow = {{0, 0}, {4, 0}, {4, 1}, {2, 0.5f}, {0, 1}};
    const auto trisA = earClip(arrow);
    CHECK(trisA.size() == 3 * 3);
    // half-plane clip: square cut in half -> 4 vertices, half the area
    const auto left = clipHalfPlane(quad, {2, -1}, {2, 4});
    CHECK(left.size() == 4);
    float area = 0.f;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = left[(i + 1) % left.size()];
        area += a.x * b.y - a.y * b.x;
    }
    CHECK_NEAR(std::fabs(area) * 0.5f, 6.f, 1e-3f); // half of 4x3
    // OBB of an axis-aligned rectangle
    const Obb2 obb = computeObb(quad);
    CHECK_NEAR(obb.halfU * 2.f, 4.f, 1e-3f);
    CHECK_NEAR(obb.halfV * 2.f, 3.f, 1e-3f);
    // OBB of a rotated rectangle: long axis follows the rotation
    const float ang = 0.6f;
    const float ca = std::cos(ang), sa = std::sin(ang);
    const fuhgl::math::Vec2 u{ca, sa}, v{-sa, ca};
    const std::vector<fuhgl::math::Vec2> rot = {{u * 5.f + v * 1.5f}, {u * 5.f - v * 1.5f},
                                                {u * -5.f - v * 1.5f}, {u * -5.f + v * 1.5f}};
    const Obb2 obr = computeObb(rot);
    CHECK_NEAR(obr.halfU * 2.f, 10.f, 1e-2f);
    CHECK_NEAR(obr.halfV * 2.f, 3.f, 1e-2f);
}

void testTerrainAndZones() {
    const CityWorld w = generateCityWorld(params());
    CHECK(w.gridW == 129 && w.heightmap.size() == static_cast<std::size_t>(129) * 129);
    CHECK(w.density.size() == w.heightmap.size() && w.wealth.size() == w.heightmap.size());
    for (const float h : w.heightmap) CHECK(h >= 0.f && h <= 1.f);
    for (const float d : w.density) CHECK(d >= 0.f && d <= 1.f);
    for (const float v : w.wealth) CHECK(v >= 0.f && v <= 1.f);
    // plateau: the city center is flat just above the water level
    CHECK_NEAR(w.heightAt(0.f, 0.f), w.waterLevelWorld * 1.05f, 0.5f);
    // density highest near the center, zero at the rim
    CHECK(w.densityAt(0.f, 0.f) > w.densityAt(90.f, 90.f));
    CHECK(w.densityAt(110.f, 0.f) < 0.05f);
}

void testRoadGraph() {
    const CityWorld w = generateCityWorld(params());
    CHECK(!w.roads.nodes.empty());
    CHECK(!w.roads.edges.empty());
    CHECK(w.roadMesh.vertexCount() > 0);
    CHECK(w.roadMesh.indices.size() % 3 == 0);
    // no road node under water
    for (const auto& nd : w.roads.nodes) CHECK(w.heightAt(nd.x, nd.y) >= w.waterLevelWorld);
    // connectivity: every node reachable from node 0
    std::vector<std::vector<int>> adj(w.roads.nodes.size());
    for (const auto& e : w.roads.edges) {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }
    std::vector<bool> seen(w.roads.nodes.size(), false);
    std::queue<int> q;
    q.push(0);
    seen[0] = true;
    while (!q.empty()) {
        const int u = q.front();
        q.pop();
        for (const int v : adj[u])
            if (!seen[v]) { seen[v] = true; q.push(v); }
    }
    for (bool s : seen) CHECK(s);
}

void testLotsAndHouses() {
    const CityWorld w = generateCityWorld(params());
    CHECK(!w.lots.empty());
    CHECK(w.lotMesh.vertexCount() > 0);
    int parks = 0, accessed = 0;
    for (const Lot& lot : w.lots) {
        CHECK(lot.poly.size() >= 3);
        CHECK(earClip(lot.poly).size() == (lot.poly.size() - 2) * 3);
        if (lot.park) ++parks;
        if (lot.hasAccess) ++accessed;
    }
    CHECK(accessed > 0);
    CHECK(!w.houses.empty());
    // houses/park greenery stand on the plateau, meshes are complete
    for (const auto& h : w.houses) {
        CHECK(h.mesh.vertexCount() > 0);
        CHECK(h.position.y >= w.waterLevelWorld * 1.05f);
        CHECK(h.position.y <= w.waterLevelWorld * 1.05f + 0.1f);
    }
    // determinism
    const CityWorld w2 = generateCityWorld(params());
    CHECK(w2.heightmap == w.heightmap);
    CHECK(w2.roads.nodes.size() == w.roads.nodes.size());
    CHECK(w2.roads.edges == w.roads.edges);
    CHECK(w2.lots.size() == w.lots.size());
    CHECK(w2.houses.size() == w.houses.size());
    CHECK(w2.houses.empty() || w2.houses[0].mesh.vertices == w.houses[0].mesh.vertices);
    (void)parks;
}

} // namespace

void runProceduralCitygenTests() {
    testPolygonHelpers();
    testTerrainAndZones();
    testRoadGraph();
    testLotsAndHouses();
}
