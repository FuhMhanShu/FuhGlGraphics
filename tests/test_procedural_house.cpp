// Unit tests for the class-based house generator (label "unit"): mesh validity (indices in
// range, finite unit normals), determinism per seed, variant richness (many distinct houses
// across seeds), roof-type coverage, feature flags (bay/dormer/annex/porch/chimney/timber
// frame), style height ordering (village < town < highrise), the interior pipeline (BSP
// floorplan, room types, spanning-tree door connectivity, exterior-only windows with min
// distances, wall shell with real openings, CSG slabs/frames/leaves/furniture), and the
// BuildingContext rules: climate -> roof pitch/overhang/gutter/flat+parapet/window
// size/shutters/chimney, flood zone -> stilts, local resources/biome -> facade material,
// density -> storeys/shop front/fence, wealth -> patches/cornice/wall jitter.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/house.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace fuhglgraphics::procedural;

void checkMeshValid(const fuhgl::render::MeshData& md) {
    CHECK(md.vertexCount() > 0);
    CHECK(md.indices.size() >= 3);
    CHECK(md.indices.size() % 3 == 0);
    for (const std::uint32_t idx : md.indices) CHECK(idx < md.vertexCount());
    for (std::size_t i = 0; i < md.vertexCount(); ++i) {
        const float* v = &md.vertices[i * 11];
        for (int k = 0; k < 11; ++k) CHECK(std::isfinite(v[k]));
        const float nl = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
        CHECK_NEAR(nl, 1.f, 1e-3f); // unit normals (winding-derived)
    }
}

std::string specSignature(const HouseSpec& s) {
    return std::to_string(static_cast<int>(s.roofType)) + "/" + std::to_string(s.floors) + "/" +
           std::to_string(static_cast<int>(s.facade)) + "/" +
           (s.hasBay ? "B" : "") + (s.hasAnnex ? "A" : "") + (s.hasPorch ? "P" : "") +
           (s.hasDormers ? "D" : "") + (s.hasChimney ? "C" : "") + (s.timberFrame ? "F" : "") +
           (s.hasGutter ? "G" : "") + (s.hasShutters ? "S" : "") + (s.hasGarage ? "X" : "") +
           (s.hasBalcony ? "K" : "") + (s.wallIrregular ? "I" : "") + (s.hasParapet ? "p" : "") +
           (s.onStilts ? "T" : "") + (s.hasShopFront ? "L" : "") + (s.hasFence ? "Z" : "") +
           (s.hasPatches ? "W" : "") + (s.hasCornice ? "O" : "") +
           (s.beveled ? "b" : "") + (s.warped ? "v" : "") + (s.hasPlinth ? "s" : "") +
           (s.hasLintels ? "t" : "") + (s.hasOverhang ? "u" : "") + (s.hasSolar ? "y" : "") +
           (s.hasRoofWindow ? "f" : "") + (s.hasIvy ? "e" : "") + std::to_string(s.props) +
           (s.hasInterior ? "I" : "") + std::to_string(s.roomCount) + "/" +
           std::to_string(s.doorCount) + "/" + std::to_string(s.windowCount);
}

void testValidityAndDeterminism() {
    HouseGenerator gen;
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 42u;
    const HouseMesh a = gen.generate(p);
    checkMeshValid(a.walls);
    checkMeshValid(a.details);
    checkMeshValid(a.roof);
    CHECK(a.spec.wallHeight > 0.f);
    CHECK(a.spec.width == p.width && a.spec.depth == p.depth && a.spec.floors == p.floors);
    const HouseMesh b = gen.generate(p);
    CHECK(a.walls.vertices == b.walls.vertices && a.walls.indices == b.walls.indices);
    CHECK(a.details.vertices == b.details.vertices);
    CHECK(a.roof.vertices == b.roof.vertices);
    CHECK(a.glass.vertices == b.glass.vertices);
    CHECK(a.interior.vertices == b.interior.vertices);
    CHECK(a.doors.vertices == b.doors.vertices);
    CHECK(specSignature(a.spec) == specSignature(b.spec));
    CHECK(a.glass.vertexCount() > 0); // window glass panes exist for reflective material
    CHECK(a.spec.wallIrregular);      // default wallIrregularity > 0 -> flag set (jitter on interior mesh)
    // interior pipeline on by default for Town: real floorplan + shell + interior + door leaves
    CHECK(a.spec.hasInterior);
    CHECK(a.spec.roomCount >= 2 && a.spec.doorCount >= a.spec.roomCount && a.spec.windowCount > 0);
    checkMeshValid(a.interior);
    CHECK(a.doors.vertexCount() > 0);
    CHECK(a.floorplan.rooms.size() == static_cast<std::size_t>(a.spec.roomCount));
}

void testVariantRichness() {
    HouseGenerator gen;
    std::set<std::string> sigs;
    std::set<int> roofTypes;
    int bays = 0, dormers = 0, annexes = 0, porches = 0, chimneys = 0, timber = 0;
    int gutters = 0, shutters = 0, garages = 0, balconies = 0;
    std::set<int> facades;
    for (unsigned seed = 1; seed <= 40; ++seed) {
        HouseGenerator::Params p = HouseGenerator::randomParams(HouseStyle::Village, 8.f, seed);
        p.seed = seed * 7u + 1u;
        p.interior = false; // facade-variant richness (bay/dormer/...) on the hollow shell
        const HouseMesh hm = gen.generate(p);
        checkMeshValid(hm.walls);
        checkMeshValid(hm.details);
        checkMeshValid(hm.roof);
        if (hm.glass.vertexCount() > 0) checkMeshValid(hm.glass);
        sigs.insert(specSignature(hm.spec) + std::to_string(hm.walls.vertexCount()));
        roofTypes.insert(static_cast<int>(hm.spec.roofType));
        facades.insert(static_cast<int>(hm.spec.facade));
        if (hm.spec.hasBay) ++bays;
        if (hm.spec.hasDormers) ++dormers;
        if (hm.spec.hasAnnex) ++annexes;
        if (hm.spec.hasPorch) ++porches;
        if (hm.spec.hasChimney) ++chimneys;
        if (hm.spec.timberFrame) ++timber;
        if (hm.spec.hasGutter) ++gutters;
        if (hm.spec.hasShutters) ++shutters;
        if (hm.spec.hasGarage) ++garages;
        if (hm.spec.hasBalcony) ++balconies;
        CHECK(hm.glass.vertexCount() > 0); // every windowed house has glass panes
    }
    CHECK(sigs.size() >= 12);   // many distinct houses across seeds
    CHECK(roofTypes.size() >= 3); // gable, shed and hipped all occur
    CHECK(facades.size() == 3);   // brick, plaster and stone all occur
    CHECK(bays > 0 && dormers > 0 && annexes > 0 && porches > 0 && chimneys > 0 && timber > 0);
    CHECK(gutters > 0 && shutters > 0 && garages > 0 && balconies > 0); // new features occur
}

void testWallJitterAndFacade() {
    HouseGenerator gen;
    // flat walls when wallIrregularity == 0 (hollow-shell mode: the interior pipeline keeps the
    // shell watertight and jitters the interior mesh instead)
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 77u;
    p.interior = false;
    p.wallIrregularity = 0.f;
    const HouseMesh flat = gen.generate(p);
    CHECK(!flat.spec.wallIrregular);
    // jittered walls: same box has vertices at varying x/z
    p.wallIrregularity = 0.02f;
    const HouseMesh rough = gen.generate(p);
    CHECK(rough.spec.wallIrregular);
    float minX = 1e9f, maxX = -1e9f;
    for (std::size_t i = 0; i < rough.walls.vertexCount(); ++i) {
        const float x = rough.walls.vertices[i * 11];
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
    }
    CHECK(maxX - minX > p.width - 0.01f); // jitter widens the silhouette slightly
    // stone facade adds relief courses to details; brick/plaster do not
    p.facade = FacadeMaterial::Stone;
    const HouseMesh stone = gen.generate(p);
    p.facade = FacadeMaterial::Brick;
    const HouseMesh brick = gen.generate(p);
    CHECK(stone.details.vertexCount() > brick.details.vertexCount());
    CHECK(stone.spec.facade == FacadeMaterial::Stone);
}

void testStyles() {
    HouseGenerator gen;
    float villageH = 0, townH = 0, highriseH = 0;
    int highriseFlat = 0;
    for (unsigned seed = 1; seed <= 10; ++seed) {
        HouseGenerator::Params pv = HouseGenerator::randomParams(HouseStyle::Village, 8.f, seed);
        HouseGenerator::Params pt = HouseGenerator::randomParams(HouseStyle::Town, 8.f, seed);
        HouseGenerator::Params ph = HouseGenerator::randomParams(HouseStyle::Highrise, 8.f, seed);
        const HouseMesh hv = gen.generate(pv);
        const HouseMesh ht = gen.generate(pt);
        const HouseMesh hh = gen.generate(ph);
        villageH += hv.spec.wallHeight;
        townH += ht.spec.wallHeight;
        highriseH += hh.spec.wallHeight;
        CHECK(hh.spec.roofType == RoofType::Flat);
        if (hh.spec.roofType == RoofType::Flat) ++highriseFlat;
        checkMeshValid(hh.walls);
        checkMeshValid(hh.details);
        CHECK(hh.details.vertexCount() > 24); // window grid
    }
    villageH /= 10.f; townH /= 10.f; highriseH /= 10.f;
    CHECK(villageH < townH);
    CHECK(highriseH > townH * 2.f);
    CHECK(highriseFlat == 10);
}

void testClimateRules() {
    HouseGenerator gen;
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 9u;

    // wet + cold (lots of snow): steep gable/hipped roof, wide eaves, gutter always
    BuildingContext wet;
    wet.temperature = 0.1f; wet.precipitation = 0.95f; wet.humidity = 0.9f;
    p.context = wet;
    const HouseMesh w = gen.generate(p);
    CHECK(w.spec.roofType == RoofType::Gable || w.spec.roofType == RoofType::Hipped);
    CHECK(w.spec.hasGutter);
    CHECK(w.spec.roofOverhang >= 0.25f);
    CHECK(w.spec.roofHeight >= 1.2f); // steep
    CHECK(w.spec.windowScale > 1.1f); // cold: big windows
    CHECK(w.spec.hasChimney);         // heating

    // arid + hot: flat roof with parapet, small windows, shutters
    BuildingContext arid;
    arid.temperature = 0.95f; arid.precipitation = 0.08f; arid.humidity = 0.1f;
    p.context = arid;
    const HouseMesh a = gen.generate(p);
    CHECK(a.spec.roofType == RoofType::Flat);
    CHECK(a.spec.hasParapet);
    CHECK(!a.spec.hasGutter);
    CHECK(a.spec.windowScale < 0.8f);  // hot: small windows keep the heat out
    CHECK(a.spec.hasShutters);

    // determinism with a context
    const HouseMesh w2 = gen.generate(p);
    CHECK(a.walls.vertices == w2.walls.vertices);
    CHECK(specSignature(a.spec) == specSignature(w2.spec));
}

void testMaterialRules() {
    HouseGenerator gen;
    HouseGenerator::Params p;
    p.style = HouseStyle::Village;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 5u;

    // explicit local material wins
    BuildingContext wood;
    wood.localMaterial = LocalMaterial::Wood;
    p.context = wood;
    const HouseMesh h = gen.generate(p);
    CHECK(h.spec.facade == FacadeMaterial::Wood);
    CHECK(h.spec.timberFrame); // wood construction -> Fachwerk

    // biome-driven Auto resolution (worldgen Biome cast): 2 = desert -> adobe, 8 = rock -> stone
    BuildingContext desert;
    desert.biome = 2; desert.temperature = 0.9f; desert.precipitation = 0.1f;
    p.context = desert;
    const HouseMesh d = gen.generate(p);
    CHECK(d.spec.facade == FacadeMaterial::Adobe);
    CHECK(d.spec.roofType == RoofType::Flat); // arid via climate

    BuildingContext rock;
    rock.biome = 8; rock.temperature = 0.2f; rock.precipitation = 0.8f;
    p.context = rock;
    const HouseMesh r = gen.generate(p);
    CHECK(r.spec.facade == FacadeMaterial::Stone);

    // metropole override: high density + high wealth -> concrete even in a forest biome
    BuildingContext metro;
    metro.biome = 4; metro.density = 0.95f; metro.wealth = 0.85f;
    p.context = metro;
    p.style = HouseStyle::Highrise;
    const HouseMesh m = gen.generate(p);
    CHECK(m.spec.facade == FacadeMaterial::Concrete);
}

void testSocioEconomyRules() {
    HouseGenerator gen;
    // high density -> many storeys + ground-floor shop front
    BuildingContext dense;
    dense.density = 0.9f; dense.wealth = 0.5f; dense.temperature = 0.5f; dense.precipitation = 0.5f;
    HouseGenerator::Params p = HouseGenerator::randomParams(HouseStyle::Town, 8.f, 77u, dense);
    CHECK(p.floors >= 4);
    const HouseMesh h = gen.generate(p);
    CHECK(h.spec.hasShopFront);

    // low density -> detached house with garden fence
    BuildingContext sparse;
    sparse.density = 0.1f; sparse.wealth = 0.5f;
    p = HouseGenerator::randomParams(HouseStyle::Village, 8.f, 78u, sparse);
    CHECK(p.floors <= 2);
    const HouseMesh s = gen.generate(p);
    CHECK(s.spec.hasFence);

    // low wealth -> patched walls + rougher walls; high wealth -> cornice bands, cleaner walls
    BuildingContext poor;
    poor.wealth = 0.1f; poor.density = 0.5f;
    p = HouseGenerator::randomParams(HouseStyle::Town, 8.f, 79u, poor);
    p.wallIrregularity = 0.02f;
    const HouseMesh lo = gen.generate(p);
    CHECK(lo.spec.hasPatches);
    BuildingContext rich;
    rich.wealth = 0.95f; rich.density = 0.5f;
    p = HouseGenerator::randomParams(HouseStyle::Town, 8.f, 79u, rich);
    p.wallIrregularity = 0.02f;
    const HouseMesh hi = gen.generate(p);
    CHECK(hi.spec.hasCornice);
    CHECK(!hi.spec.hasPatches);
}

void testStilts() {
    HouseGenerator gen;
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 11u;
    BuildingContext flood;
    flood.waterProximity = 0.9f; // flood zone / coast
    p.context = flood;
    const HouseMesh h = gen.generate(p);
    CHECK(h.spec.onStilts);
    CHECK(h.spec.stiltHeight > 1.0f);
    // the windows/glass are lifted with the house, above the foundation (the stilt posts in
    // details intentionally reach down to the foundation, so check the glass instead)
    CHECK(h.glass.vertexCount() > 0);
    float minGlassY = 1e9f;
    for (std::size_t i = 0; i < h.glass.vertexCount(); ++i)
        minGlassY = std::min(minGlassY, h.glass.vertices[i * 11 + 1]);
    CHECK(minGlassY > h.spec.stiltHeight * 0.9f);
    checkMeshValid(h.walls);
    checkMeshValid(h.details);
    checkMeshValid(h.roof);
}

void testOrganicFeatures() {
    HouseGenerator gen;
    // bevel: chamfered wall body has many more vertices than a plain box (24) and outward normals
    // (hollow-shell mode; with interiors the shell is segmented around real openings instead)
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 21u;
    p.interior = false;
    const HouseMesh bv = gen.generate(p);
    CHECK(bv.spec.beveled);
    CHECK(bv.walls.vertexCount() > 24); // 96 for the chamfered box
    CHECK(bv.spec.warped);
    CHECK(bv.spec.hasPlinth);  // every house gets the massive plinth
    CHECK(bv.spec.hasLintels); // lintels above door/windows
    const float cy = bv.spec.wallHeight * 0.5f; // box center after the shiftY
    for (std::size_t i = 0; i < 96 && i < bv.walls.vertexCount(); ++i) { // bevel box = first 96 verts (annexes follow)
        const float* v = &bv.walls.vertices[i * 11];
        // bevel verts: normal points away from the (shifted) box center
        CHECK(v[3] * v[0] + v[4] * (v[1] - cy) + v[5] * v[2] > -0.01f);
    }
    p.bevelEdges = false;
    p.warpAmount = 0.f;
    const HouseMesh pl = gen.generate(p);
    CHECK(!pl.spec.beveled && !pl.spec.warped);
    CHECK(pl.walls.vertexCount() < bv.walls.vertexCount()); // plain box (24) + annexes < chamfered
    p.bevelEdges = true;
    p.warpAmount = 0.045f;

    // growth phases + roof penetrations + clutter occur across seeds; ivy follows humidity
    int overhangs = 0, solars = 0, roofWins = 0, props = 0, ivies = 0;
    for (unsigned seed = 1; seed <= 40; ++seed) {
        HouseGenerator::Params q = HouseGenerator::randomParams(HouseStyle::Village, 8.f, seed);
        q.seed = seed * 13u + 3u;
        q.clutter = 1.f;
        BuildingContext hum;
        hum.humidity = 0.9f; // humid -> ivy
        q.context = hum;
        const HouseMesh h = gen.generate(q);
        checkMeshValid(h.walls);
        checkMeshValid(h.details);
        checkMeshValid(h.roof);
        if (h.glass.vertexCount() > 0) checkMeshValid(h.glass);
        if (h.spec.hasOverhang) ++overhangs;
        if (h.spec.hasSolar) ++solars;
        if (h.spec.hasRoofWindow) ++roofWins;
        props += h.spec.props;
        if (h.spec.hasIvy) ++ivies;
    }
    CHECK(overhangs > 0);   // overhanging upper floors occur
    CHECK(solars > 0);      // solar panels occur
    CHECK(roofWins > 0);    // roof windows occur
    CHECK(props > 20);      // clutter props occur (flower boxes, AC, dish, firewood, line)
    CHECK(ivies > 20);      // humid climate -> ivy on most houses
    // dry climate -> no ivy
    HouseGenerator::Params q = HouseGenerator::randomParams(HouseStyle::Village, 8.f, 5u);
    BuildingContext dry;
    dry.humidity = 0.1f;
    q.context = dry;
    const HouseMesh d = gen.generate(q);
    CHECK(!d.spec.hasIvy);
    // clutter off -> no props
    HouseGenerator::Params q2 = HouseGenerator::randomParams(HouseStyle::Town, 8.f, 5u);
    q2.clutter = 0.f;
    const HouseMesh c0 = gen.generate(q2);
    CHECK(c0.spec.props == 0);
    // determinism with the new features
    const HouseMesh a = gen.generate(p);
    const HouseMesh b = gen.generate(p);
    CHECK(a.details.vertices == b.details.vertices);
    CHECK(specSignature(a.spec) == specSignature(b.spec));
}

// --- interior pipeline: floorplan, doors, windows, shell with real openings ---------------

void testInteriorPipeline() {
    HouseGenerator gen;
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 7.f; p.depth = 5.5f; p.floors = 2; p.seed = 42u;
    p.floorHeight = 2.8f;
    const HouseMesh hm = gen.generate(p);
    CHECK(hm.spec.hasInterior);
    const Floorplan& fp = hm.floorplan;
    const float t = hm.spec.wallThickness;
    CHECK_NEAR(t, 0.24f, 1e-6f);
    // step 1: BSP rooms tile the inner rect exactly (no gaps/overlaps -> area sum matches)
    CHECK(fp.rooms.size() >= 2);
    float area = 0.f;
    for (const PlanRoom& r : fp.rooms) area += (r.x1 - r.x0) * (r.z1 - r.z0);
    CHECK_NEAR(area, (p.width - 2.f * t) * (p.depth - 2.f * t), 0.05f);
    CHECK(fp.hallRoom >= 0 && fp.hallRoom < static_cast<int>(fp.rooms.size()));
    // hall touches the street-facing (+z) side
    CHECK(std::fabs(fp.rooms[fp.hallRoom].z1 - (p.depth * 0.5f - t)) < 1e-3f);
    // multi-storey: a stairwell room is reserved
    if (fp.rooms.size() > 2) CHECK(fp.stairRoom >= 0);
    // step 2: front door on the +z exterior wall of the hall, ground floor only
    CHECK(!fp.doors.empty());
    const PlanDoor& front = fp.doors[0];
    CHECK(front.exterior);
    CHECK_NEAR(front.fixed, p.depth * 0.5f - t * 0.5f, 1e-3f);
    CHECK(front.floor0 == 0 && front.floor1 == 0);
    CHECK(front.center >= fp.rooms[fp.hallRoom].x0 && front.center <= fp.rooms[fp.hallRoom].x1);
    // spanning tree: BFS over the interior doors from the hall reaches every room
    {
        const int n = static_cast<int>(fp.rooms.size());
        std::vector<std::vector<int>> adj(n);
        int interiorDoors = 0;
        for (const PlanDoor& d : fp.doors)
            if (!d.exterior) {
                adj[d.roomA].push_back(d.roomB);
                adj[d.roomB].push_back(d.roomA);
                ++interiorDoors;
            }
        CHECK(interiorDoors >= n - 1); // spanning tree covers all rooms
        std::vector<char> seen(n, 0);
        std::vector<int> stack{fp.hallRoom};
        seen[fp.hallRoom] = 1;
        while (!stack.empty()) {
            const int u = stack.back();
            stack.pop_back();
            for (const int v : adj[u])
                if (!seen[v]) { seen[v] = 1; stack.push_back(v); }
        }
        for (char s : seen) CHECK(s); // every room reachable from the hall
    }
    // step 3: windows only on exterior walls (|fixed| = half extent minus half thickness),
    // with min distance to corners and to the doors on the same wall
    CHECK(!fp.windows.empty());
    for (const PlanWindow& win : fp.windows) {
        const float hw = p.width * 0.5f - t * 0.5f, hd = p.depth * 0.5f - t * 0.5f;
        const bool onExt = std::fabs(std::fabs(win.fixed) - hw) < 1e-3f ||
                           std::fabs(std::fabs(win.fixed) - hd) < 1e-3f;
        CHECK(onExt);
        CHECK(win.center - win.width * 0.5f > -p.width * 0.5f + 0.4f); // corner margin
        CHECK(win.center + win.width * 0.5f < p.width * 0.5f - 0.4f + 1.f);
    }
    // min distance window<->door on the same wall
    for (const PlanWindow& win : fp.windows)
        for (const PlanDoor& d : fp.doors)
            if (d.axis == win.axis && std::fabs(d.fixed - win.fixed) < 1e-3f &&
                d.floor0 <= win.floor1 && win.floor0 <= d.floor1)
                CHECK(std::fabs(win.center - d.center) >=
                      (win.width + d.width) * 0.5f + 0.2f);
    // storage rooms get no windows
    for (const PlanWindow& win : fp.windows)
        CHECK(fp.rooms[win.room].type != RoomType::Storage);
    // step 4: shell walls carry the openings — walls mesh outer faces at |z| = depth/2,
    // interior mesh has faces inside the footprint, door leaves + glass exist
    float maxAbsZ = 0.f;
    for (std::size_t i = 0; i < hm.walls.vertexCount(); ++i)
        maxAbsZ = std::max(maxAbsZ, std::fabs(hm.walls.vertices[i * 11 + 2]));
    CHECK_NEAR(maxAbsZ, p.depth * 0.5f, 1e-3f); // outer silhouette unchanged (facade POM fits)
    checkMeshValid(hm.walls);
    checkMeshValid(hm.interior);
    checkMeshValid(hm.doors);
    checkMeshValid(hm.glass);
    CHECK(hm.interior.vertexCount() > 0);
    CHECK(hm.doors.vertexCount() >= 24); // at least one leaf box (front door)
    CHECK(hm.glass.vertexCount() >= 24 * hm.spec.windowCount); // one pane box per window (floors)
    CHECK(hm.spec.furniture > 0); // rooms got furniture (CSG sub-meshes)
    // determinism incl. the plan
    const HouseMesh hm2 = gen.generate(p);
    CHECK(hm2.interior.vertices == hm.interior.vertices);
    CHECK(hm2.doors.vertices == hm.doors.vertices);
    CHECK(hm2.floorplan.rooms.size() == fp.rooms.size());
    CHECK(hm2.floorplan.doors.size() == fp.doors.size());
}

void testInteriorModes() {
    HouseGenerator gen;
    // interior off -> old hollow shell, empty plan
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 5u;
    p.interior = false;
    HouseMesh hm = gen.generate(p);
    CHECK(!hm.spec.hasInterior);
    CHECK(hm.floorplan.rooms.empty());
    CHECK(hm.interior.vertexCount() == 0 && hm.doors.vertexCount() == 0);
    CHECK(hm.walls.vertexCount() > 0);
    // highrise ignores the interior flag (window-grid facade)
    p.interior = true;
    p.style = HouseStyle::Highrise;
    p.floors = 6;
    hm = gen.generate(p);
    CHECK(!hm.spec.hasInterior);
    CHECK(hm.spec.roofType == RoofType::Flat);
    // tiny footprints fall back to the hollow shell (no room fits)
    p.style = HouseStyle::Village;
    p.floors = 1;
    p.width = 2.8f; p.depth = 2.8f;
    hm = gen.generate(p);
    CHECK(!hm.spec.hasInterior);
    // stairwell: upper-floor slab keeps the opening -> the interior mesh has geometry above
    // floor 0 in the stair room; just check the interior mesh spans multiple floors
    p.width = 7.f; p.depth = 5.5f; p.floors = 3;
    hm = gen.generate(p);
    CHECK(hm.spec.hasInterior);
    float maxY = 0.f;
    for (std::size_t i = 0; i < hm.interior.vertexCount(); ++i)
        maxY = std::max(maxY, hm.interior.vertices[i * 11 + 1]);
    CHECK(maxY > 2.f * p.floorHeight); // slabs/stairs reach the upper floors
}

} // namespace

void runProceduralHouseTests() {
    testValidityAndDeterminism();
    testVariantRichness();
    testStyles();
    testWallJitterAndFacade();
    testClimateRules();
    testMaterialRules();
    testSocioEconomyRules();
    testStilts();
    testOrganicFeatures();
    testInteriorPipeline();
    testInteriorModes();
}
