// Unit tests for the G20 terrain + LOD integration (no GL context needed): LodMesh level
// selection (selectLevel/resolveLodMesh/defaultDistances) and the TerrainComponent —
// heightAt() on a manually set heightmap and the fillFrame() path (DrawItem carries the
// LodMesh pointer so the renderer picks the level per camera distance).

#include "test_framework.hpp"

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/cam/camera.hpp>
#include <fuhgl/math/func.hpp>
#include <fuhgl/render/lod.hpp>

#include <vector>

using namespace fuhglgraphics;

void runTerrainLodTests() {
    // --- LodMesh::selectLevel / defaultDistances (pure CPU) ---
    {
        fuhgl::render::LodMesh lod;
        std::vector<fuhgl::render::Mesh> levels(3); // uncreated meshes: create() only stores
        CHECK(lod.create(std::move(levels), {10.f, 30.f}));
        CHECK(lod.levelCount() == 3);
        CHECK(lod.selectLevel(0.f) == 0);
        CHECK(lod.selectLevel(9.9f) == 0);
        CHECK(lod.selectLevel(10.f) == 1);  // distance >= switch distance
        CHECK(lod.selectLevel(29.9f) == 1);
        CHECK(lod.selectLevel(30.f) == 2);
        CHECK(lod.selectLevel(1000.f) == 2); // clamped to the last level
        CHECK(lod.select(15.f) == &lod.level(1));
        // invalid distances rejected
        fuhgl::render::LodMesh bad;
        std::vector<fuhgl::render::Mesh> one;
        one.push_back(fuhgl::render::Mesh{});
        CHECK(!bad.create(std::move(one), {5.f, 2.f})); // 1 level needs 0 distances / ascending
        std::vector<fuhgl::render::Mesh> two;
        two.push_back(fuhgl::render::Mesh{});
        two.push_back(fuhgl::render::Mesh{});
        CHECK(!bad.create(std::move(two), {5.f, 2.f})); // 2 levels need exactly 1 distance

        const auto d = fuhgl::render::LodMesh::defaultDistances(fuhgl::math::radians(60.f), 10.f,
                                                                {0.5f, 0.25f, 0.125f});
        CHECK(d.size() == 3);
        CHECK(d[0] < d[1] && d[1] < d[2]); // ascending
        // d = radius / (fraction * tan(fov/2)); tan(30 deg) ~ 0.5774
        CHECK_NEAR(d[0], 10.f / (0.5f * 0.5773503f), 0.01f);
    }

    // --- resolveLodMesh: item.lod + camera distance -> level (F41) ---
    {
        fuhgl::render::LodMesh lod;
        std::vector<fuhgl::render::Mesh> levels(3);
        CHECK(lod.create(std::move(levels), {10.f, 30.f}));
        fuhgl::render::Mesh plain;
        fuhgl::render::DrawItem item;
        item.mesh = &plain;
        item.lod = &lod;
        int level = -1;
        // camera at the bounding-sphere center (origin): distance 0 -> level 0
        CHECK(fuhgl::render::resolveLodMesh(item, {0.f, 0.f, 0.f}, true, &level) == &lod.level(0));
        CHECK(level == 0);
        // 20 units away -> level 1
        CHECK(fuhgl::render::resolveLodMesh(item, {0.f, 0.f, 20.f}, true, &level) == &lod.level(1));
        CHECK(level == 1);
        // lodEnabled = false -> always the plain mesh, level -1
        CHECK(fuhgl::render::resolveLodMesh(item, {0.f, 0.f, 20.f}, false, &level) == &plain);
        CHECK(level == -1);
        // no lod pointer -> plain mesh
        item.lod = nullptr;
        CHECK(fuhgl::render::resolveLodMesh(item, {0.f, 0.f, 20.f}, true, &level) == &plain);
    }

    // --- TerrainComponent::heightAt on a manual heightmap (no GL) ---
    {
        scene::TerrainComponent tc;
        tc.gridW = tc.gridH = 3;
        tc.sizeX = tc.sizeZ = 2.f;
        tc.heightScale = 4.f;
        tc.heights.resize(9);
        for (int i = 0; i < 9; ++i) tc.heights[static_cast<std::size_t>(i)] = i / 8.f; // 0..1 ramp
        CHECK_NEAR(tc.heightAt(0.f, 0.f), 0.5f * 4.f, 1e-6f);  // center = heights[4]
        CHECK_NEAR(tc.heightAt(-1.f, -1.f), 0.f, 1e-6f);         // corner min
        CHECK_NEAR(tc.heightAt(1.f, 1.f), 1.f * 4.f, 1e-6f);     // corner max
        CHECK_NEAR(tc.heightAt(2.f, 2.f), 1.f * 4.f, 1e-6f);     // clamped outside
        scene::TerrainComponent empty;
        CHECK_NEAR(empty.heightAt(0.f, 0.f), 0.f, 1e-6f);
    }

    // --- fillFrame() path: the terrain draw item carries the LodMesh (LOD resolved per frame) ---
    {
        scene::Scene scene;
        scene::Node& tn = scene.root().createChild("terrain");
        scene::TerrainComponent& tc = tn.attach<scene::TerrainComponent>();
        std::vector<fuhgl::render::Mesh> levels(3);
        CHECK(tc.lodMesh.create(std::move(levels), {10.f, 30.f}));
        tc.materialId = 7;
        tc.castShadow = false;

        fuhgl::FirstPersonCamera cam;
        cam.position = {0.f, 2.f, 5.f};
        cam.fovY = fuhgl::math::radians(60.f);

        scene::FrameData frame;
        scene.fillFrame(frame, &cam, 64, 64);
        CHECK(frame.view.items.size() == 1);
        CHECK(frame.view.items[0].lod == &tc.lodMesh);
        CHECK(frame.view.items[0].mesh == &tc.lodMesh.level(0));
        CHECK(frame.materialIds.size() == 1 && frame.materialIds[0] == 7);
        CHECK(frame.castShadow.size() == 1 && !frame.castShadow[0]);
        CHECK(frame.skins.size() == 1 && frame.skins[0] == nullptr);
        // near camera (dist ~5) -> level 0; far camera (dist 50) -> level 2
        int level = -1;
        fuhgl::render::resolveLodMesh(frame.view.items[0], cam.position, true, &level);
        CHECK(level == 0);
        fuhgl::render::resolveLodMesh(frame.view.items[0], {0.f, 0.f, 50.f}, true, &level);
        CHECK(level == 2);
        // the node's world AABB covers the terrain bounding sphere (frustum culling)
        const scene::Node* tn2 = scene.root().children()[0].get();
        CHECK(tn2->hasAabb());
        fuhgl::math::Vec3 mn, mx;
        tn2->worldAabb(mn, mx);
        CHECK(mn.x <= -1.f && mx.x >= 1.f); // the (default) bounding sphere has radius >= 1
    }
}
