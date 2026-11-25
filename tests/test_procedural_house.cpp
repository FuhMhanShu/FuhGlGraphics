// Unit tests for the class-based house generator (label "unit"): mesh validity (indices in
// range, finite unit normals), determinism per seed, variant richness (many distinct houses
// across seeds), roof-type coverage, feature flags (bay/dormer/annex/porch/chimney/timber
// frame), style height ordering (village < town < highrise), and the BuildingContext rules:
// climate -> roof pitch/overhang/gutter/flat+parapet/window size/shutters/chimney, flood zone
// -> stilts, local resources/biome -> facade material, density -> storeys/shop front/fence,
// wealth -> patches/cornice/wall jitter.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/house.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

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
           (s.hasPatches ? "W" : "") + (s.hasCornice ? "O" : "");
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
    CHECK(specSignature(a.spec) == specSignature(b.spec));
    CHECK(a.glass.vertexCount() > 0); // window glass panes exist for reflective material
    CHECK(a.spec.wallIrregular);      // default wallIrregularity > 0 -> jittered walls
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
    // flat walls when wallIrregularity == 0
    HouseGenerator::Params p;
    p.style = HouseStyle::Town;
    p.width = 6.f; p.depth = 4.5f; p.floors = 2; p.seed = 77u;
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
}
