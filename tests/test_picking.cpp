// Unit tests for the G18 picking module (no GL context needed): ray-from-screen, AABB slab
// test, Möller–Trumbore triangle test and a full scene::pick() on a small scene graph.

#include "test_framework.hpp"

#include <fuhglgraphics/scene/picking.hpp>
#include <fuhglgraphics/scene/scene.hpp>

#include <fuhgl/cam/camera.hpp>
#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cmath>

using namespace fuhglgraphics;

namespace {

// minimal camera with fixed view/proj (value type like the real controllers)
class TestCam : public fuhgl::Camera {
public:
    fuhgl::math::Vec3 pos{0.f, 0.f, 5.f};
    fuhgl::math::Vec3 target{0.f, 0.f, 0.f};
    float fovY = 1.0f;
    fuhgl::math::Mat4 view() const override {
        return fuhgl::math::lookAt(pos, target, {0.f, 1.f, 0.f});
    }
    fuhgl::math::Mat4 projection(float aspect) const override {
        return fuhgl::math::perspective(fovY, aspect, 0.1f, 100.f);
    }
    fuhgl::math::Vec3 eye() const override { return pos; }
    void update(float) override {}
};

} // namespace

void runPickingTests() {
    // --- rayFromScreen: center pixel -> camera forward (-Z), corners differ ---
    {
        TestCam cam;
        const scene::Ray center = scene::rayFromScreen(cam, 1.f, 100, 100, 49.5f, 49.5f); // exact image center
        CHECK_NEAR(center.dir.x, 0.f, 1e-4);
        CHECK_NEAR(center.dir.y, 0.f, 1e-4);
        CHECK_NEAR(center.dir.z, -1.f, 1e-4);
        CHECK_NEAR(fuhgl::math::length(center.dir), 1.f, 1e-5);
        const scene::Ray left = scene::rayFromScreen(cam, 1.f, 100, 100, 4.5f, 49.5f);
        const scene::Ray right = scene::rayFromScreen(cam, 1.f, 100, 100, 94.5f, 49.5f);
        CHECK(left.dir.x < -0.1f);
        CHECK(right.dir.x > 0.1f);
        CHECK_NEAR(left.dir.x, -right.dir.x, 1e-4); // symmetric
        const scene::Ray top = scene::rayFromScreen(cam, 1.f, 100, 100, 49.5f, 4.5f);
        CHECK(top.dir.y > 0.1f); // py from the top: top pixel -> +Y
    }

    // --- rayIntersectAabb ---
    {
        const scene::Ray r{{0.f, 0.f, 5.f}, {0.f, 0.f, -1.f}};
        float t = 0.f;
        CHECK(scene::rayIntersectAabb(r, {-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}, t));
        CHECK_NEAR(t, 4.f, 1e-4); // enters at z = 1 -> distance 4
        CHECK(!scene::rayIntersectAabb(r, {-1.f, -1.f, 6.f}, {1.f, 1.f, 10.f}, t)); // behind the camera
        const scene::Ray side{{5.f, 0.f, 0.f}, {0.f, 0.f, -1.f}};
        CHECK(!scene::rayIntersectAabb(side, {-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}, t)); // misses
        const scene::Ray inside{{0.f, 0.f, 0.f}, {0.f, 0.f, -1.f}};
        CHECK(scene::rayIntersectAabb(inside, {-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}, t));
        CHECK_NEAR(t, 0.f, 1e-5);
    }

    // --- rayIntersectTriangle (CCW against +Z, in the z=0 plane) ---
    {
        const scene::Ray r{{0.25f, 0.25f, 5.f}, {0.f, 0.f, -1.f}};
        float t = 0.f, u = 0.f, v = 0.f;
        CHECK(scene::rayIntersectTriangle(r, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, t, u, v));
        CHECK_NEAR(t, 5.f, 1e-4);
        CHECK_NEAR(u, 0.25, 1e-4);
        CHECK_NEAR(v, 0.25, 1e-4);
        const scene::Ray miss{{2.f, 2.f, 5.f}, {0.f, 0.f, -1.f}};
        CHECK(!scene::rayIntersectTriangle(miss, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, t, u, v));
        const scene::Ray back{{0.25f, 0.25f, 5.f}, {0.f, 0.f, 1.f}}; // from behind, dir away
        CHECK(!scene::rayIntersectTriangle(back, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, t, u, v));
    }

    // --- pick() on a small scene: one box at z = -2 (meshData set), one far box ---
    {
        scene::Scene scene;
        const fuhgl::render::MeshData boxData = fuhgl::render::shapes::boxData({0.5f, 0.5f, 0.5f}, {1, 0, 0});
        scene::Node& nearNode = scene.root().createChild("nearBox");
        nearNode.translation = {0.f, 0.f, -2.f};
        scene::MeshEntry& ne = nearNode.attach<scene::MeshComponent>().addMesh(nullptr);
        ne.meshData = &boxData;
        scene::Node& farNode = scene.root().createChild("farBox");
        farNode.translation = {3.f, 0.f, -6.f};
        scene::MeshEntry& fe = farNode.attach<scene::MeshComponent>().addMesh(nullptr);
        fe.meshData = &boxData;

        TestCam cam; // at (0,0,5) looking at origin
        scene::PickHit hit;
        CHECK(scene::pick(scene, &cam, 100, 100, 49.5f, 49.5f, hit));
        CHECK(hit.hit);
        CHECK(hit.node == &nearNode);
        CHECK(hit.meshEntry == 0);
        // eye z=5, box front face z=-1.5 -> ~6.5 along the ray, minus the near-plane offset
        // (the ray origin sits on the near plane, z=0.1 in front of the eye)
        CHECK(hit.distance > 6.0f && hit.distance < 6.6f);
        CHECK_NEAR(hit.point.z, -1.5f, 1e-3);
        // pick the far box at its screen position (project (3,0,-6))
        const fuhgl::math::Vec4 clip = cam.viewProj(1.f) * fuhgl::math::Vec4{3.f, 0.f, -6.f, 1.f};
        const float px = (clip.x / clip.w * 0.5f + 0.5f) * 100.f - 0.5f;
        const float py = (1.f - (clip.y / clip.w * 0.5f + 0.5f)) * 100.f - 0.5f;
        scene::PickHit hit2;
        CHECK(scene::pick(scene, &cam, 100, 100, px, py, hit2));
        CHECK(hit2.node == &farNode);
        // empty corner: no hit
        scene::PickHit miss;
        CHECK(!scene::pick(scene, &cam, 100, 100, 2.f, 2.f, miss));
        CHECK(!miss.hit);
        // no camera: false
        CHECK(!scene::pick(scene, nullptr, 100, 100, 50.f, 50.f, miss));
    }
}
