#pragma once

/// @file picking.hpp
/// @brief G18 — picking / ray-casting on the G9 scene graph. Pure CPU math (no GL):
/// a world-space ray from the camera through a pixel, slab tests against node world AABBs
/// and Möller–Trumbore triangle tests against the MeshEntry's CPU `meshData` (set e.g. by
/// the glTF loader). `pick()` returns the nearest hit (node, mesh entry, distance, point,
/// triangle). Entries without `meshData` are skipped (their node AABB still culls).
/// Basis for editor selection/gizmos (G2/G11/G12).

#include <fuhgl/cam/camera.hpp>
#include <fuhgl/math.hpp>

#include <cstdint>

namespace fuhglgraphics::scene {

class Scene;
class Node;

/// @brief World-space ray (origin + normalized direction).
struct Ray {
    fuhgl::math::Vec3 origin{0.f, 0.f, 0.f};
    fuhgl::math::Vec3 dir{0.f, 0.f, -1.f};
};

/// @brief Ray from the camera through pixel (`px`, `py`) of a `width` x `height` viewport
/// (py measured from the top, pixel centers at +0.5). Pure math — unit-testable.
Ray rayFromScreen(const fuhgl::Camera& cam, float aspect, int width, int height, float px, float py);

/// @brief Slab test of a ray against an AABB. Returns false on miss; `tOut` = entry distance
/// along the (normalized) ray direction on hit.
bool rayIntersectAabb(const Ray& r, const fuhgl::math::Vec3& mn, const fuhgl::math::Vec3& mx,
                      float& tOut);

/// @brief Möller–Trumbore ray/triangle test. On hit returns true and the distance `t` plus
/// the barycentric coordinates (u, v).
bool rayIntersectTriangle(const Ray& r, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
                          const fuhgl::math::Vec3& c, float& t, float& u, float& v);

/// @brief Result of a scene pick.
struct PickHit {
    bool hit = false;
    const Node* node = nullptr;   ///< node carrying the hit mesh entry
    int meshEntry = -1;           ///< index into the node's MeshComponent::meshes
    float distance = 0.f;         ///< world-space distance along the ray
    fuhgl::math::Vec3 point{};    ///< world-space hit point
    std::uint32_t triangle = 0;   ///< triangle index within the entry's meshData
};

/// @brief Picks the nearest scene object under pixel (`px`, `py`): updates the world
/// transforms, ray-tests node AABBs (culling) and the entries' `meshData` triangles in
/// world space. Returns true and fills `out` on hit. `camera` nullptr -> false.
bool pick(Scene& scene, const fuhgl::Camera* camera, int width, int height, float px, float py,
          PickHit& out);

} // namespace fuhglgraphics::scene
