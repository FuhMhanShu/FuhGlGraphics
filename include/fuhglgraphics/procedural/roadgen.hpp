#pragma once

/// @file roadgen.hpp
/// @brief G38: organic road/path network generator (Least-Cost Path + spline geometry +
/// terrain blending) — design: docs/ROAD_GENERATION.md.
///
/// Real traffic networks are not straight lines: people and animals follow the path of
/// least resistance. This generator connects sites (settlements, farms, viewpoints) with
/// an A* search whose movement cost is driven by the physics of the terrain:
///
///   1. Route: least-cost path (A* with physics rules) — slope enters the cost
///      quadratically (cost += slopeWeight * slope^2) and explodes above a class-specific
///      critical slope, so steep hillsides automatically produce switchbacks (Serpentinen);
///      water is very expensive except at shallow, narrow spots (ford criteria — those are
///      the preferred bridge locations), and EVERY water crossing is flagged bridge
///      (Bruecke / Steg): roads cross water only as a deck above the water level, never as
///      a sunken ford ribbon; swamp, dense forest and rock cost extra; existing roads get a strong
///      discount so new paths merge into the network instead of running parallel.
///   2. Hierarchy: highways (Handelsstrassen) between the most important sites, country
///      roads (Landstrassen/Dorfwege) from every other site to the network, desire paths
///      (Trampelpfade) from each site to nearby points of interest — each class with its
///      own critical slope, width and minimum curve radius.
///   3. Geometry: the raw A* grid line is smoothed with a Catmull-Rom spline and clamped
///      to the class minimum curve radius, then extruded with a class cross-section
///      (highway: crowned carriageway + shoulders + ditches; country road: two wheel ruts
///      with a grass ridge between; trail: a narrow trodden depression).
///   4. Terrain blending: the road cuts a bench into the uphill side (Hanganschnitt) and
///      fills an embankment on the downhill side (Damm); the road mask has noise-frayed
///      edges (organic transition in the terrain shader), the rut mask marks the wheel
///      ruts (puddles after rain), and props (milestones, lanterns, signs, guard walls)
///      are placed along the spline with jitter.
///
/// Pure CPU, deterministic per seed, no GL. The module is self-contained: it only reads
/// the heightmap/flow/biome grids (biome as raw `unsigned char` codes, so no dependency
/// on worldgen.hpp) and writes plain data + MeshData that the caller (worldgen) attaches
/// to the world: roads carve the terrain, vegetation avoids the road mask but grows as
/// weeds at its frayed edge, creatures can query roadDistanceAt() for fast routes.

#include "noise.hpp"

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Hierarchy level of a road — decides critical slope, width, curve radius, props.
enum class RoadClass {
    Highway = 0, ///< overregional trade road between major settlements (wide, gentle curves)
    Country = 1, ///< Landstrasse / Dorfweg from smaller sites to the network (wheel ruts)
    Trail = 2,   ///< Trampelpfad between houses, to the well / forest edge (steep allowed)
};

/// @brief A point of interest the network connects: settlement, farm, mine, viewpoint.
struct RoadSite {
    fuhgl::math::Vec2 pos{0.f, 0.f}; ///< world XZ
    float importance = 0.5f;         ///< [0,1]; >= RoadGenParams::highwayMinImportance -> highway hub
    int id = 0;                      ///< caller id (settlement index), echoed in RoadPath::fromSite
};

/// @brief Terrain input for the road generator. Heightmap is normalized [0,1], row-major
/// (index = x + gridW*z), world rect centered at the origin. `flow` is the erosion/river
/// flow accumulation [0,1] (ford detection + swamp cost), `biome` the optional per-cell
/// biome cast (worldgen Biome codes: 4 Forest, 5 Jungle, 6 Taiga = dense woods; 8 Rock),
/// `existingMask` an optional [0,1] per-cell mask of already built roads (merge discount).
struct RoadInput {
    const std::vector<float>* heightmap = nullptr;
    int gridW = 0, gridH = 0;
    float sizeX = 0.f, sizeZ = 0.f;  ///< world extent of the heightmap rect
    float heightScale = 1.f;         ///< world y = sample * heightScale
    float waterLevel = 0.f;          ///< normalized; cells below are water
    const std::vector<float>* flow = nullptr;
    const std::vector<unsigned char>* biome = nullptr;
    const std::vector<float>* existingMask = nullptr;
    std::vector<RoadSite> sites;     ///< points of interest to connect
};

/// @brief Knobs of RoadGenerator::generate() — all with sensible defaults.
struct RoadGenParams {
    unsigned seed = 1;

    // --- hierarchy ---
    float highwayMinImportance = 0.7f;  ///< sites with importance >= this become highway hubs
    int desirePathsPerSite = 2;         ///< trails per site to random nearby land POIs
    float desirePathMinLen = 8.f;       ///< desire-path target distance range (world units)
    float desirePathMaxLen = 30.f;

    // --- A* movement costs (per world unit traveled) ---
    float slopeWeight = 26.f;           ///< quadratic slope cost: cost += slopeWeight*slope^2
    float critSlopeHighway = 0.27f;     ///< critical world slope (~15 deg) per class; above it
    float critSlopeCountry = 0.45f;     ///< the cost is multiplied by criticalPenalty, so A*
    float critSlopeTrail = 0.95f;       ///< automatically picks switchbacks on steep slopes
    float criticalPenalty = 90.f;
    float waterCost = 500.f;            ///< per unit across water (forces bridges/detours)
    float fordMaxDepth = 0.025f;        ///< shallow water = preferred bridge location (A* cost)
    float fordMaxFlow = 0.2f;           ///< narrow stream (low flow) = preferred bridge location
    float fordCost = 5.f;               ///< cheap crossing cost at shallow/narrow spots (bridge site)
    float swampCost = 9.f;              ///< wet lowland: high flow near the water level
    float forestCost = 2.2f;            ///< dense woods (biome 4/5/6)
    float rockCost = 5.f;               ///< rock cells (biome 8 / very steep)
    float existingRoadDiscount = 0.85f; ///< cost *= (1 - discount*mask) on existing roads
    int maxExpand = 400000;             ///< A* expansion budget per search

    // --- spline geometry ---
    float minRadiusHighway = 45.f;      ///< minimum curve radius per class (world units)
    float minRadiusCountry = 24.f;
    float minRadiusTrail = 9.f;

    // --- cross-section widths (world units) ---
    float highwayWidth = 6.f;           ///< carriageway width
    float shoulderWidth = 1.6f;         ///< highway shoulder + drainage ditch
    float countryWidth = 3.6f;
    float trailWidth = 1.3f;
    float crownHeight = 0.14f;          ///< highway crown (rain runs off to the sides)
    float rutDepth = 0.09f;             ///< country-road wheel-rut depth
    float rutOffset = 0.3f;             ///< |t| of the ruts, fraction of the half width

    // --- terrain carving (bench / embankment) ---
    float benchCut = 1.2f;              ///< max world units cut into the uphill side
    float embankment = 1.0f;            ///< max world units filled on the downhill side
    float blendMargin = 2.5f;           ///< soft blend margin beyond the road edge (world units)

    // --- props along the spline ---
    float milestoneEvery = 26.f;        ///< distance between milestones (highway/country)
    float lanternEvery = 18.f;          ///< distance between lanterns near settlements
    float lanternRadius = 26.f;         ///< lanterns only within this distance of a site
    float wallMinDrop = 1.6f;           ///< guard wall where the ground drops this far at the edge
    float propJitter = 0.35f;           ///< relative position jitter of the props
};

/// @brief One routed road: smoothed spline in world space (y = terrain height) + per-point
/// bridge flags (1 = the segment there crosses water and is rendered as a bridge /
/// footbridge deck above the water level — roads never run through water at ground level).
struct RoadPath {
    RoadClass cls = RoadClass::Trail;
    int fromSite = -1;                 ///< RoadSite::id of the endpoints (-1 = free POI target)
    int toSite = -1;
    std::vector<fuhgl::math::Vec3> points; ///< smoothed, resampled polyline (world XZ, y = height)
    std::vector<char> bridge;              ///< aligned with `points`
    float length() const;
};

/// @brief A prop placed along a road (milestone / lantern / sign / guard wall) as local
/// mesh + placement; the caller wraps it into a scene PlacedMesh.
struct RoadProp {
    enum class Kind { Milestone, Lantern, Sign, Wall } kind = Kind::Milestone;
    fuhgl::math::Vec3 position{0.f, 0.f, 0.f};
    float rotY = 0.f;
    fuhgl::render::MeshData mesh;      ///< local space, base at y = 0
};

/// @brief Result of RoadGenerator::generate() + buildGeometry(): the network as plain data.
struct RoadNetwork {
    std::vector<RoadPath> paths;
    std::vector<RoadProp> props;
    fuhgl::render::MeshData roadMesh;   ///< extruded road profiles (vertex colors per class)
    fuhgl::render::MeshData bridgeMesh; ///< bridge decks + posts (separate mesh, e.g. wood/stone)
    std::vector<float> mask;            ///< gridW*gridH [0,1] road mask, noise-frayed edges —
                                        ///< terrain-shader edge blending + scatter suppression
    std::vector<float> rutMask;         ///< gridW*gridH [0,1] wheel-rut mask (puddles/dark ruts)
    std::vector<unsigned char> bridgeMask; ///< gridW*gridH cells where a bridge crosses water
    std::vector<float> dist;            ///< gridW*gridH world-unit distance to the nearest road
    int gridW = 0, gridH = 0;
    float sizeX = 0.f, sizeZ = 0.f;
    float heightScale = 1.f;            ///< echo of RoadInput (world y = normalized sample * this)
    float waterLevel = 0.f;             ///< echo of RoadInput (normalized) — carving skips water

    /// @brief World-unit distance to the nearest road centerline at (x, z) (bilinear sample
    /// of `dist`; large when the network is empty) — creature fast-route query.
    float roadDistanceAt(float x, float z) const;
};

/// @brief Class-based road network generator (stateless apart from the params passed in).
/// Usage: generate() routes + stamps the masks, then the caller carves the terrain
/// (carveTerrain — the road bench), then buildGeometry() extrudes the meshes against the
/// carved heightmap and places the props.
class RoadGenerator {
public:
    /// @brief Routes the network between `in.sites` (hierarchy: highways between important
    /// sites -> country roads to the rest -> desire-path trails) and stamps mask/rutMask/
    /// bridgeMask/dist. Does not touch meshes yet. Deterministic per params.seed.
    RoadNetwork generate(const RoadInput& in, const RoadGenParams& p) const;

    /// @brief Cuts the road bench into the uphill side and fills the embankment on the
    /// downhill side of every path (Hanganschnitt/Damm with soft blending). Works on the
    /// normalized [0,1] heightmap (net.heightScale converts the world-y path heights);
    /// water cells and the outermost grid ring are never modified. `heightmap` must be the
    /// same vector as RoadInput::heightmap so the follow-up buildGeometry() sees the bench.
    void carveTerrain(std::vector<float>& heightmap, const RoadNetwork& net,
                      const RoadGenParams& p) const;

    /// @brief Extrudes the class cross-sections along the smoothed paths into
    /// net.roadMesh / net.bridgeMesh and places the props into net.props. Heights are
    /// sampled from `in.heightmap` (call after carveTerrain so the road sits on the bench).
    void buildGeometry(RoadNetwork& net, const RoadInput& in, const RoadGenParams& p) const;

    /// @brief A* least-cost path on the grid between two world points. Returns the cell
    /// indices (row-major x + gridW*z) from `from` to `to`, or empty when the expansion
    /// budget is exhausted. `cls` selects the critical slope; `existingMask` (optional,
    /// [0,1] per cell) gives the merge discount. Exposed for tests.
    static std::vector<int> leastCostPath(const RoadInput& in, const RoadGenParams& p,
                                          RoadClass cls, fuhgl::math::Vec2 from,
                                          fuhgl::math::Vec2 to,
                                          const std::vector<float>& existingMask);

    /// @brief Catmull-Rom resample of a raw polyline at ~`step` spacing + iterative
    /// minimum-curve-radius clamp (the middle point of a too-tight triple is pulled toward
    /// the chord midpoint). Endpoints are preserved. Exposed for tests.
    static std::vector<fuhgl::math::Vec2> smoothPath(const std::vector<fuhgl::math::Vec2>& raw,
                                                     float step, float minRadius);
};

} // namespace fuhglgraphics::procedural
