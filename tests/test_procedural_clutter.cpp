// Unit tests for the G39a causal clutter scatter (label "unit"): slope-driven rockfall at
// cliff feet, flow-driven riverbed pebbles/driftwood and waterline reeds, parent-child litter
// under trees (incl. humidity-driven mushrooms on the north side), building/road-edge
// clutter, poisson + noise cluster masks, determinism.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/clutter.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace {

using namespace fuhglgraphics::procedural;

struct TestWorld {
    std::vector<float> h;
    std::vector<float> flow;
    std::vector<float> humidity;
    std::vector<unsigned char> biome;
    std::vector<float> roadMask;
    ClutterInput in;
};

// 65x65 grid over 120 world units; flat 0.5 land with a single cliff step at x=32..33 (east
// plateau) and a river channel at z=29..36 (below water level 0.3) with smooth banks on both
// sides, forest biome in the south. The plateau ramps down to the channel so the x-step is the
// ONLY steep slope in the world (rockfall must not appear anywhere else).
TestWorld makeWorld() {
    TestWorld w;
    const int g = 65;
    w.h.assign(static_cast<std::size_t>(g) * g, 0.5f);
    w.flow.assign(w.h.size(), 0.f);
    w.humidity.assign(w.h.size(), 0.4f);
    w.biome.assign(w.h.size(), 3); // grassland
    w.roadMask.assign(w.h.size(), 0.f);
    // shore profile in z (west side i<33): banks + channel, all z-steps <= 0.06 so the central
    // difference slope stays <= 0.32 (below the cliff band 0.495) everywhere except the x-step
    auto shoreH = [](int j, float& flow) -> float {
        if (j <= 25 || j >= 40) { flow = 0.f; return 0.50f; }
        static const float hz[15] = {0.44f, 0.38f, 0.32f, 0.26f, 0.20f, 0.14f, 0.10f, 0.10f,
                                     0.14f, 0.20f, 0.26f, 0.32f, 0.38f, 0.44f, 0.50f};
        const float h = hz[j - 26]; // j = 26..40
        if (j >= 29 && j <= 36) flow = 1.0f;      // channel
        else if (j == 28 || j == 37) flow = 0.8f;  // near banks
        else if (j == 27 || j == 38) flow = 0.3f;  // waterline band
        else flow = 0.15f;
        return h;
    };
    // cliff plateau (east, i>=33) ramping down into the channel with the same gentle 0.06 steps
    auto plateauH = [](int j) -> float {
        if (j <= 25 || j >= 46) return 0.90f;
        static const float ramp[20] = {0.84f, 0.78f, 0.72f, 0.66f, 0.60f, 0.54f, 0.48f, 0.42f,
                                       0.36f, 0.30f, 0.36f, 0.42f, 0.48f, 0.54f, 0.60f, 0.66f,
                                       0.72f, 0.78f, 0.84f, 0.90f};
        return ramp[j - 26]; // j = 26..45
    };
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            const std::size_t idx = static_cast<std::size_t>(i) + g * j;
            float flow = 0.f;
            const float shore = shoreH(j, flow);
            w.h[idx] = i >= 33 ? std::max(shore, plateauH(j)) : shore;
            w.flow[idx] = flow;
            if (j > 40) { w.biome[idx] = 4; w.humidity[idx] = 0.85f; } // forest, damp
            // a road strip with frayed edges (mask rim at i=10 and i=14)
            if (i >= 11 && i <= 13) w.roadMask[idx] = 1.f;
            if (i == 10 || i == 14) w.roadMask[idx] = 0.3f;
        }
    w.in.heightmap = &w.h;
    w.in.gridW = g;
    w.in.gridH = g;
    w.in.sizeX = 120.f;
    w.in.sizeZ = 120.f;
    w.in.heightScale = 10.f;
    w.in.waterLevel = 0.3f;
    w.in.flow = &w.flow;
    w.in.humidity = &w.humidity;
    w.in.biome = &w.biome;
    w.in.roadMask = &w.roadMask;
    return w;
}

float worldX(const TestWorld& w, float gx) { return (gx / (w.in.gridW - 1) - 0.5f) * w.in.sizeX; }

void testRockfallAtCliffFoot() {
    TestWorld w = makeWorld();
    ClutterGenParams p;
    p.seed = 5u;
    // rockfall weight: ~0 on flat land, high at/above the cliff (z on land, not in the channel)
    const float landZ = worldX(w, 10.f);
    CHECK(ClutterGenerator::rockfallWeight(w.in, p, worldX(w, 10.f), landZ) < 0.05f); // flat land
    CHECK(ClutterGenerator::rockfallWeight(w.in, p, worldX(w, 32.f), landZ) > 0.5f);  // cliff step
    CHECK(ClutterGenerator::rockfallWeight(w.in, p, worldX(w, 40.f), landZ) < 0.05f); // plateau top: flat
    // generate: rockfall items exist and sit on the high-slope side (east of the step)
    ClutterGenerator cg;
    p.rockfallCount = 200;
    p.pebbleCount = 0;
    p.driftwoodCount = 0;
    p.reedCount = 0;
    p.litterPerTree = 0;
    p.mushroomPerTree = 0;
    p.roadEdgeClutterCount = 0;
    w.in.treeSpots.clear();
    w.in.buildingSpots.clear();
    const auto items = cg.generate(w.in, p);
    int rockfall = 0;
    for (const auto& it : items) {
        if (it.kind != ClutterKind::Rockfall) continue;
        ++rockfall;
        // at the cliff step / its foot, not on the flat land far west
        CHECK(it.position.x > worldX(w, 28.f) && it.position.x < worldX(w, 38.f)); // at the cliff step
        CHECK(it.mesh.vertexCount() > 0);
    }
    CHECK(rockfall > 20);
}

void testRiverbedAndReeds() {
    TestWorld w = makeWorld();
    ClutterGenParams p;
    p.seed = 6u;
    p.rockfallCount = 0;
    p.reedCount = 120;
    p.pebbleCount = 150;
    p.driftwoodCount = 20;
    p.litterPerTree = 0;
    p.mushroomPerTree = 0;
    p.roadEdgeClutterCount = 0;
    w.in.treeSpots.clear();
    w.in.buildingSpots.clear();
    // weights: riverbed only in the channel, reeds only at the waterline band
    CHECK(ClutterGenerator::riverbedWeight(w.in, 0.f, worldX(w, 32.f)) > 0.5f);  // channel
    CHECK(ClutterGenerator::riverbedWeight(w.in, 0.f, worldX(w, 10.f)) == 0.f);   // dry land
    CHECK(ClutterGenerator::waterlineWeight(w.in, 0.f, worldX(w, 28.f)) > 0.2f); // shore band
    CHECK(ClutterGenerator::waterlineWeight(w.in, 0.f, worldX(w, 32.f)) == 0.f);  // deep channel: no reeds
    ClutterGenerator cg;
    const auto items = cg.generate(w.in, p);
    int pebbles = 0, wood = 0, reeds = 0;
    for (const auto& it : items) {
        const float z = it.position.z;
        if (it.kind == ClutterKind::Pebble || it.kind == ClutterKind::Driftwood) {
            CHECK(std::fabs(z - worldX(w, 32.f)) < 18.f); // inside the channel band
            if (it.kind == ClutterKind::Pebble) ++pebbles; else ++wood;
        } else if (it.kind == ClutterKind::Reed) {
            ++reeds;
            CHECK(std::fabs(z - worldX(w, 32.f)) < 18.f); // near the banks
        }
    }
    CHECK(pebbles > 20);
    CHECK(reeds > 5);
    (void)wood;
}

void testTreeLitterAndMushrooms() {
    TestWorld w = makeWorld();
    w.in.treeSpots = {{worldX(w, 50.f), worldX(w, 50.f)}}; // one tree in the damp forest
    w.in.treeRadii = {3.5f};
    ClutterGenParams p;
    p.seed = 7u;
    p.rockfallCount = 0;
    p.pebbleCount = 0;
    p.driftwoodCount = 0;
    p.reedCount = 0;
    p.litterPerTree = 6;
    p.mushroomPerTree = 4;
    p.roadEdgeClutterCount = 0;
    w.in.buildingSpots.clear();
    CHECK(ClutterGenerator::treeDistance(w.in, worldX(w, 50.f), worldX(w, 50.f)) < 0.1f);
    CHECK(ClutterGenerator::treeDistance(w.in, worldX(w, 5.f), worldX(w, 5.f)) > 10.f);
    ClutterGenerator cg;
    const auto items = cg.generate(w.in, p);
    CHECK(!items.empty());
    int mushrooms = 0;
    for (const auto& it : items) {
        // litter only inside the tree's scatter radius
        CHECK(ClutterGenerator::treeDistance(w.in, it.position.x, it.position.z) <= 4.0f);
        if (it.kind == ClutterKind::Mushroom) {
            ++mushrooms;
            CHECK(it.position.z < worldX(w, 50.f)); // north side (-z)
        }
    }
    CHECK(mushrooms > 0); // damp forest -> mushrooms on the north side
}

void testBuildingAndRoadClutter() {
    TestWorld w = makeWorld();
    w.in.buildingSpots = {{worldX(w, 20.f), worldX(w, 20.f)}};
    ClutterGenParams p;
    p.seed = 8u;
    p.rockfallCount = 0;
    p.pebbleCount = 0;
    p.driftwoodCount = 0;
    p.reedCount = 0;
    p.litterPerTree = 0;
    p.mushroomPerTree = 0;
    p.roadEdgeClutterCount = 60;
    w.in.treeSpots.clear();
    CHECK(ClutterGenerator::buildingDistance(w.in, worldX(w, 20.f), worldX(w, 20.f)) < 0.1f);
    CHECK(ClutterGenerator::roadEdgeDistance(w.in, worldX(w, 10.f), 0.f) < 0.5f);  // frayed rim: yes
    CHECK(ClutterGenerator::roadEdgeDistance(w.in, worldX(w, 12.f), 0.f) > 0.5f);  // road center: no
    CHECK(ClutterGenerator::roadEdgeDistance(w.in, worldX(w, 20.f), 0.f) > 0.5f);  // off-road: no
    ClutterGenerator cg;
    const auto items = cg.generate(w.in, p);
    std::set<ClutterKind> kinds;
    int nearBuilding = 0, nearRoad = 0;
    for (const auto& it : items) {
        kinds.insert(it.kind);
        if (ClutterGenerator::buildingDistance(w.in, it.position.x, it.position.z) < 8.f) ++nearBuilding;
        if (ClutterGenerator::roadEdgeDistance(w.in, it.position.x, it.position.z) < 0.5f) ++nearRoad;
    }
    CHECK(nearBuilding > 0); // crates/tiles/gravel/weeds around the building
    CHECK(nearRoad > 0);     // weed tufts at the road rim
    CHECK(kinds.count(ClutterKind::Crate) + kinds.count(ClutterKind::RoofTile) +
          kinds.count(ClutterKind::GravelPile) + kinds.count(ClutterKind::WeedTuft) > 0);
}

void testClusterMaskAndDeterminism() {
    ClutterGenParams p;
    p.seed = 9u;
    // cluster mask: 0 where noise below threshold, 1 above — and spatially clustered
    int zero = 0, one = 0;
    for (int i = 0; i < 200; ++i) {
        const float m = ClutterGenerator::clusterMask(p, p.seed, i * 0.7f, 3.3f);
        CHECK(m >= 0.f && m <= 1.f);
        if (m == 0.f) ++zero; else if (m == 1.f) ++one;
    }
    CHECK(zero > 0 && one > 0); // both nests and empty ground exist
    // determinism
    TestWorld w = makeWorld();
    w.in.treeSpots = {{0.f, -20.f}};
    w.in.treeRadii = {3.f};
    w.in.buildingSpots = {{10.f, 10.f}};
    ClutterGenerator cg;
    const auto a = cg.generate(w.in, p);
    const auto b = cg.generate(w.in, p);
    CHECK(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].kind == b[i].kind);
        CHECK(a[i].position.x == b[i].position.x && a[i].position.z == b[i].position.z);
        CHECK(a[i].mesh.vertices == b[i].mesh.vertices);
    }
    // maxItems cap respected
    ClutterGenParams p2 = p;
    p2.maxItems = 25;
    CHECK(static_cast<int>(cg.generate(w.in, p2).size()) <= 25);
}

} // namespace

void runProceduralClutterTests() {
    testRockfallAtCliffFoot();
    testRiverbedAndReeds();
    testTreeLitterAndMushrooms();
    testBuildingAndRoadClutter();
    testClusterMaskAndDeterminism();
}
