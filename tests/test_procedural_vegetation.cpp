// Unit tests for the G37 ecophysiological vegetation generator (label "unit"):
// climate-to-geometry rules (cold->needle/conical/droop, wet->drip tips, dry->thorn/succulent,
// wind->lean+reduced height, density->high crown base+self-pruning), space colonization
// (open tree broad crown vs. dense stand tall narrow crown with pruned bottom), Da Vinci pipe
// model (R^2 = sum r_i^2), determinism per seed, mesh validity, leaf/bark tint helpers.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/vegetation.hpp>

#include <algorithm>
#include <cmath>

namespace {

using namespace fuhglgraphics::procedural;
using fuhgl::math::Vec3;

float clamp01f(float x) { return std::min(1.f, std::max(0.f, x)); }

void checkMeshValid(const fuhgl::render::MeshData& md) {
    CHECK(md.vertexCount() > 0);
    CHECK(md.indices.size() >= 3 && md.indices.size() % 3 == 0);
    for (const std::uint32_t idx : md.indices) CHECK(idx < md.vertexCount());
    for (std::size_t i = 0; i < md.vertexCount(); ++i) {
        const float* v = &md.vertices[i * 11];
        for (int k = 0; k < 11; ++k) CHECK(std::isfinite(v[k]));
    }
}

// crown width of the leaf-mesh vertices in a height band [y0,y1]
float crownWidth(const fuhgl::render::MeshData& leaves, float y0, float y1) {
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (std::size_t i = 0; i < leaves.vertexCount(); ++i) {
        const float* v = &leaves.vertices[i * 11];
        if (v[1] < y0 || v[1] > y1) continue;
        minX = std::min(minX, v[0]); maxX = std::max(maxX, v[0]);
        minZ = std::min(minZ, v[2]); maxZ = std::max(maxZ, v[2]);
    }
    if (maxX < minX) return 0.f;
    return std::max(maxX - minX, maxZ - minZ);
}

// number of branch-mesh vertices below y (self-pruning check: dense stand has none in the
// lower trunk region, an open tree has branches there)
int branchVerticesBelow(const fuhgl::render::MeshData& branches, float y) {
    int n = 0;
    for (std::size_t i = 0; i < branches.vertexCount(); ++i)
        if (branches.vertices[i * 11 + 1] < y) ++n;
    return n;
}

void testClimateToGeometry() {
    // cold/snowy -> needles, conical narrow crown, drooping branches
    EcoEnvironment cold;
    cold.temperature = 0.05f;
    cold.humidity = 0.5f;
    const GrowthForm fc = deriveGrowthForm(cold);
    CHECK(fc.leaf == LeafMorphology::Needle);
    CHECK(fc.branchAngleDeg > 100.f); // branches hang downward
    CHECK(fc.droop > 0.3f);
    // very wet tropics -> drip-tip leaves, big and thin
    EcoEnvironment tropic;
    tropic.temperature = 0.9f;
    tropic.humidity = 0.95f;
    const GrowthForm ft = deriveGrowthForm(tropic);
    CHECK(ft.leaf == LeafMorphology::DripTip);
    CHECK(ft.dripTip > 0.9f);
    CHECK(ft.leafThickness < 0.3f);
    CHECK(ft.leafSize > 0.9f);
    // dry steppe -> small waxy leaves; very dry -> thorns or succulent
    EcoEnvironment dry;
    dry.temperature = 0.8f;
    dry.humidity = 0.22f;
    const GrowthForm fd = deriveGrowthForm(dry);
    CHECK(fd.leaf == LeafMorphology::Broad);
    CHECK(fd.leafThickness > 0.9f);
    CHECK(fd.leafSize < 0.45f);
    EcoEnvironment desert;
    desert.temperature = 0.9f;
    desert.humidity = 0.05f;
    const GrowthForm fde = deriveGrowthForm(desert);
    CHECK(fde.leaf == LeafMorphology::None || fde.leaf == LeafMorphology::Thorn);
    CHECK(fde.succulent || fde.leaf == LeafMorphology::Thorn);
    // warm frost-free -> broad leaves, wide horizontal branches
    EcoEnvironment warm;
    warm.temperature = 0.85f;
    warm.humidity = 0.5f;
    const GrowthForm fw = deriveGrowthForm(warm);
    CHECK(fw.leaf == LeafMorphology::Broad);
    CHECK(fw.branchAngleDeg < 80.f);
    // wind exposure -> constant wind vector on the growth direction + reduced max height
    EcoEnvironment windy;
    windy.temperature = 0.5f;
    windy.humidity = 0.5f;
    windy.windExposure = 0.9f;
    windy.windDir = {1.f, 0.f, 0.f};
    const GrowthForm fwi = deriveGrowthForm(windy);
    CHECK(fwi.windLean.x > 0.5f);
    CHECK(fwi.maxHeight < deriveGrowthForm(EcoEnvironment{0.5f, 0.5f, 0.f, {1.f, 0.f, 0.f}, 0.f, 0.f, 1u}).maxHeight * 0.7f);
    // dense stand -> high crown base + self-pruning on
    EcoEnvironment dense;
    dense.temperature = 0.5f;
    dense.humidity = 0.5f;
    dense.standDensity = 0.9f;
    const GrowthForm fdn = deriveGrowthForm(dense);
    CHECK(fdn.selfPrune);
    CHECK(fdn.pruneBelow > 0.4f * fdn.maxHeight * 0.9f);
}

void testSpaceColonizationCompetition() {
    VegetationGenerator gen;
    // open meadow: light from all sides -> broad, wide crown
    EcoEnvironment open;
    open.temperature = 0.8f;
    open.humidity = 0.5f;
    open.standDensity = 0.f;
    open.seed = 11u;
    VegetationParams po = VegetationGenerator::randomParams(open, 11u);
    po.scale = 1.f;
    const VegetationMesh mo = gen.generate(po);
    checkMeshValid(mo.branches);
    CHECK(mo.leaves.vertexCount() > 0);
    CHECK(mo.height > 2.f);
    CHECK(mo.attractorsLeft < po.sca.attractors); // attractors were consumed

    // dense forest: light only from above -> tall straight trunk, small top crown, lower
    // branches pruned away
    EcoEnvironment forest;
    forest.temperature = 0.8f;
    forest.humidity = 0.5f;
    forest.standDensity = 0.95f;
    forest.seed = 11u;
    VegetationParams pf = VegetationGenerator::randomParams(forest, 11u);
    pf.scale = 1.f;
    const VegetationMesh mf = gen.generate(pf);
    checkMeshValid(mf.branches);
    // crown much narrower than the open tree's, and the dense-stand tree grows taller
    // (kerzengerade hoch, erst oben die kleine Krone)
    const float wOpen = crownWidth(mo.leaves, 0.f, mo.height);
    const float wForest = crownWidth(mf.leaves, 0.f, mf.height);
    CHECK(wForest < wOpen * 0.75f);
    CHECK(mf.height > mo.height * 0.9f);
    // crown sits high: little leaf mass in the lower half
    const float lowerLeaves = crownWidth(mf.leaves, 0.f, mf.height * 0.5f);
    const float upperLeaves = crownWidth(mf.leaves, mf.height * 0.5f, mf.height);
    CHECK(lowerLeaves < upperLeaves);
    // self-pruning: almost no branch vertices in the lower trunk region
    CHECK(branchVerticesBelow(mf.branches, mf.form.pruneBelow * 0.5f) <= branchVerticesBelow(mo.branches, mf.form.pruneBelow * 0.5f));
}

void testPipeModel() {
    // R^n = sum of children radii^n at every internal node (Da Vinci rule, n = 2)
    EcoEnvironment eco;
    eco.temperature = 0.8f;
    eco.humidity = 0.5f;
    eco.seed = 7u;
    VegetationParams p = VegetationGenerator::randomParams(eco, 7u);
    p.scale = 1.f;
    const GrowthForm form = deriveGrowthForm(eco);
    ScaParams sca = p.sca;
    ScaTree tree = spaceColonization(form, eco, sca, eco.seed);
    const float tip = 0.03f;
    applyPipeModel(tree, tip, 2.f);
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        const ScaNode& nd = tree.nodes[i];
        if (!nd.alive) continue;
        float sum = 0.f;
        int kids = 0;
        for (std::size_t c = i + 1; c < tree.nodes.size(); ++c)
            if (tree.nodes[c].parent == static_cast<int>(i) && tree.nodes[c].alive) {
                sum += tree.nodes[c].radius * tree.nodes[c].radius;
                ++kids;
            }
        if (kids == 0) {
            CHECK_NEAR(nd.radius, tip, 1e-5f); // leaf tip keeps the bud radius
        } else {
            // parent cross-section = sum of children cross-sections (area, n=2)
            CHECK_NEAR(nd.radius * nd.radius, sum, 1e-4f);
            CHECK(nd.radius > 0.f);
        }
    }
    // trunk is the thickest part
    float maxR = 0.f;
    for (const ScaNode& nd : tree.nodes) maxR = std::max(maxR, nd.radius);
    CHECK_NEAR(tree.nodes[0].radius, maxR, 1e-5f);
}

void testWindFlaggedTree() {
    VegetationGenerator gen;
    EcoEnvironment windy;
    windy.temperature = 0.5f;
    windy.humidity = 0.4f;
    windy.windExposure = 1.f;
    windy.windDir = {1.f, 0.f, 0.f};
    windy.seed = 5u;
    VegetationParams p = VegetationGenerator::randomParams(windy, 5u);
    p.scale = 1.f;
    const VegetationMesh m = gen.generate(p);
    checkMeshValid(m.branches);
    // the crown leans into the wind (+x) and the tree stays low
    float meanX = 0.f;
    int n = 0;
    for (std::size_t i = 0; i < m.branches.vertexCount(); ++i) {
        const float* v = &m.branches.vertices[i * 11];
        if (v[1] > m.height * 0.5f) { meanX += v[0]; ++n; }
    }
    CHECK(n > 0);
    meanX /= static_cast<float>(n);
    CHECK(meanX > 0.05f); // crown displaced downwind
    CHECK(m.height < 6.f); // reduced max height
}

void testMaterialAndTints() {
    VegetationMaterial m;
    // autumn ramp: green -> yellow -> orange -> red-brown
    const Vec3 c0 = autumnRampColor(m, 0.f);
    const Vec3 c1 = autumnRampColor(m, 0.5f);
    const Vec3 c2 = autumnRampColor(m, 1.f);
    CHECK(c0.y > c0.x * 0.9f);            // greenish base at t=0 (ramp starts from leafOld green)
    CHECK(c1.y > 0.4f && c1.y > c1.z);     // yellow/orange mid
    CHECK(c2.x > c2.y && c2.y > c2.z);     // red-brown at t=1
    // leaf tint: young tips lighter (higher green) than old inner leaves
    const Vec3 young = leafTint(m, 1.f, 0.f);
    const Vec3 old = leafTint(m, 0.f, 0.f);
    CHECK(young.y > old.y);
    // full autumn overrides the green with the ramp color
    const Vec3 autumn = leafTint(m, 0.5f, 1.f);
    CHECK(autumn.x > autumn.z);
    // wet side: faces the incoming wind (normal . -windDir)
    CHECK_NEAR(wetSideFactor({-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}), 1.f, 1e-5f);
    CHECK_NEAR(wetSideFactor({1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}), 0.f, 1e-5f);
    CHECK_NEAR(wetSideFactor({0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}), 0.f, 1e-5f);
}

void testDeterminismAndVariety() {
    VegetationGenerator gen;
    EcoEnvironment eco;
    eco.temperature = 0.7f;
    eco.humidity = 0.6f;
    eco.seed = 3u;
    VegetationParams p = VegetationGenerator::randomParams(eco, 3u);
    p.scale = 1.f;
    const VegetationMesh a = gen.generate(p);
    const VegetationMesh b = gen.generate(p);
    CHECK(a.branches.vertices == b.branches.vertices); // deterministic per seed
    CHECK(a.branches.indices == b.branches.indices);
    CHECK(a.leaves.vertices == b.leaves.vertices);
    CHECK_NEAR(a.height, b.height, 1e-6f);
    // different seed -> different tree
    VegetationParams p2 = VegetationGenerator::randomParams(eco, 4u);
    p2.scale = 1.f;
    const VegetationMesh c = gen.generate(p2);
    CHECK(a.branches.vertices != c.branches.vertices);
    // succulent (desert) has no leaves but a fat trunk
    EcoEnvironment desert;
    desert.temperature = 0.9f;
    desert.humidity = 0.05f;
    desert.seed = 8u;
    VegetationParams pd = VegetationGenerator::randomParams(desert, 8u);
    pd.scale = 1.f;
    const VegetationMesh ms = gen.generate(pd);
    CHECK(ms.leaves.vertexCount() == 0);
    checkMeshValid(ms.branches);
    CHECK(ms.form.succulent);
}

} // namespace

void runProceduralVegetationTests() {
    testClimateToGeometry();
    testSpaceColonizationCompetition();
    testPipeModel();
    testWindFlaggedTree();
    testMaterialAndTints();
    testDeterminismAndVariety();
}
