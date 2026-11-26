// Unit tests for the G38 road/path network generator (label "unit"): A* least-cost routing
// with physics costs (slope detour/switchbacks, water fords vs. bridges, existing-road merge
// discount), Catmull-Rom smoothing + min curve radius, hierarchy (highway/country/trail),
// terrain bench carving, mask/rut/bridge/distance fields, mesh extrusion + props, and
// determinism per seed.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/roadgen.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using namespace fuhglgraphics::procedural;
using fuhgl::math::Vec2;

/// @brief Synthetic flat terrain input: grid g x g over `size` world units, heightScale hs,
/// water level wl. The heightmap is owned by the caller (returned via out param).
struct FlatWorld {
    std::vector<float> h;
    std::vector<float> flow;
    std::vector<unsigned char> biome;
    RoadInput in;
};

FlatWorld flatWorld(int g, float size, float hs, float wl, float baseH) {
    FlatWorld w;
    w.h.assign(static_cast<std::size_t>(g) * g, baseH);
    w.flow.assign(w.h.size(), 0.f);
    w.in.heightmap = &w.h;
    w.in.gridW = g;
    w.in.gridH = g;
    w.in.sizeX = size;
    w.in.sizeZ = size;
    w.in.heightScale = hs;
    w.in.waterLevel = wl;
    w.in.flow = &w.flow;
    return w;
}

float worldSlopeAt(const FlatWorld& w, int x, int z) {
    const int g = w.in.gridW;
    const float cell = w.in.sizeX / (g - 1);
    auto at = [&](int xx, int zz) {
        xx = std::min(g - 1, std::max(0, xx));
        zz = std::min(g - 1, std::max(0, zz));
        return w.h[static_cast<std::size_t>(xx) + g * zz];
    };
    const float gx = (at(x + 1, z) - at(x - 1, z)) * 0.5f * w.in.heightScale / cell;
    const float gz = (at(x, z + 1) - at(x, z - 1)) * 0.5f * w.in.heightScale / cell;
    return std::sqrt(gx * gx + gz * gz);
}

void testAstarSlopeDetour() {
    // flat land with a steep ridge across the map (except a gap at the north end):
    // the highway A* must detour through the gap instead of climbing the ridge
    FlatWorld w = flatWorld(61, 120.f, 10.f, 0.3f, 0.5f);
    const int g = 61;
    const int midX = 30;
    const int gapZ0 = 4, gapZ1 = 10;
    for (int z = 0; z < g; ++z)
        for (int x = 0; x < g; ++x)
            if (std::abs(x - midX) <= 1 && !(z >= gapZ0 && z <= gapZ1))
                w.h[static_cast<std::size_t>(x) + g * z] = 0.95f; // ridge top
    RoadGenParams p;
    const std::vector<float> noMask;
    const auto path = RoadGenerator::leastCostPath(w.in, p, RoadClass::Highway,
                                                   {-50.f, 40.f}, {50.f, 40.f}, noMask);
    CHECK(path.size() >= 2);
    // no path cell on the steep ridge walls (slope above the highway critical slope)
    int steep = 0, throughGap = 0;
    for (int c : path) {
        const int x = c % g, z = c / g;
        if (worldSlopeAt(w, x, z) > p.critSlopeHighway + 1e-3f) ++steep;
        if (std::abs(x - midX) <= 2 && z >= gapZ0 - 1 && z <= gapZ1 + 1) ++throughGap;
    }
    CHECK(steep == 0);       // the critical-slope penalty forced the detour
    CHECK(throughGap > 0);   // ...through the gap in the ridge
    // detour is longer than the straight line
    float len = 0.f;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const int a = path[i - 1], b = path[i];
        len += std::hypot(static_cast<float>((b % g) - (a % g)),
                          static_cast<float>((b / g) - (a / g))) * (120.f / 60.f);
    }
    CHECK(len > 100.f * 1.05f); // straight distance is 100
}

void testAstarFordVsBridge() {
    // a river channel across the whole map: shallow+narrow = ford (cheap, no bridge),
    // deep = bridge-costly (the path is still found, generate() flags it as bridge)
    RoadGenParams p;
    const std::vector<float> noMask;
    {
        FlatWorld w = flatWorld(61, 120.f, 10.f, 0.3f, 0.5f);
        const int g = 61;
        for (int z = 0; z < g; ++z)
            for (int x = 0; x < g; ++x)
                if (std::abs(x - 30) <= 1) {
                    w.h[static_cast<std::size_t>(x) + g * z] = 0.29f;  // shallow: depth 0.01
                    w.flow[static_cast<std::size_t>(x) + g * z] = 0.1f; // narrow stream
                }
        const auto path = RoadGenerator::leastCostPath(w.in, p, RoadClass::Country,
                                                       {-40.f, 0.f}, {40.f, 0.f}, noMask);
        CHECK(path.size() >= 2);
        int waterCells = 0;
        for (int c : path)
            if (w.h[static_cast<std::size_t>(c)] < w.in.waterLevel) ++waterCells;
        CHECK(waterCells > 0); // crossed at the ford (cheap), not around (map edge is far)
    }
    {
        FlatWorld w = flatWorld(61, 120.f, 10.f, 0.3f, 0.5f);
        const int g = 61;
        for (int z = 0; z < g; ++z)
            for (int x = 0; x < g; ++x)
                if (std::abs(x - 30) <= 1) {
                    w.h[static_cast<std::size_t>(x) + g * z] = 0.05f;  // deep: depth 0.25
                    w.flow[static_cast<std::size_t>(x) + g * z] = 1.0f; // wide river
                }
        const auto path = RoadGenerator::leastCostPath(w.in, p, RoadClass::Highway,
                                                       {-40.f, 0.f}, {40.f, 0.f}, noMask);
        CHECK(path.size() >= 2); // still found — crossing is expensive but finite
        int waterCells = 0;
        for (int c : path)
            if (w.h[static_cast<std::size_t>(c)] < w.in.waterLevel) ++waterCells;
        CHECK(waterCells > 0);
    }
}

void testAstarExistingRoadMerge() {
    // an existing road corridor along z=0: with the merge discount the new path bends into
    // the corridor; without discount it stays on the straight line
    FlatWorld w = flatWorld(61, 120.f, 10.f, 0.3f, 0.5f);
    std::vector<float> existing(w.h.size(), 0.f);
    const int g = 61;
    for (int x = 0; x < g; ++x) existing[static_cast<std::size_t>(x) + g * 30] = 1.f;
    RoadGenParams p;
    const auto pathDisc = RoadGenerator::leastCostPath(w.in, p, RoadClass::Country,
                                                       {-40.f, -6.f}, {40.f, -6.f}, existing);
    CHECK(pathDisc.size() >= 2);
    int onCorridor = 0;
    for (int c : pathDisc)
        if (existing[static_cast<std::size_t>(c)] > 0.9f) ++onCorridor;
    CHECK(onCorridor > 0); // merged into the existing road
    RoadGenParams p0 = p;
    p0.existingRoadDiscount = 0.f;
    const auto pathPlain = RoadGenerator::leastCostPath(w.in, p0, RoadClass::Country,
                                                        {-40.f, -6.f}, {40.f, -6.f}, existing);
    int onCorridor0 = 0;
    for (int c : pathPlain)
        if (existing[static_cast<std::size_t>(c)] > 0.9f) ++onCorridor0;
    CHECK(onCorridor0 == 0); // no discount -> straight line, no merge
}

void testSmoothPath() {
    // raw zackige Gitter-Linie with a 90 deg corner -> smooth spline, endpoints preserved,
    // shorter than the raw polyline, min curve radius respected
    const std::vector<Vec2> raw = {{0.f, 0.f}, {5.f, 0.f}, {10.f, 0.f}, {10.f, 5.f}, {10.f, 10.f}};
    const float minR = 4.f;
    const auto sm = RoadGenerator::smoothPath(raw, 1.f, minR);
    CHECK(sm.size() > raw.size());
    CHECK_NEAR(sm.front().x, raw.front().x, 1e-4f);
    CHECK_NEAR(sm.front().y, raw.front().y, 1e-4f);
    CHECK_NEAR(sm.back().x, raw.back().x, 1e-4f);
    CHECK_NEAR(sm.back().y, raw.back().y, 1e-4f);
    auto polyLen = [](const std::vector<Vec2>& v) {
        float l = 0.f;
        for (std::size_t i = 1; i < v.size(); ++i)
            l += std::hypot(v[i].x - v[i - 1].x, v[i].y - v[i - 1].y);
        return l;
    };
    CHECK(polyLen(sm) < polyLen(raw)); // corner cutting shortens the line
    float minRSeen = 1e9f;
    for (std::size_t i = 1; i + 1 < sm.size(); ++i) {
        const float la = std::hypot(sm[i].x - sm[i + 1].x, sm[i].y - sm[i + 1].y);
        const float lb = std::hypot(sm[i - 1].x - sm[i + 1].x, sm[i - 1].y - sm[i + 1].y);
        const float lc = std::hypot(sm[i - 1].x - sm[i].x, sm[i - 1].y - sm[i].y);
        const float area2 =
            std::fabs((sm[i].x - sm[i - 1].x) * (sm[i + 1].y - sm[i - 1].y) -
                      (sm[i + 1].x - sm[i - 1].x) * (sm[i].y - sm[i - 1].y));
        if (area2 > 1e-9f) minRSeen = std::min(minRSeen, la * lb * lc / (2.f * area2));
    }
    CHECK(minRSeen >= minR * 0.6f); // curve radius clamp worked (allow small slack)
}

void testGenerateHierarchyAndBridge() {
    // flat land, deep river across the middle, three sites -> highway across the river
    // (flagged bridge), country road from the minor site, desire-path trails
    FlatWorld w = flatWorld(65, 120.f, 12.f, 0.3f, 0.55f);
    const int g = 65;
    for (int z = 0; z < g; ++z)
        for (int x = 0; x < g; ++x)
            if (std::abs(x - 32) <= 1) {
                w.h[static_cast<std::size_t>(x) + g * z] = 0.05f;
                w.flow[static_cast<std::size_t>(x) + g * z] = 1.0f;
            }
    w.in.sites = {{{-30.f, 0.f}, 0.9f, 0}, {{30.f, 0.f}, 0.9f, 1}, {{0.f, 35.f}, 0.4f, 2}};
    RoadGenParams p;
    p.seed = 5u;
    RoadGenerator rg;
    const RoadNetwork net = rg.generate(w.in, p);
    CHECK(net.gridW == g && net.gridH == g);
    CHECK(net.mask.size() == static_cast<std::size_t>(g) * g);
    CHECK(net.rutMask.size() == net.mask.size());
    CHECK(net.bridgeMask.size() == net.mask.size());
    CHECK(net.dist.size() == net.mask.size());
    CHECK(net.paths.size() >= 3); // highway + country + at least one trail
    int highways = 0, countries = 0, trails = 0, bridgeFlagged = 0;
    for (const auto& path : net.paths) {
        if (path.cls == RoadClass::Highway) ++highways;
        if (path.cls == RoadClass::Country) ++countries;
        if (path.cls == RoadClass::Trail) ++trails;
        for (char b : path.bridge) if (b) ++bridgeFlagged;
        CHECK(path.points.size() >= 2);
        CHECK(path.bridge.size() == path.points.size());
        CHECK(path.length() > 0.f);
    }
    CHECK(highways >= 1 && countries >= 1 && trails >= 1);
    CHECK(bridgeFlagged > 0); // the forced river crossing is flagged as bridge
    // roads cross water ONLY as bridges: every path point below the water level is flagged
    for (const auto& path : net.paths)
        for (std::size_t i = 0; i < path.points.size(); ++i) {
            const int gx = static_cast<int>(std::min(1.f, std::max(0.f, path.points[i].x / w.in.sizeX + 0.5f)) * (g - 1));
            const int gz = static_cast<int>(std::min(1.f, std::max(0.f, path.points[i].z / w.in.sizeZ + 0.5f)) * (g - 1));
            const float hv = w.h[static_cast<std::size_t>(gx) + static_cast<std::size_t>(g) * gz];
            if (hv < w.in.waterLevel) CHECK(path.bridge[i] == 1); // never a sunken ford ribbon
        }
    int bridgeCells = 0;
    for (const unsigned char b : net.bridgeMask) if (b) ++bridgeCells;
    CHECK(bridgeCells > 0);
    // road mask in range, distance field: ~0 on the road, large far away
    for (const float m : net.mask) CHECK(m >= 0.f && m <= 1.f + 1e-6f);
    CHECK(net.roadDistanceAt(-30.f, 0.f) < 3.f);  // site A is a road hub
    CHECK(net.roadDistanceAt(0.f, -55.f) > 10.f);  // far corner has no road
    // terrain carving keeps the heightmap valid and flattens land near the road
    std::vector<float> carved = w.h;
    rg.carveTerrain(carved, net, p);
    for (const float hv : carved) CHECK(hv >= 0.f && hv <= 1.f);
    CHECK(carved != w.h); // the bench/embankment changed land cells (water untouched)
    int waterUntouched = 0, waterCells = 0;
    for (std::size_t i = 0; i < carved.size(); ++i) {
        if (w.h[i] < w.in.waterLevel) {
            ++waterCells;
            if (carved[i] == w.h[i]) ++waterUntouched;
        }
    }
    CHECK(waterCells > 0 && waterUntouched == waterCells);
    // mesh extrusion + props
    RoadNetwork net2 = net;
    rg.buildGeometry(net2, w.in, p);
    CHECK(net2.roadMesh.vertexCount() > 0);
    CHECK(net2.roadMesh.indices.size() % 3 == 0);
    CHECK(net2.bridgeMesh.vertexCount() > 0); // bridge deck + posts
    CHECK(net2.props.size() > 0);              // milestones on the long highway
    for (const auto& prop : net2.props) CHECK(prop.mesh.vertexCount() > 0);
    // determinism
    const RoadNetwork netB = rg.generate(w.in, p);
    CHECK(netB.mask == net.mask);
    CHECK(netB.paths.size() == net.paths.size());
    for (std::size_t i = 0; i < netB.paths.size(); ++i) {
        CHECK(netB.paths[i].points.size() == net.paths[i].points.size());
        for (std::size_t k = 0; k < netB.paths[i].points.size(); ++k)
            CHECK(netB.paths[i].points[k].x == net.paths[i].points[k].x &&
                  netB.paths[i].points[k].z == net.paths[i].points[k].z);
    }
}

void testGenerateEmptyAndSingleSite() {
    // no sites -> empty but well-formed network; one site -> only desire paths
    FlatWorld w = flatWorld(33, 60.f, 8.f, 0.3f, 0.5f);
    RoadGenParams p;
    p.seed = 9u;
    RoadGenerator rg;
    RoadInput empty = w.in;
    const RoadNetwork net0 = rg.generate(empty, p);
    CHECK(net0.paths.empty());
    CHECK(net0.mask.size() == w.h.size());
    CHECK(net0.roadDistanceAt(0.f, 0.f) > 1e8f);
    w.in.sites = {{{0.f, 0.f}, 0.9f, 0}};
    p.desirePathsPerSite = 2;
    const RoadNetwork net1 = rg.generate(w.in, p);
    CHECK(net1.paths.size() >= 1);
    CHECK(net1.paths.size() <= 2);
    for (const auto& path : net1.paths) CHECK(path.cls == RoadClass::Trail);
}

} // namespace

void runProceduralRoadgenTests() {
    testAstarSlopeDetour();
    testAstarFordVsBridge();
    testAstarExistingRoadMerge();
    testSmoothPath();
    testGenerateHierarchyAndBridge();
    testGenerateEmptyAndSingleSite();
}
