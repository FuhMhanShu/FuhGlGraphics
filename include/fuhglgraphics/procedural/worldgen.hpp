#pragma once

/// @file worldgen.hpp
/// @brief World generator helper: one parameter struct (WorldGenParams) + one function
/// (generateWorld) that procedurally builds either a LANDSCAPE (G24 noise heightmap with
/// optional lake basin + scattered G26 L-system trees) or a CITY (G27 WFC street/building
/// layout over a ground plate, optional G28 voronoi district plates). Pure CPU,
/// deterministic per seed, no GL: the result is plain data (heightmap, placements, MeshData)
/// that the app attaches to a scene::Scene (upload via Mesh::setMeshData, place via node
/// transforms — see examples/wfc_screenshot for the scene side).

#include "lsystem.hpp"
#include "noise.hpp"
#include "voronoi.hpp"
#include "wfc.hpp"

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Parameters of generateWorld(). `mode` switches between Landscape and City;
/// all other members are plain knobs with sensible defaults.
struct WorldGenParams {
    enum class Mode { Landscape, City };

    Mode mode = Mode::Landscape;  ///< what generateWorld() builds
    unsigned seed = 1;            ///< same seed -> identical world on every platform

    // --- shared ---
    float worldSize = 200.f;      ///< X/Z extent of the world in world units (centered at origin)

    // --- landscape (mode == Landscape) ---
    int terrainGrid = 129;        ///< heightmap resolution (grid x grid samples)
    float terrainHeight = 18.f;   ///< height scale in world units (heightmap is stored in [0,1])
    int octaves = 6;              ///< fBm octaves of the height field
    float noiseScale = 0.045f;    ///< noise-space scale per world unit
    float warpStrength = 0.f;     ///< domain-warp strength (0 = off)
    float heightExponent = 0.8f;  ///< contrast shaping of the height field (pow(h, exponent); <1 raises peaks)
    float waterLevel = 0.f;       ///< normalized [0,1] water level; >0 carves a lake basin below it
    float lakeCarve = 0.18f;      ///< basin depth factor for samples below waterLevel
    int treeCount = 120;          ///< scattered L-system trees (0 = off)
    int bushCount = 160;          ///< scattered L-system bushes (0 = off)
    int grassBlades = 4000;       ///< hint for the app: instanced grass blades over the terrain (scene-side)
    float treeMinDist = 6.f;      ///< min distance between trees (rejection sampling)

    // --- city (mode == City) ---
    int cityGrid = 12;            ///< WFC cells per side
    float cellSize = 8.f;         ///< world size of one city cell
    float buildingHeight = 6.f;   ///< base wall height of placed houses (jittered per block)
    float houseFootprint = 0.62f; ///< house footprint as a fraction of the cell
    int blocksPerDistrict = 0;    ///< >0: extrude this many voronoi district plates under the city
    float parkFraction = 0.12f;   ///< fraction of interior block cells turned into parks (lawn + bushes)
    int parkBushesPerPlaza = 3;   ///< bushes scattered on each park cell
    int maxBacktracks = 20000;    ///< WFC solver backtrack budget
};

/// @brief One placed mesh: local-space MeshData + a world placement (position / rotY / scale)
/// + a tint. Attach as a scene node (translation/rotation/scale) with the mesh uploaded.
struct PlacedMesh {
    fuhgl::render::MeshData mesh;          ///< local-space triangle data (11 floats/vertex)
    fuhgl::render::MeshData roofMesh;      ///< optional second part (city houses: gabled roof) — own MeshEntry, e.g. shingle texture
    fuhgl::math::Vec3 position{0.f, 0.f, 0.f}; ///< world position
    float rotY = 0.f;                      ///< rotation around +Y in radians
    float scale = 1.f;
    fuhgl::math::Vec3 tint{1.f, 1.f, 1.f}; ///< multiplies the vertex colors
};

/// @brief Result of generateWorld(): plain CPU data, no GL objects.
struct GeneratedWorld {
    WorldGenParams::Mode mode = WorldGenParams::Mode::Landscape;
    unsigned seed = 0;

    // --- landscape terrain (mode == Landscape) ---
    std::vector<float> heightmap;          ///< gridW*gridH samples, row-major (index = x + gridW*z), [0,1]
    int gridW = 0, gridH = 0;              ///< heightmap resolution
    float sizeX = 0.f, sizeZ = 0.f;        ///< world extent (terrain centered at the origin)
    float heightScale = 1.f;               ///< world-unit height = sample * heightScale
    float waterLevelWorld = 0.f;           ///< waterLevel * heightScale (0 = no lake)

    /// @brief World-space terrain height at (x, z): bilinear heightmap sample * heightScale,
    /// clamped to the terrain rect. Matches scene::TerrainComponent::heightAt semantics.
    float heightAt(float x, float z) const;

    // --- city layout (mode == City) ---
    std::vector<WfcCell> cells;            ///< cityW*cityH WFC placements, row-major (x + cityW*z)
    std::vector<WfcTile> tiles;            ///< tile set used (WfcCell::tile indexes into this)
    int cityW = 0, cityH = 0;              ///< WFC grid size
    float cityCellSize = 0.f;              ///< world size of one cell
    fuhgl::render::MeshData cityGround;    ///< base plate under the whole city (top at y = 0)
    fuhgl::render::MeshData roads;         ///< merged slabs: sidewalk underlay + asphalt road + park lawn (plaza cells)
    fuhgl::render::MeshData districts;     ///< voronoi district plates (empty when blocksPerDistrict == 0)

    // --- placed objects ---
    std::vector<PlacedMesh> trees;         ///< landscape: scattered L-system trees
    std::vector<PlacedMesh> bushes;        ///< landscape: bushes; city: park greenery
    std::vector<PlacedMesh> buildings;     ///< city: one procedural house per non-park "block" cell (mesh = walls+door+windows, roofMesh = gabled roof)
    int parkCellCount = 0;                 ///< city: number of cells turned into parks (plaza + converted blocks)
};

/// @brief City tile set for the WFC solver: edge tags "r" = road, "s" = lot/sidewalk edge
/// (also the border tag, so the city ring is closed by blocks), "p" = plaza. Names:
/// "block" (building lot), "plaza", "streetH"/"streetV", "cross", "tE" (T junction, road
/// to E/S/W) and "endE" (dead end to E) — rotations cover the symmetric variants.
std::vector<WfcTile> makeCityTiles();

/// @brief Generates a whole world from `params` (landscape or city, see WorldGenParams::mode).
/// Deterministic per seed; pure CPU, no GL.
GeneratedWorld generateWorld(const WorldGenParams& params);

} // namespace fuhglgraphics::procedural
