#pragma once

/// @file sdf.hpp
/// @brief G29: signed distance fields (SDF) — primitives (sphere, box, rounded box, plane,
/// cylinder), smooth-min combos and 3D fractals (Mandelbulb, DE) — plus `meshFromSdf()`,
/// which converts any SDF into a `fuhgl::render::MeshData` via the G25 marching cubes.
/// Typical use: emissive fractal sculptures rendered with bloom/god rays. Pure CPU, no GL.

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <functional>

namespace fuhglgraphics::procedural {

/// @brief SDF primitives/operators (negative inside). All take world-space points.
namespace sdf {

float sphere(const fuhgl::math::Vec3& p, float r);
float box(const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& halfExtents);
float roundedBox(const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& halfExtents, float r);
float plane(const fuhgl::math::Vec3& p, float height);        ///< ground plane y = height
float cylinder(const fuhgl::math::Vec3& p, float radius, float height); ///< along +y, base at y=0
/// @brief Smooth minimum (polynomial smin): <= min(a, b), blends within k.
float smoothMin(float a, float b, float k);
/// @brief Mandelbulb distance estimator (power 8 classic), bailout 2, 8-12 iterations.
float mandelbulb(const fuhgl::math::Vec3& p, float power = 8.f, int iterations = 10);

} // namespace sdf

/// @brief Meshes an SDF (negative inside) over the box [min, max] with `resolution`^3 samples
/// via G25 marching cubes at isolevel 0. Returns the indexed MeshData (empty on degenerate).
fuhgl::render::MeshData meshFromSdf(const std::function<float(const fuhgl::math::Vec3&)>& field,
                                    const fuhgl::math::Vec3& min, const fuhgl::math::Vec3& max,
                                    int resolution,
                                    const fuhgl::math::Vec3& color = {1.f, 1.f, 1.f});

} // namespace fuhglgraphics::procedural
