#pragma once

/// @file citygen.hpp
/// @brief Pipeline-based procedural city generator (terrain & zones -> road graph -> lot
/// subdivision -> buildings), pure CPU, deterministic per seed, no GL:
///
///   1. terrain + zones: fBm heightmap (G24 Noise), flattened city plateau, lake below
///      `waterLevel`; zone grids `density` (high-rise vs. suburb) and `wealth` from extra
///      noise layers.
///   2. road network: a warped street grid stored as a planar graph (nodes = intersections,
///      edges = street segments); nodes/edges under water or outside the plateau are dropped.
///   3. blocks + lots: each surviving grid cell is a block polygon; blocks are recursively
///      OBB-split (half-plane split along the longest PCA axis) into lots; lots touching the
///      block boundary get road access, interior lots become parks/parking.
///   4. buildings: per accessible lot a HouseGenerator house (style from the density zone);
///      lots are triangulated with ear clipping for their ground slabs.
///
/// 2D polygon helpers (ear-clipping triangulation, half-plane clip, OBB) are self-contained —
/// no external polygon library needed.

#include "house.hpp"
#include "noise.hpp"
#include "worldgen.hpp" // PlacedMesh

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Parameters of generateCityWorld().
struct CityGenParams {
    unsigned seed = 1;
    float worldSize = 240.f;      ///< X/Z extent (centered at origin)
    int heightGrid = 129;          ///< heightmap/zone grid resolution
    float heightScale = 30.f;      ///< mountain height in world units
    float noiseScale = 0.045f;
    float warp = 0.8f;             ///< domain warp of the height field
    float waterLevel = 0.28f;      ///< normalized [0,1]; terrain below -> lake
    float cityRadius = 95.f;       ///< radius of the flattened city plateau around the center
    int roadGrid = 9;              ///< intersections per axis (roadGrid^2 nodes before masking)
    float roadWidth = 3.2f;        ///< street width in world units
    float lotMaxSize = 13.f;       ///< OBB split stops when lots are smaller than this
    float lotMinSize = 5.f;        ///< lots smaller than this are merged away (become park)
    float houseCoverage = 0.8f;    ///< house footprint as a fraction of the lot
    RoadSurface roadSurface = RoadSurface::Asphalt; ///< street material (texture app-side; cobble gets POM)
    float roadIrregularity = 0.08f; ///< edge jitter of the street quads in world units (0 = straight)
    float roadDamage = 0.25f;      ///< probability of a pothole (dark patch) per road edge
    bool wetRoads = false;         ///< wet streets: render with low roughness + reflections
    bool allowPathCuts = false;    ///< false: interior park lots never touch the road graph (roads
                                   ///< stay uninterrupted); true: parks may reach the street edge
};

/// @brief Planar road graph: nodes are intersections (XZ), edges are street segments.
struct RoadGraph {
    std::vector<fuhgl::math::Vec2> nodes;
    std::vector<std::pair<int, int>> edges;
};

/// @brief One building lot (convex polygon, CCW) with its build decision.
struct Lot {
    std::vector<fuhgl::math::Vec2> poly; ///< CCW polygon in world XZ
    bool hasAccess = false;              ///< touches a block boundary (= road)
    bool park = false;                   ///< interior lot -> park/parking instead of a house
};

/// @brief Result of generateCityWorld().
struct CityWorld {
    // stage 1: terrain + zones
    std::vector<float> heightmap;  ///< gridW*gridH, row-major (x + gridW*z), [0,1]
    std::vector<float> density;    ///< same layout, [0,1] — high near the center -> highrises
    std::vector<float> wealth;     ///< same layout, [0,1]
    int gridW = 0, gridH = 0;
    float sizeX = 0.f, sizeZ = 0.f, heightScale = 1.f, waterLevelWorld = 0.f;
    float heightAt(float x, float z) const;   ///< bilinear, clamped (world XZ -> world Y)
    float densityAt(float x, float z) const;
    float wealthAt(float x, float z) const;

    // stage 2: roads
    RoadGraph roads;
    fuhgl::render::MeshData roadMesh;   ///< sidewalk underlay + street quads (irregular edges,
                                        ///< chosen surface, pothole patches) + intersection squares
    RoadSurface roadSurface = RoadSurface::Asphalt; ///< echo of CityGenParams::roadSurface
    bool wetRoads = false;                 ///< echo of CityGenParams::wetRoads

    // stage 3: blocks -> lots
    std::vector<Lot> lots;
    fuhgl::render::MeshData lotMesh;    ///< one slab per lot (ear-clipped), parks tinted green

    // stage 4: buildings
    std::vector<PlacedMesh> houses;     ///< HouseGenerator output per accessible lot
};

// --- self-contained 2D polygon helpers ----------------------------------------------------

/// @brief Ear-clipping triangulation of a simple polygon (CCW) -> index triples into `poly`.
std::vector<std::uint32_t> earClip(const std::vector<fuhgl::math::Vec2>& poly);

/// @brief Clips `poly` (CCW) to the half-plane left of the directed line a->b (Sutherland-
/// Hodgman against one bisector-style edge).
std::vector<fuhgl::math::Vec2> clipHalfPlane(const std::vector<fuhgl::math::Vec2>& poly,
                                             const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b);

/// @brief 2D oriented bounding box of a point set: center, axes (unit), half extents.
struct Obb2 {
    fuhgl::math::Vec2 center{0.f, 0.f};
    fuhgl::math::Vec2 axisU{1.f, 0.f}; ///< longest axis
    fuhgl::math::Vec2 axisV{0.f, 1.f};
    float halfU = 0.f, halfV = 0.f;
};
Obb2 computeObb(const std::vector<fuhgl::math::Vec2>& pts);

/// @brief Generates the whole pipeline city. Deterministic per seed; pure CPU, no GL.
CityWorld generateCityWorld(const CityGenParams& params);

} // namespace fuhglgraphics::procedural
