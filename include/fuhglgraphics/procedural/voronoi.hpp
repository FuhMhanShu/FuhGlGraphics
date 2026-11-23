#pragma once

/// @file voronoi.hpp
/// @brief G28: 2D Voronoi diagrams — seeded seed points in a rectangle, cells as convex
/// polygons (half-plane clipping against the bisectors, robust for moderate n), adjacency
/// edges, F1/F2 distance fields, and extrusion of cells into prism meshes (`MeshData`) for
/// cracked ground plates, city blocks or crystal shards. Deterministic per seed; pure CPU.

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <utility>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief A 2D Voronoi diagram: seed points + one convex polygon per seed (CCW, clipped to
/// the bounds rect) + adjacency edges (pairs of seed indices sharing a cell edge).
struct Voronoi2D {
    fuhgl::math::Vec2 min{0.f, 0.f}, max{1.f, 1.f};
    std::vector<fuhgl::math::Vec2> points;                 ///< seed points (index = cell id)
    std::vector<std::vector<fuhgl::math::Vec2>> cells;      ///< convex polygon per seed, CCW
    std::vector<std::pair<int, int>> edges;                ///< adjacent seed pairs
};

/// @brief Builds the diagram: `n` seeded points in [min, max], cells clipped by half-plane
/// intersection with every bisector (O(n^2), fine for n up to a few hundred).
Voronoi2D voronoi2d(int n, const fuhgl::math::Vec2& min, const fuhgl::math::Vec2& max, unsigned seed);

/// @brief F1 (nearest seed distance), F2 (second nearest) and the border distance F2-F1
/// at point `p`.
void voronoiDistances(const Voronoi2D& v, const fuhgl::math::Vec2& p, float& f1, float& f2);

/// @brief Extrudes every cell into a prism (bottom at y0, top at y1) -> one MeshData.
/// `gap` shrinks each polygon towards its centroid (cracks between plates), `heightJitter`
/// (with `jitterSeed`) adds per-cell top-height variation (crystals/shards), `colorJitter`
/// varies the per-vertex color per cell. Vertex color = base * (1 ± colorJitter).
fuhgl::render::MeshData extrudeCells(const Voronoi2D& v, float y0, float y1, float gap = 0.f,
                                     float heightJitter = 0.f, unsigned jitterSeed = 0,
                                     const fuhgl::math::Vec3& color = {1.f, 1.f, 1.f},
                                     float colorJitter = 0.15f);

} // namespace fuhglgraphics::procedural
