// Unit tests for the plant generator (label "unit"): mesh validity, determinism per seed,
// per-kind differences (fir conical + dark needles, birch white trunk + drooping light crown,
// bush low and wide, deciduous in between), scale/height behavior, and randomParams.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/plant.hpp>

#include <algorithm>
#include <cmath>

namespace {

using namespace fuhglgraphics::procedural;

void checkMeshValid(const fuhgl::render::MeshData& md) {
    CHECK(md.vertexCount() > 0);
    CHECK(md.indices.size() >= 3 && md.indices.size() % 3 == 0);
    for (const std::uint32_t idx : md.indices) CHECK(idx < md.vertexCount());
    for (std::size_t i = 0; i < md.vertexCount(); ++i) {
        const float* v = &md.vertices[i * 11];
        for (int k = 0; k < 11; ++k) CHECK(std::isfinite(v[k]));
    }
}

// crown width of the leaf-colored vertices at a given height band [y0,y1]
float crownWidth(const PlantMesh& pm, float y0, float y1) {
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (std::size_t i = 0; i < pm.mesh.vertexCount(); ++i) {
        const float* v = &pm.mesh.vertices[i * 11];
        if (std::fabs(v[6] - pm.leafColor.x) > 1e-4f) continue;
        if (v[1] < y0 || v[1] > y1) continue;
        minX = std::min(minX, v[0]); maxX = std::max(maxX, v[0]);
        minZ = std::min(minZ, v[2]); maxZ = std::max(maxZ, v[2]);
    }
    if (maxX < minX) return 0.f;
    return std::max(maxX - minX, maxZ - minZ);
}

void testValidityAndDeterminism() {
    PlantGenerator gen;
    for (int k = 0; k < 4; ++k) {
        PlantParams p = PlantGenerator::randomParams(static_cast<PlantKind>(k), 42u + k * 7u);
        const PlantMesh a = gen.generate(p);
        const PlantMesh b = gen.generate(p);
        checkMeshValid(a.mesh);
        CHECK(a.mesh.vertices == b.mesh.vertices); // deterministic per seed
        CHECK(a.mesh.indices == b.mesh.indices);
        CHECK(a.height > 0.f);
        CHECK(a.kind == p.kind);
    }
}

void testKindsDiffer() {
    PlantGenerator gen;
    PlantParams pf = PlantGenerator::randomParams(PlantKind::Fir, 9u);
    PlantParams pd = PlantGenerator::randomParams(PlantKind::Deciduous, 9u);
    PlantParams pb = PlantGenerator::randomParams(PlantKind::Birch, 9u);
    PlantParams pu = PlantGenerator::randomParams(PlantKind::Bush, 9u);
    const PlantMesh fir = gen.generate(pf);
    const PlantMesh decid = gen.generate(pd);
    const PlantMesh birch = gen.generate(pb);
    const PlantMesh bush = gen.generate(pu);
    // birch trunk is (near-)white, fir trunk dark brown, deciduous in between
    CHECK(birch.trunkColor.x > 0.8f);
    CHECK(fir.trunkColor.x < 0.4f);
    // fir needles darker than deciduous leaves
    CHECK(fir.leafColor.y < decid.leafColor.y);
    // fir crown is more conical than a deciduous crown (top quarter narrower relative to middle)
    auto taperRatio = [](const PlantMesh& m) {
        const float top = crownWidth(m, m.height * 0.75f, m.height);
        const float mid = crownWidth(m, m.height * 0.35f, m.height * 0.6f);
        return mid > 0.f ? top / mid : 1.f;
    };
    CHECK(taperRatio(fir) < taperRatio(decid));
    // bush is much lower than a tree of the same scale
    PlantParams pu2 = pu; pu2.scale = pd.scale;
    const PlantMesh bush2 = gen.generate(pu2);
    CHECK(bush2.height < decid.height * 0.8f);
    // same kind + different seeds -> different meshes (variety)
    PlantParams p1 = PlantGenerator::randomParams(PlantKind::Deciduous, 1u);
    PlantParams p2 = PlantGenerator::randomParams(PlantKind::Deciduous, 2u);
    CHECK(gen.generate(p1).mesh.vertices != gen.generate(p2).mesh.vertices);
}

void testScale() {
    PlantGenerator gen;
    PlantParams p = PlantGenerator::randomParams(PlantKind::Deciduous, 5u);
    p.scale = 1.f;
    const PlantMesh a = gen.generate(p);
    p.scale = 2.f;
    const PlantMesh b = gen.generate(p);
    CHECK_NEAR(b.height, a.height * 2.f, 1e-3f);
}

} // namespace

void runProceduralPlantTests() {
    testValidityAndDeterminism();
    testKindsDiffer();
    testScale();
}
