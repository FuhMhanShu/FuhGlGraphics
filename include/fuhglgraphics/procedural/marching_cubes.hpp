#pragma once

/// @file marching_cubes.hpp
/// @brief G25: marching cubes — extract an iso-surface triangle mesh from a sampled
/// scalar volume (voxels). Classic 33-case marching cubes (Lorensen & Cline) with linear
/// edge interpolation and gradient normals, output as `fuhgl::render::MeshData`
/// (11 floats/vertex: pos, normal, color, uv) ready for `Mesh::setMeshData`.
/// A chunked variant splits large volumes into several MeshData chunks (seams welded
/// because all chunks sample the same world-space grid). Used by G29 (SDF meshing).

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <functional>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief A sampled scalar volume: values on a regular nx x ny x nz lattice over the
/// world-space box [min, max]. Index order: x + nx * (y + ny * z).
struct VolumeGrid {
    int nx = 0, ny = 0, nz = 0;
    fuhgl::math::Vec3 min{0.f, 0.f, 0.f}, max{1.f, 1.f, 1.f};
    std::vector<float> values; ///< nx*ny*nz samples

    bool ok() const { return nx > 1 && ny > 1 && nz > 1 && values.size() == size(); }
    std::size_t size() const { return static_cast<std::size_t>(nx) * ny * nz; }
    std::size_t index(int x, int y, int z) const {
        return static_cast<std::size_t>(x) + static_cast<std::size_t>(nx) *
               (static_cast<std::size_t>(y) + static_cast<std::size_t>(ny) * z);
    }
    float at(int x, int y, int z) const { return values[index(x, y, z)]; }
    /// @brief World position of lattice point (x, y, z).
    fuhgl::math::Vec3 pos(int x, int y, int z) const {
        return {min.x + (max.x - min.x) * x / (nx - 1),
                min.y + (max.y - min.y) * y / (ny - 1),
                min.z + (max.z - min.z) * z / (nz - 1)};
    }
};

/// @brief Samples `field(worldPos)` into a VolumeGrid over [min, max] with nx*ny*nz points.
VolumeGrid sampleField(const std::function<float(const fuhgl::math::Vec3&)>& field,
                       const fuhgl::math::Vec3& min, const fuhgl::math::Vec3& max,
                       int nx, int ny, int nz);

/// @brief Classic marching cubes over `grid` at `isolevel` (a point is "inside" when its
/// value < isolevel). Vertices on crossing edges are linearly interpolated; normals come
/// from the field gradient (central differences) interpolated the same way. Vertices are
/// shared between adjacent triangles (indexed mesh). `color` is written per vertex.
fuhgl::render::MeshData marchingCubes(const VolumeGrid& grid, float isolevel,
                                      const fuhgl::math::Vec3& color = {1.f, 1.f, 1.f});

/// @brief Chunked marching cubes: splits the lattice into chunksX*chunksY*chunksZ chunks
/// (each chunk gets one extra shared boundary layer, so chunk seams coincide exactly).
/// Returns one MeshData per non-empty chunk.
std::vector<fuhgl::render::MeshData> marchingCubesChunked(const VolumeGrid& grid, float isolevel,
                                                           int chunksX, int chunksY, int chunksZ,
                                                           const fuhgl::math::Vec3& color = {1.f, 1.f, 1.f});

} // namespace fuhglgraphics::procedural
