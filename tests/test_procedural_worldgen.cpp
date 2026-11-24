// Unit tests for the world generator helper (label "unit"): generateWorld() in both modes —
// landscape (heightmap size/range, heightAt bilinear sampling + clamping, lake basin, tree
// placement rules) and city (WFC layout validity incl. border tag, block->building mapping,
// road slabs, district plates) — plus determinism per seed and mode dispatch.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/worldgen.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

using namespace fuhglgraphics::procedural;

WorldGenParams landscapeParams() {
    WorldGenParams p;
    p.mode = WorldGenParams::Mode::Landscape;
    p.seed = 42u;
    p.worldSize = 120.f;
    p.terrainGrid = 65;
    p.terrainHeight = 12.f;
    p.waterLevel = 0.35f;
    p.treeCount = 40;
    p.bushCount = 25;
    p.treeMinDist = 5.f;
    return p;
}

WorldGenParams cityParams() {
    WorldGenParams p;
    p.mode = WorldGenParams::Mode::City;
    p.seed = 777u;
    p.cityGrid = 10;
    p.cellSize = 8.f;
    p.buildingHeight = 6.f;
    p.blocksPerDistrict = 24;
    p.parkBushesPerPlaza = 3;
    return p;
}

void testLandscapeHeightmap() {
    const WorldGenParams p = landscapeParams();
    const GeneratedWorld w = generateWorld(p);
    CHECK(w.mode == WorldGenParams::Mode::Landscape);
    CHECK(w.seed == p.seed);
    CHECK(w.gridW == p.terrainGrid && w.gridH == p.terrainGrid);
    CHECK(w.heightmap.size() == static_cast<std::size_t>(p.terrainGrid) * p.terrainGrid);
    CHECK(w.sizeX == p.worldSize && w.heightScale == p.terrainHeight);
    CHECK(w.waterLevelWorld > 0.f);
    for (const float h : w.heightmap) {
        CHECK(h >= 0.f && h <= 1.f);
    }
    // island falloff: the border ring lands on the shore (slightly above the water level
    // when a lake is carved, near zero otherwise); erosion may deposit a little sediment there
    const int g = w.gridW;
    const float borderMax = p.waterLevel > 0.f ? p.waterLevel * 1.05f + 0.15f : 0.2f;
    for (int i = 0; i < g; ++i) {
        CHECK(w.heightmap[i] <= borderMax);
        CHECK(w.heightmap[i + g * (g - 1)] <= borderMax);
        CHECK(w.heightmap[g * i] <= borderMax);
        CHECK(w.heightmap[g * i + g - 1] <= borderMax);
    }
    // lake basin: with waterLevel > 0 some samples end below the water level
    float minH = 1.f;
    for (const float h : w.heightmap) minH = std::min(minH, h);
    CHECK(minH < p.waterLevel);
    // splatmap + flowmap exist and are in range
    CHECK(w.splatmap.size() == static_cast<std::size_t>(g) * g * 4);
    CHECK(w.flowmap.size() == static_cast<std::size_t>(g) * g);
    int snowPixels = 0, rockPixels = 0;
    for (std::size_t i = 0; i < w.splatmap.size(); i += 4) {
        if (w.splatmap[i + 3] > 200) ++snowPixels; // A = snow
        if (w.splatmap[i + 1] > 150) ++rockPixels; // G = rock
    }
    CHECK(snowPixels > 0);  // high peaks are snow-covered
    CHECK(rockPixels > 0);  // steep slopes are rock
}

void testHeightAt() {
    WorldGenParams p = landscapeParams();
    p.waterLevel = 0.f;
    const GeneratedWorld w = generateWorld(p);
    // grid-node sample: heightAt at the exact node position equals sample * heightScale
    const float x = -w.sizeX * 0.5f + 10.f * (w.sizeX / (w.gridW - 1)); // node x=10
    const float z = -w.sizeZ * 0.5f + 20.f * (w.sizeZ / (w.gridH - 1)); // node z=20
    const float expect = w.heightmap[10 + w.gridW * 20] * w.heightScale;
    CHECK_NEAR(w.heightAt(x, z), expect, 1e-3f);
    // clamping: far outside the rect clamps to the edge column/row, never NaN
    CHECK_NEAR(w.heightAt(-1e6f, 0.f), w.heightAt(-w.sizeX, 0.f), 1e-3f);
    CHECK_NEAR(w.heightAt(1e6f, 1e6f), w.heightAt(w.sizeX, w.sizeZ), 1e-3f);
    CHECK(std::isfinite(w.heightAt(0.f, 0.f)));
}

void testErosionAndScatter() {
    WorldGenParams p = landscapeParams();
    p.erosionDroplets = 1500;
    p.thermalPasses = 3;
    const GeneratedWorld w = generateWorld(p);
    // heightmap still valid after erosion
    for (const float h : w.heightmap) CHECK(h >= 0.f && h <= 1.f);
    // flow map was produced and normalized
    float maxFlow = 0.f;
    for (const float f : w.flowmap) maxFlow = std::max(maxFlow, f);
    CHECK(maxFlow > 0.f && maxFlow <= 1.f + 1e-6f);
    // poisson scatter: min distance respected among trees
    for (std::size_t i = 0; i < w.trees.size(); ++i)
        for (std::size_t j = i + 1; j < w.trees.size(); ++j) {
            const float dx = w.trees[i].position.x - w.trees[j].position.x;
            const float dz = w.trees[i].position.z - w.trees[j].position.z;
            CHECK(dx * dx + dz * dz >= p.treeMinDist * p.treeMinDist * 0.9f);
        }
    // rocks scattered and on the ground
    CHECK(!w.rocks.empty());
    for (const auto& r : w.rocks) {
        CHECK(r.mesh.vertexCount() > 0);
        CHECK_NEAR(r.position.y, w.heightAt(r.position.x, r.position.z) + 0.1f, 0.5f);
    }
    // determinism with erosion
    const GeneratedWorld w2 = generateWorld(p);
    CHECK(w2.heightmap == w.heightmap);
    CHECK(w2.splatmap == w.splatmap);
}

void testLandscapeTrees() {
    const WorldGenParams p = landscapeParams();
    const GeneratedWorld w = generateWorld(p);
    CHECK(!w.trees.empty());
    CHECK(w.trees.size() <= static_cast<std::size_t>(p.treeCount));
    CHECK(!w.bushes.empty());
    CHECK(w.bushes.size() <= static_cast<std::size_t>(p.bushCount));
    for (const auto& b : w.bushes) {
        CHECK(b.mesh.vertexCount() > 0);
        CHECK_NEAR(b.position.y, w.heightAt(b.position.x, b.position.z), 1e-2f);
        CHECK(b.position.y >= w.waterLevelWorld);
    }
    const float half = p.worldSize * 0.5f;
    for (const auto& t : w.trees) {
        CHECK(t.mesh.vertexCount() > 0);
        CHECK(t.mesh.indices.size() >= 3);
        CHECK(t.position.x >= -half && t.position.x <= half);
        CHECK(t.position.z >= -half && t.position.z <= half);
        // trees stand on the terrain and not in the lake
        CHECK_NEAR(t.position.y, w.heightAt(t.position.x, t.position.z), 1e-2f);
        CHECK(t.position.y >= w.waterLevelWorld);
        CHECK(t.scale > 0.f);
    }
    // min distance respected
    for (std::size_t i = 0; i < w.trees.size(); ++i)
        for (std::size_t j = i + 1; j < w.trees.size(); ++j) {
            const float dx = w.trees[i].position.x - w.trees[j].position.x;
            const float dz = w.trees[i].position.z - w.trees[j].position.z;
            CHECK(dx * dx + dz * dz >= p.treeMinDist * p.treeMinDist * 0.99f);
        }
}

void testCityLayout() {
    const WorldGenParams p = cityParams();
    const GeneratedWorld w = generateWorld(p);
    CHECK(w.mode == WorldGenParams::Mode::City);
    CHECK(w.cells.size() == static_cast<std::size_t>(p.cityGrid) * p.cityGrid);
    CHECK(w.cityW == p.cityGrid && w.cityCellSize == p.cellSize);
    CHECK(!w.tiles.empty());
    CHECK(w.cityGround.vertexCount() > 0);
    CHECK(w.roads.vertexCount() > 0);
    CHECK(w.districts.vertexCount() > 0); // blocksPerDistrict > 0

    // every solved cell references a valid tile; adjacent edge tags match (incl. border "s")
    int solved = 0, blocks = 0, streets = 0;
    const auto tag = [&](int x, int z, int dir) -> std::string {
        if (x < 0 || x >= w.cityW || z < 0 || z >= w.cityH) return "s";
        const WfcCell& c = w.cells[x + w.cityW * z];
        if (c.tile < 0) return "";
        return wfcRotatedTags(w.tiles[c.tile], c.rot)[dir];
    };
    for (int z = 0; z < w.cityH; ++z)
        for (int x = 0; x < w.cityW; ++x) {
            const WfcCell& c = w.cells[x + w.cityW * z];
            if (c.tile < 0) continue;
            ++solved;
            CHECK(c.tile >= 0 && c.tile < static_cast<int>(w.tiles.size()));
            CHECK(c.rot >= 0 && c.rot < 4);
            const std::string& n = w.tiles[c.tile].name;
            if (n == "block") ++blocks;
            else if (n != "plaza") ++streets;
            if (x + 1 < w.cityW) {
                const std::string e = tag(x, z, 0), ww = tag(x + 1, z, 2);
                if (!e.empty() && !ww.empty()) CHECK(e == ww);
            }
            if (z + 1 < w.cityH) {
                const std::string s = tag(x, z, 1), nn = tag(x, z + 1, 3);
                if (!s.empty() && !nn.empty()) CHECK(s == nn);
            }
        }
    CHECK(solved == p.cityGrid * p.cityGrid); // city tile set is always solvable
    CHECK(blocks > 0);
    CHECK(streets > 0);
    // border ring carries the border tag "s" on its outward edges
    for (int x = 0; x < w.cityW; ++x) {
        CHECK(tag(x, 0, 3) == "s" || tag(x, 0, 3).empty());
        CHECK(tag(x, w.cityH - 1, 1) == "s" || tag(x, w.cityH - 1, 1).empty());
    }
    for (int z = 0; z < w.cityH; ++z) {
        CHECK(tag(0, z, 2) == "s" || tag(0, z, 2).empty());
        CHECK(tag(w.cityW - 1, z, 0) == "s" || tag(w.cityW - 1, z, 0).empty());
    }
    // houses on non-park block cells; park cells (plaza tiles + converted interior blocks)
    // carry lawn + bushes instead
    int plazaTiles = 0;
    for (const auto& c : w.cells)
        if (c.tile >= 0 && w.tiles[c.tile].name == "plaza") ++plazaTiles;
    CHECK(w.parkCellCount >= plazaTiles);
    CHECK(w.buildings.size() + static_cast<std::size_t>(w.parkCellCount) ==
          static_cast<std::size_t>(blocks));
    std::set<int> roofTypes;
    for (const auto& b : w.buildings) {
        CHECK(b.mesh.vertexCount() >= 24);      // walls box (highrise may add a setback box)
        CHECK(b.detailMesh.vertexCount() > 0);  // door/windows/chimney
        CHECK(b.detailMesh.indices.size() >= 3);
        CHECK(b.roofMesh.vertexCount() > 0);    // roof
        CHECK(b.roofType >= 0 && b.roofType <= 2);
        roofTypes.insert(b.roofType);
        const float gx = (b.position.x / p.cellSize) + (p.cityGrid - 1) * 0.5f;
        const float gz = (b.position.z / p.cellSize) + (p.cityGrid - 1) * 0.5f;
        CHECK_NEAR(gx, std::round(gx), 1e-3f);
        CHECK_NEAR(gz, std::round(gz), 1e-3f);
    }
    // park greenery: bushes on every park cell
    CHECK(w.bushes.size() == static_cast<std::size_t>(w.parkCellCount) * p.parkBushesPerPlaza);
    CHECK(w.parkCellCount > 0); // parkFraction guarantees parks for interior blocks
    CHECK(!roofTypes.empty());
}

// average wall height (max y of the walls mesh) of the generated houses
float avgWallHeight(WorldGenParams p) {
    const GeneratedWorld w = generateWorld(p);
    double sum = 0;
    for (const auto& b : w.buildings) {
        float maxY = 0.f;
        for (std::size_t i = 0; i < b.mesh.vertexCount(); ++i)
            maxY = std::max(maxY, b.mesh.vertices[i * 11 + 1]);
        sum += maxY;
    }
    return w.buildings.empty() ? 0.f : static_cast<float>(sum / w.buildings.size());
}

void testHouseStyles() {
    WorldGenParams base = cityParams();
    base.houseStyle = HouseStyle::Village;
    const float villageH = avgWallHeight(base);
    base.houseStyle = HouseStyle::Town;
    const float townH = avgWallHeight(base);
    base.houseStyle = HouseStyle::Highrise;
    const GeneratedWorld hr = generateWorld(base);
    const float highriseH = avgWallHeight(base);
    CHECK(villageH > 0.f && townH > 0.f && highriseH > 0.f);
    CHECK(villageH < townH);      // village houses are lower than town houses
    CHECK(highriseH > townH * 2); // highrise towers over town houses
    for (const auto& b : hr.buildings) { // highrise: flat roofs + window grid details
        CHECK(b.roofType == static_cast<int>(RoofType::Flat));
        CHECK(b.detailMesh.vertexCount() > 24);
    }
    base.houseStyle = HouseStyle::Town;
    const GeneratedWorld town = generateWorld(base);
    bool anyGable = false, anyShed = false;
    for (const auto& b : town.buildings) {
        if (b.roofType == 0) anyGable = true; // Satteldach
        if (b.roofType == 1) anyShed = true;  // Schleppdach
    }
    CHECK(anyGable);
    CHECK(anyShed); // both roof variants occur with seed 777
}

void testCityTilesSolvable() {
    // the city tile set alone (default params) always solves a 12x12 grid
    Wfc wfc(12, 12, "s");
    for (const auto& t : makeCityTiles()) wfc.addTile(t);
    const auto cells = wfc.solve(1u);
    for (const auto& c : cells) CHECK(c.tile >= 0);
}

void testDeterminismAndModes() {
    const GeneratedWorld a = generateWorld(landscapeParams());
    const GeneratedWorld b = generateWorld(landscapeParams());
    CHECK(a.heightmap == b.heightmap);
    CHECK(a.trees.size() == b.trees.size());
    for (std::size_t i = 0; i < a.trees.size(); ++i)
        CHECK(a.trees[i].position.x == b.trees[i].position.x &&
              a.trees[i].mesh.vertices == b.trees[i].mesh.vertices);

    const GeneratedWorld c1 = generateWorld(cityParams());
    const GeneratedWorld c2 = generateWorld(cityParams());
    CHECK(c1.cells.size() == c2.cells.size());
    for (std::size_t i = 0; i < c1.cells.size(); ++i)
        CHECK(c1.cells[i].tile == c2.cells[i].tile && c1.cells[i].rot == c2.cells[i].rot);
    CHECK(c1.buildings.size() == c2.buildings.size());

    // different seed -> different landscape
    WorldGenParams p = landscapeParams();
    p.seed = 43u;
    const GeneratedWorld other = generateWorld(p);
    CHECK(other.heightmap != a.heightmap);

    // districts off -> empty mesh
    WorldGenParams cp = cityParams();
    cp.blocksPerDistrict = 0;
    CHECK(generateWorld(cp).districts.vertexCount() == 0);
}

} // namespace

void runProceduralWorldgenTests() {
    testLandscapeHeightmap();
    testHeightAt();
    testErosionAndScatter();
    testLandscapeTrees();
    testCityLayout();
    testHouseStyles();
    testCityTilesSolvable();
    testDeterminismAndModes();
}
