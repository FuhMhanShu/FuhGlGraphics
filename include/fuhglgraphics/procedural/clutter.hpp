#pragma once

/// @file clutter.hpp
/// @brief G39a: causal micro-scattering (clutter) — "what lies where" is derived from the
/// world data, not from uniform randomness. Design: docs/AMBIENT_LIFE.md.
///
/// Real ground is not evenly scattered: rockfall gathers at the foot of cliffs, riverbeds
/// hold smooth pebbles / driftwood / reeds where the flow map is high, every tree drops
/// litter in an invisible radius around it (branches, cones, bark, mushrooms on the wet
/// north side, leaf piles), and buildings/road edges collect weeds, gravel, roof tiles and
/// crates. Clusters (mushrooms, flowers, pebble nests) never grow alone — poisson-disk
/// sampling gives the minimum distance, a high-frequency noise mask decides where a dense
/// nest forms and where nothing grows at all.
///
/// Pure CPU, deterministic per seed, no GL: the result is plain data (ClutterItem =
/// kind + local mesh + placement) that the caller (worldgen) attaches to the scene.

#include "noise.hpp"

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief What a clutter item is — selects the app-side material (and the scatter rule).
enum class ClutterKind {
    Rockfall = 0, ///< sharp rock at the foot of cliffs (slope map)
    Pebble,       ///< smooth river pebble in channels / shore (flow map high, near water)
    Driftwood,    ///< washed-up log in riverbeds (flow very high)
    Reed,         ///< reed tuft at the waterline (shore band + flow)
    Branch,       ///< broken branch under a tree (parent-child)
    Cone,         ///< pine cone under conifers
    Bark,         ///< bark piece under a tree
    Mushroom,     ///< mushroom cluster on the wet/north side of trees (humidity)
    LeafPile,     ///< leaf litter pile under a tree
    WeedTuft,     ///< weed tuft at road edges / near buildings
    GravelPile,   ///< small gravel heap near buildings / road edges
    RoofTile,     ///< fallen roof tile next to a building
    Crate,        ///< forgotten crate next to a building
};

/// @brief One scattered clutter item: kind + local-space mesh + world placement.
struct ClutterItem {
    ClutterKind kind = ClutterKind::WeedTuft;
    fuhgl::render::MeshData mesh;              ///< local space, base at y = 0
    fuhgl::math::Vec3 position{0.f, 0.f, 0.f}; ///< world position (on the terrain)
    float rotY = 0.f;
    float scale = 1.f;
};

/// @brief World data the causal scatter rules read. All grids are row-major (x + gridW*z);
/// the heightmap is normalized [0,1], `flow` the erosion/river flow accumulation [0,1],
/// `humidity` the optional climate map [0,1] (drives mushrooms on wet sides).
struct ClutterInput {
    const std::vector<float>* heightmap = nullptr;
    int gridW = 0, gridH = 0;
    float sizeX = 0.f, sizeZ = 0.f;   ///< world extent (centered at the origin)
    float heightScale = 1.f;          ///< world y = sample * heightScale
    float waterLevel = 0.f;           ///< normalized; cells below are water
    const std::vector<float>* flow = nullptr;
    const std::vector<float>* humidity = nullptr;
    const std::vector<unsigned char>* biome = nullptr;   ///< optional biome cast (forest etc.)
    const std::vector<float>* roadMask = nullptr;         ///< optional [0,1] road edge mask
    std::vector<fuhgl::math::Vec2> treeSpots;             ///< tree positions + litter radius
    std::vector<float> treeRadii;                         ///< per treeSpots: litter radius
    std::vector<fuhgl::math::Vec2> buildingSpots;         ///< building positions (city/hamlets)
};

/// @brief Knobs of ClutterGenerator::generate().
struct ClutterGenParams {
    unsigned seed = 1;
    int maxItems = 900;               ///< hard cap on the total item count
    float minDist = 0.7f;             ///< poisson-disk minimum distance between items (world)
    // cluster nests: high-frequency noise mask decides where dense nests form
    float clusterNoiseScale = 0.55f;  ///< noise-space scale per world unit
    float clusterThreshold = 0.62f;   ///< fbm above this -> nest allowed here
    // per-kind budgets (0 = off)
    int rockfallCount = 120;          ///< talus at cliff feet
    int pebbleCount = 160;            ///< riverbed/shore pebbles
    int driftwoodCount = 24;          ///< logs in strong channels
    int reedCount = 90;               ///< reeds at the waterline
    int litterPerTree = 4;            ///< branch/cone/bark/leafpile per tree (max)
    int mushroomPerTree = 2;          ///< mushroom clusters per tree (wet sides only)
    int buildingClutterCount = 60;    ///< weeds/gravel/tiles/crates around buildings (own budget)
    int roadEdgeClutterCount = 80;    ///< weed tufts / gravel at the frayed road edges
    float cliffSlope = 0.9f;          ///< world slope from which on = cliff (rockfall)
    float maxSlope = 2.5f;            ///< slope normalization for the rockfall band
};

/// @brief Class-based causal clutter scatterer (stateless apart from the params passed in).
class ClutterGenerator {
public:
    /// @brief Scatters clutter causally from `in` (slope/flow/humidity/biome/road/tree/
    /// building data). Deterministic per params.seed. Pass order: rockfall, riverbed,
    /// reeds, building ring, road rim (each with its own budget), then tree litter fills
    /// the rest up to maxItems. Riverbed/reed candidates are jittered around precomputed
    /// zone cells (high-flow sub-water / waterline band), so the thin features are hit
    /// reliably. Exposed per-rule weight functions make the causality unit-testable.
    std::vector<ClutterItem> generate(const ClutterInput& in, const ClutterGenParams& p) const;

    /// @brief Rockfall weight at (x, z): high on/below cliffs (world slope band).
    static float rockfallWeight(const ClutterInput& in, const ClutterGenParams& p, float x, float z);
    /// @brief Riverbed weight: flow map high + near/below the water level (pebbles, driftwood).
    static float riverbedWeight(const ClutterInput& in, float x, float z);
    /// @brief Waterline weight: shore band with some flow (reeds).
    static float waterlineWeight(const ClutterInput& in, float x, float z);
    /// @brief Distance to the nearest tree (litter spawns only under trees) — 1e9 when none.
    static float treeDistance(const ClutterInput& in, float x, float z);
    /// @brief Distance to the nearest building — 1e9 when none.
    static float buildingDistance(const ClutterInput& in, float x, float z);
    /// @brief Distance to the nearest road edge (road mask rim) — 1e9 when no road mask.
    static float roadEdgeDistance(const ClutterInput& in, float x, float z);
    /// @brief Cluster mask in [0,1]: 0 where the noise says "nothing grows here", else a
    /// nest density (high-freq fbm, remapped through clusterThreshold).
    static float clusterMask(const ClutterGenParams& p, unsigned seed, float x, float z);
};

} // namespace fuhglgraphics::procedural
