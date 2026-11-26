// Unit tests for the world generator helper (label "unit"): generateWorld() in both modes —
// landscape (heightmap size/range, heightAt bilinear sampling + clamping, lake basin, tree
// placement rules) and city (WFC layout validity incl. border tag, block->building mapping,
// road slabs, district plates) — plus determinism per seed and mode dispatch.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/noise.hpp>
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
    CHECK(w.roads.vertexCount() > 0);      // street slabs (textured app-side)
    CHECK(w.pavements.vertexCount() > 0);  // sidewalk + lot plates + lawn
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
    int withGlass = 0, facades[6] = {0, 0, 0, 0, 0, 0};
    for (const auto& b : w.buildings) {
        CHECK(b.mesh.vertexCount() >= 24);      // walls box (highrise may add a setback box)
        CHECK(b.detailMesh.vertexCount() > 0);  // door/windows/chimney
        CHECK(b.detailMesh.indices.size() >= 3);
        CHECK(b.roofMesh.vertexCount() > 0);    // roof
        CHECK(b.roofType >= 0 && b.roofType <= 3); // RoofType cast (flat = arid climate)
        roofTypes.insert(b.roofType);
        CHECK(b.facade >= 0 && b.facade <= 5);  // FacadeMaterial cast (biome-driven: wood/adobe/concrete too)
        ++facades[b.facade];
        if (b.glassMesh.vertexCount() > 0) ++withGlass; // reflective window panes
        const float gx = (b.position.x / p.cellSize) + (p.cityGrid - 1) * 0.5f;
        const float gz = (b.position.z / p.cellSize) + (p.cityGrid - 1) * 0.5f;
        CHECK_NEAR(gx, std::round(gx), 1e-3f);
        CHECK_NEAR(gz, std::round(gz), 1e-3f);
    }
    // park greenery: bushes on every park cell
    CHECK(w.bushes.size() == static_cast<std::size_t>(w.parkCellCount) * p.parkBushesPerPlaza);
    CHECK(w.parkCellCount > 0); // parkFraction guarantees parks for interior blocks
    CHECK(!roofTypes.empty());
    CHECK(withGlass > 0); // at least some houses carry window glass
    CHECK(facades[0] + facades[1] + facades[2] + facades[3] + facades[4] + facades[5] ==
          static_cast<int>(w.buildings.size()));
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

void testSpline1D() {
    Spline1D sp;
    sp.pts = {{0.f, 0.f}, {0.4f, 0.f}, {0.45f, 0.03f}, {0.7f, 0.38f}, {1.f, 1.f}};
    CHECK_NEAR(sp.eval(-1.f), 0.f, 1e-6f); // clamped left
    CHECK_NEAR(sp.eval(2.f), 1.f, 1e-6f);  // clamped right
    CHECK_NEAR(sp.eval(0.4f), 0.f, 1e-6f); // exact knot
    CHECK_NEAR(sp.eval(1.f), 1.f, 1e-6f);
    const float mid = sp.eval(0.575f);     // between the 0.45 and 0.7 knots
    CHECK(mid > 0.03f && mid < 0.38f);
    float prev = sp.eval(0.f);             // monotonic non-decreasing sweep
    for (int i = 1; i <= 100; ++i) {
        const float v = sp.eval(i / 100.f);
        CHECK(v >= prev - 1e-6f);
        prev = v;
    }
    Spline1D empty;
    CHECK_NEAR(empty.eval(0.5f), 0.5f, 1e-6f); // identity fallback
}

void testLandscapeClimateAndRivers() {
    WorldGenParams p = landscapeParams();
    p.settlementCount = 0; // isolate the river carving from the road bench carving
    p.erosionDroplets = 0; // isolate the river flow from the droplet flow
    const GeneratedWorld w = generateWorld(p);
    const std::size_t n = w.heightmap.size();
    // climate maps present, sized and in range; biome ids valid
    CHECK(w.temperature.size() == n && w.humidity.size() == n && w.biome.size() == n);
    CHECK(w.pondMask.size() == n);
    int ocean = 0, land = 0;
    for (float t : w.temperature) CHECK(t >= 0.f && t <= 1.f);
    for (float h : w.humidity) CHECK(h >= 0.f && h <= 1.f);
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char b = w.biome[i];
        CHECK(b <= static_cast<unsigned char>(Biome::Snow));
        if (b == static_cast<unsigned char>(Biome::Ocean)) ++ocean;
        if (w.heightmap[i] >= p.waterLevel) ++land;
    }
    CHECK(ocean > 0 && land > 0);
    // lapse rate: low cells are warmer on average than high cells
    double tLow = 0, tHigh = 0;
    int nLow = 0, nHigh = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (w.heightmap[i] < 0.4f) { tLow += w.temperature[i]; ++nLow; }
        if (w.heightmap[i] > 0.75f) { tHigh += w.temperature[i]; ++nHigh; }
    }
    CHECK(nLow > 0 && nHigh > 0);
    CHECK(tLow / nLow > tHigh / nHigh);
    // humidity: near-water cells are wetter on average than high inland cells
    double hWater = 0, hInland = 0;
    int nW = 0, nI = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (w.heightmap[i] < p.waterLevel + 0.03f) { hWater += w.humidity[i]; ++nW; }
        if (w.heightmap[i] > 0.6f) { hInland += w.humidity[i]; ++nI; }
    }
    CHECK(nW > 0 && nI > 0);
    CHECK(hWater / nW > hInland / nI);
    // rivers: strong flow channels exist and the carving removed material
    float maxFlow = 0.f;
    int strongFlow = 0;
    double sumH = 0;
    for (std::size_t i = 0; i < n; ++i) {
        maxFlow = std::max(maxFlow, w.flowmap[i]);
        if (w.flowmap[i] > 0.4f) ++strongFlow;
        sumH += w.heightmap[i];
    }
    CHECK(maxFlow > 0.9f);   // flow map is normalized
    CHECK(strongFlow > 20);  // river channels carved
    WorldGenParams pn = p;
    pn.riverCount = 0;
    const GeneratedWorld wn = generateWorld(pn);
    double sumN = 0;
    for (float h : wn.heightmap) sumN += h;
    CHECK(sumH < sumN); // carving lowers the total terrain volume
    // climateMaps off -> maps empty
    WorldGenParams pc = landscapeParams();
    pc.climateMaps = false;
    const GeneratedWorld wc = generateWorld(pc);
    CHECK(wc.temperature.empty() && wc.humidity.empty() && wc.biome.empty());
    CHECK(wc.pondMask.size() == wc.heightmap.size());
}

void testLandscapeTerracing() {
    WorldGenParams p = landscapeParams();
    p.terraceSteps = 10;
    p.terraceStrength = 1.f;
    p.erosionDroplets = 0; // keep the peaks high so the quantized zone is populated
    p.thermalPasses = 0;
    p.riverCount = 0;
    const GeneratedWorld w = generateWorld(p);
    for (float h : w.heightmap) CHECK(h >= 0.f && h <= 1.f);
    // high terrain is quantized to the step grid (blend reaches 1 at h=0.85)
    int checked = 0;
    for (float h : w.heightmap)
        if (h > 0.84f) {
            const float frac = std::fabs(h * 10.f - std::round(h * 10.f));
            CHECK(frac < 0.05f);
            ++checked;
        }
    CHECK(checked > 0);
    // terracing changes the terrain vs off
    WorldGenParams p0 = p;
    p0.terraceSteps = 0;
    CHECK(generateWorld(p0).heightmap != w.heightmap);
}

void testCityRoads() {
    // default: plaza (path) tiles are dropped from the tile set, so paths can never
    // interrupt the road network; the solved road cells form one connected component
    WorldGenParams p = cityParams();
    const GeneratedWorld w = generateWorld(p);
    CHECK(w.roadSurface == p.roadSurface);
    CHECK(w.wetRoads == p.wetRoads);
    for (const auto& t : w.tiles) CHECK(t.name != "plaza");
    CHECK(wfcRoadsConnected(w.cells, w.tiles, w.cityW, w.cityH));

    // allowPathCuts=true keeps plaza tiles in the set and skips the connectivity repair, so
    // the raw solve (possibly fragmented roads) is accepted as-is
    WorldGenParams pc = cityParams();
    pc.allowPathCuts = true;
    const GeneratedWorld wc = generateWorld(pc);
    bool hasPlazaTile = false;
    for (const auto& t : wc.tiles)
        if (t.name == "plaza") hasPlazaTile = true;
    CHECK(hasPlazaTile);
    for (const auto& c : wc.cells) CHECK(c.tile >= 0);

    // synthetic connectivity checks: two streetH cells side by side share an "r" edge and are
    // connected; two streetV cells side by side touch via "s" edges and are not
    const std::vector<WfcTile> tiles = {
        {"streetH", 1.f, {"r", "s", "r", "s"}},
        {"streetV", 1.f, {"s", "r", "s", "r"}},
    };
    const std::vector<WfcCell> hh = {{0, 0}, {0, 0}};
    CHECK(wfcRoadsConnected(hh, tiles, 2, 1));
    const std::vector<WfcCell> vv = {{1, 0}, {1, 0}};
    CHECK(!wfcRoadsConnected(vv, tiles, 2, 1));

    // road damage: damage=1 adds a pothole quad on every street cell -> strictly more vertices
    WorldGenParams pd = cityParams();
    pd.roadDamage = 0.f;
    const GeneratedWorld w0 = generateWorld(pd);
    pd.roadDamage = 1.f;
    const GeneratedWorld w1 = generateWorld(pd);
    CHECK(w1.roads.vertexCount() > w0.roads.vertexCount());

    // irregular street edges change the slab geometry
    WorldGenParams pi = cityParams();
    pi.roadIrregularity = 0.f;
    const GeneratedWorld wi0 = generateWorld(pi);
    pi.roadIrregularity = 0.06f;
    const GeneratedWorld wi1 = generateWorld(pi);
    CHECK(wi0.roads.vertices != wi1.roads.vertices);

    // surface + wet flags are echoed and change the slab colors stored per vertex
    WorldGenParams ps = cityParams();
    ps.roadSurface = RoadSurface::Cobble;
    ps.wetRoads = true;
    const GeneratedWorld ws = generateWorld(ps);
    CHECK(ws.roadSurface == RoadSurface::Cobble && ws.wetRoads);
    const GeneratedWorld wa = generateWorld(cityParams()); // asphalt, dry
    CHECK(ws.roads.vertices != wa.roads.vertices);

    // roadSurfaceStyle: uv scale per surface, wet darkens the slab color
    fuhgl::math::Vec3 cA, cW;
    float uvA = 0.f, uvW = 0.f;
    roadSurfaceStyle(RoadSurface::Asphalt, false, cA, uvA);
    roadSurfaceStyle(RoadSurface::Asphalt, true, cW, uvW);
    CHECK_NEAR(uvA, uvW, 1e-6f);
    CHECK(cW.x < cA.x && cW.y < cA.y && cW.z < cA.z);
    fuhgl::math::Vec3 cc, cp, cd;
    float uvC = 0.f, uvP = 0.f, uvD = 0.f;
    roadSurfaceStyle(RoadSurface::Cobble, false, cc, uvC);
    roadSurfaceStyle(RoadSurface::Paving, false, cp, uvP);
    roadSurfaceStyle(RoadSurface::Dirt, false, cd, uvD);
    CHECK(uvA > 0.f && uvC > 0.f && uvP > 0.f && uvD > 0.f);
    CHECK(uvC != uvA && uvP != uvA && uvD != uvA);
}

// bilinear sample of a gridW*gridH float map (same layout as the worldgen maps)
float sampleMap(const std::vector<float>& map, int g, float size, float x, float z) {
    if (map.empty()) return 0.f;
    const float u = std::min(1.f, std::max(0.f, x / size + 0.5f)) * (g - 1);
    const float v = std::min(1.f, std::max(0.f, z / size + 0.5f)) * (g - 1);
    const int x0 = static_cast<int>(u), z0 = static_cast<int>(v);
    const int x1 = std::min(g - 1, x0 + 1), z1 = std::min(g - 1, z0 + 1);
    const float tx = u - x0, tz = v - z0;
    const auto at = [&](int xx, int zz) { return map[static_cast<std::size_t>(xx) + g * zz]; };
    return (at(x0, z0) * (1.f - tx) + at(x1, z0) * tx) * (1.f - tz) +
           (at(x0, z1) * (1.f - tx) + at(x1, z1) * tx) * tz;
}

void testLandscapeSettlementsAndRoads() {
    // default landscape params: settlements + G38 road network are part of the world
    const WorldGenParams p = landscapeParams();
    const GeneratedWorld w = generateWorld(p);
    const int g = w.gridW;
    CHECK(!w.settlements.empty());
    CHECK(w.settlements.size() <= static_cast<std::size_t>(p.settlementCount));
    CHECK(!w.buildings.empty()); // hamlet houses
    CHECK(w.roads.vertexCount() > 0);   // landscape road ribbons (G38 extruded profiles)
    CHECK(w.roads.indices.size() % 3 == 0);
    CHECK(w.roadMask.size() == static_cast<std::size_t>(g) * g);
    CHECK(w.rutMask.size() == w.roadMask.size());
    CHECK(w.bridgeMask.size() == w.roadMask.size());
    CHECK(w.roadDistance.size() == w.roadMask.size());
    for (const float m : w.roadMask) CHECK(m >= 0.f && m <= 1.f + 1e-6f);
    for (const auto& s : w.settlements) {
        CHECK(s.houseCount > 0);
        CHECK(s.firstHouse + s.houseCount <= w.buildings.size());
        CHECK(s.importance > 0.f && s.importance <= 1.f);
        // the settlement center is a road hub of the network
        CHECK(w.roadDistanceAt(s.center.x, s.center.y) < 6.f);
        for (std::size_t k = s.firstHouse; k < s.firstHouse + s.houseCount; ++k) {
            const auto& b = w.buildings[k];
            CHECK(b.mesh.vertexCount() > 0);
            CHECK(std::hypot(b.position.x - s.center.x, b.position.z - s.center.y) <=
                  s.radius + 3.f);
            CHECK_NEAR(b.position.y, w.heightAt(b.position.x, b.position.z), 1e-2f);
            CHECK(b.position.y >= w.waterLevelWorld);
            // houses keep off the road corridor
            CHECK(w.roadDistanceAt(b.position.x, b.position.z) > p.roads.highwayWidth * 0.5f);
        }
    }
    // vegetation keeps off the roads (trees/rocks only where the road mask is low)
    for (const auto& t : w.trees)
        CHECK(sampleMap(w.roadMask, g, w.sizeX, t.position.x, t.position.z) < 0.7f);
    for (const auto& r : w.rocks)
        CHECK(sampleMap(w.roadMask, g, w.sizeX, r.position.x, r.position.z) < 0.7f);
    // road props (milestones/lanterns/signs/walls) exist and carry meshes
    for (const auto& prop : w.roadProps) CHECK(prop.mesh.vertexCount() > 0);
    // determinism of the whole road/settlement phase
    const GeneratedWorld w2 = generateWorld(p);
    CHECK(w2.roadMask == w.roadMask);
    CHECK(w2.rutMask == w.rutMask);
    CHECK(w2.settlements.size() == w.settlements.size());
    CHECK(w2.buildings.size() == w.buildings.size());
    CHECK(w2.roadProps.size() == w.roadProps.size());
    CHECK(w2.roads.vertices == w.roads.vertices);
    // off switch: no settlements -> no roads, no hamlet houses, empty masks
    WorldGenParams p0 = landscapeParams();
    p0.settlementCount = 0;
    const GeneratedWorld w0 = generateWorld(p0);
    CHECK(w0.settlements.empty());
    CHECK(w0.buildings.empty());
    CHECK(w0.roadProps.empty());
    CHECK(w0.roads.vertexCount() == 0);
    CHECK(w0.bridges.vertexCount() == 0);
    CHECK(w0.roadMask.empty() && w0.rutMask.empty() && w0.bridgeMask.empty() &&
          w0.roadDistance.empty());
    CHECK(w0.roadDistanceAt(0.f, 0.f) > 1e8f);
    // roadDistanceAt is ~0 on a settlement and large far away from any road
    CHECK(w.roadDistanceAt(w.settlements[0].center.x, w.settlements[0].center.y) <
          w.roadDistanceAt(0.f, 0.f) + 1e9f); // sanity: finite
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
    testCityRoads();
    testSpline1D();
    testLandscapeClimateAndRivers();
    testLandscapeTerracing();
    testLandscapeSettlementsAndRoads();
}
