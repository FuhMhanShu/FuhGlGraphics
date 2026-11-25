#pragma once

/// @file worldgen.hpp
/// @brief World generator helper: one parameter struct (WorldGenParams) + one function
/// (generateWorld) that procedurally builds either a LANDSCAPE (G24 noise heightmap with
/// optional lake basin + scattered G26 L-system trees) or a CITY (G27 WFC street/building
/// layout over a ground plate, optional G28 voronoi district plates). Pure CPU,
/// deterministic per seed, no GL: the result is plain data (heightmap, placements, MeshData)
/// that the app attaches to a scene::Scene (upload via Mesh::setMeshData, place via node
/// transforms — see examples/wfc_screenshot for the scene side).

#include "house.hpp"
#include "lsystem.hpp"
#include "noise.hpp"
#include "voronoi.hpp"
#include "wfc.hpp"

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Street surface material — selects slab color + uv scale in the generator; the app
/// attaches the matching texture set (asphalt / cobblestone with POM parallax / paving stones /
/// dirt-mud). Wet roads (WorldGenParams::wetRoads) are rendered dark + reflective app-side.
enum class RoadSurface { Asphalt = 0, Cobble = 1, Paving = 2, Dirt = 3 };

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
    float lakeRadius = 0.f;       ///< >0: carve a round lake basin of this radius (world units) around the center
    int erosionDroplets = 4000;   ///< hydraulic erosion droplets on the heightmap (0 = off)
    int thermalPasses = 4;        ///< thermal erosion passes (talus-angle relaxation) (0 = off)
    float talus = 0.09f;          ///< thermal talus threshold (normalized height drop per cell; ~35° world slope at default grid/height)
    int treeCount = 120;          ///< scattered L-system trees, poisson-disk sampled (0 = off)
    int bushCount = 160;          ///< scattered L-system bushes (0 = off)
    int rockCount = 40;           ///< scattered rocks on steep/rocky splatmask areas (0 = off)
    int grassBlades = 4000;       ///< hint for the app: instanced grass blades over the terrain (scene-side)
    float treeMinDist = 6.f;      ///< min distance between scatter points (poisson disk)

    // --- landscape shape shaping: spline-mapped noise layers + terracing ---
    float contOcean = 0.40f;      ///< continentalness spline knot: below = deep ocean (h -> 0)
    float contCoast = 0.45f;      ///< knot: coast band up to here (low h)
    float contPlains = 0.70f;     ///< knot: plains up to here, above rises steeply to mountains
    float erosionScale = 0.011f;  ///< noise scale of the erosion map (high value = smooth: meadows/lakes)
    float pvScale = 0.06f;        ///< noise scale of the peaks&valleys detail layer
    float pvWeight = 0.16f;       ///< weight of the peaks&valleys layer (normalized height)
    int terraceSteps = 0;         ///< >0: terrace (quantize) high terrain into steps (0 = off)
    float terraceStrength = 0.65f; ///< terracing blend strength on high terrain

    // --- hydrology: rivers carved by gradient descent from mountain sources ---
    int riverCount = 10;          ///< number of rivers (0 = off); sources at h >= riverSourceMinH
    float riverSourceMinH = 0.55f; ///< min normalized height of a river source cell
    float riverDepth = 0.05f;     ///< riverbed carve depth (normalized) at the channel center
    float riverWidth = 1.6f;      ///< riverbed half-width in grid cells

    // --- climate / biomes (Whittaker lookup over temperature + humidity) ---
    bool climateMaps = true;      ///< compute temperature/humidity/biome maps (landscape)
    float tempNoiseScale = 0.008f;    ///< noise scale of the temperature variation layer
    float humidityNoiseScale = 0.013f; ///< noise scale of the humidity layer
    float lapseRate = 0.5f;       ///< temperature drop per normalized altitude (mountains are cold)
    float humidityRiverBoost = 0.5f;  ///< humidity boost along flow channels (rivers)

    // --- city (mode == City) ---
    int cityGrid = 12;            ///< WFC cells per side
    float cellSize = 8.f;         ///< world size of one city cell
    float buildingHeight = 6.f;   ///< base wall height of placed houses (scaled per houseStyle, jittered per block)
    float houseFootprint = 0.62f; ///< house footprint as a fraction of the cell
    HouseStyle houseStyle = HouseStyle::Town; ///< architectural style of the houses
    int blocksPerDistrict = 0;    ///< >0: extrude this many voronoi district plates under the city
    float parkFraction = 0.12f;   ///< fraction of interior block cells turned into parks (lawn + bushes)
    int parkBushesPerPlaza = 3;   ///< bushes scattered on each park cell
    int maxBacktracks = 20000;    ///< WFC solver backtrack budget
    RoadSurface roadSurface = RoadSurface::Asphalt; ///< street slab material (texture app-side)
    float roadIrregularity = 0.06f; ///< edge jitter of the road slabs in world units (0 = straight)
    float roadDamage = 0.35f;     ///< probability of a pothole (dark patch) per street cell
    bool wetRoads = false;        ///< wet streets: render with low roughness + reflections
    bool allowPathCuts = false;   ///< false (default): plaza tiles are dropped and road fragments
                                  ///< outside the largest connected component become blocks, so
                                  ///< paths/parks never interrupt the streets; true: any valid WFC
                                  ///< solve is accepted (plaza tiles allowed, roads may fragment)
};

/// @brief Landscape biome from the Whittaker-style 2D lookup over (temperature, humidity)
/// with shore / slope / altitude overrides. Stored per cell in GeneratedWorld::biome.
enum class Biome : unsigned char {
    Ocean = 0, Shore, Desert, Grassland, Forest, Jungle, Taiga, Tundra, Rock, Snow
};

/// @brief Whittaker-style biome lookup over (temperature, humidity) only (no terrain
/// overrides). Shared by the landscape climate maps and the city generators, which feed the
/// Biome cast into BuildingContext::biome for the HouseGenerator material rules.
Biome biomeFromClimate(float temperature, float humidity);

/// @brief One placed mesh: local-space MeshData + a world placement (position / rotY / scale)
/// + a tint. Attach as a scene node (translation/rotation/scale) with the mesh uploaded.
struct PlacedMesh {
    fuhgl::render::MeshData mesh;          ///< local-space triangle data (11 floats/vertex)
    fuhgl::render::MeshData roofMesh;      ///< optional roof part (city houses) — own MeshEntry, e.g. shingle texture
    fuhgl::render::MeshData detailMesh;    ///< optional details (city houses: door/windows/chimney/bay) — own MeshEntry, plain vertex colors
    fuhgl::render::MeshData glassMesh;     ///< optional window glass (city houses) — own MeshEntry with a reflective material
    int roofType = 0;                      ///< city houses: cast of procedural::RoofType (0 gable, 1 lean-to, 2 hipped, 3 flat)
    int facade = 0;                        ///< city houses: cast of procedural::FacadeMaterial (0 brick, 1 plaster, 2 stone) — texture choice app-side
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
    fuhgl::render::MeshData roads;         ///< street slabs only (irregular edges, chosen surface,
                                           ///< pothole patches) — the textured mesh, app attaches the
                                           ///< road texture set (cobble gets POM) + wet material here
    fuhgl::render::MeshData pavements;     ///< sidewalk underlay + lot plates + park lawn slabs (plain vertex colors)
    fuhgl::render::MeshData districts;     ///< voronoi district plates (empty when blocksPerDistrict == 0)
    RoadSurface roadSurface = RoadSurface::Asphalt; ///< echo of WorldGenParams::roadSurface (texture choice)
    bool wetRoads = false;                 ///< echo of WorldGenParams::wetRoads

    // --- placed objects ---
    std::vector<PlacedMesh> trees;         ///< landscape: scattered L-system trees
    std::vector<PlacedMesh> bushes;        ///< landscape: bushes; city: park greenery
    std::vector<PlacedMesh> rocks;         ///< landscape: scattered rocks (splatmap rock/slope)
    std::vector<PlacedMesh> buildings;     ///< city: one procedural house per non-park "block" cell (mesh = walls+door+windows, roofMesh = gabled roof)
    int parkCellCount = 0;                 ///< city: number of cells turned into parks (plaza + converted blocks)

    // --- landscape splatmap (mode == Landscape) ---
    /// @brief gridW*gridH RGBA weights per cell: R = grass, G = rock (slope band relative to the
    /// steepest slope in the terrain), B = sand (shore + flow channels from the hydraulic
    /// erosion), A = snow (altitude). Upload as a texture and use as the terrain albedo/splat map.
    std::vector<unsigned char> splatmap;
    /// @brief Flow accumulation of the hydraulic erosion + carved rivers (normalized [0,1]) —
    /// riverbeds/wet areas.
    std::vector<float> flowmap;
    /// @brief gridW*gridH temperature in [0,1]: latitude (equator at the center) + low-freq
    /// noise - altitude * WorldGenParams::lapseRate. Landscape only.
    std::vector<float> temperature;
    /// @brief gridW*gridH humidity in [0,1]: low-freq noise + boost near water + along rivers.
    /// Landscape only.
    std::vector<float> humidity;
    /// @brief gridW*gridH Biome cast per cell (Whittaker lookup over temperature/humidity with
    /// shore/slope/altitude overrides). Landscape only.
    std::vector<unsigned char> biome;
    /// @brief gridW*gridH pond mask: 1 = mountain-lake cell (a river sink filled to its spill
    /// point). Render as wet mud / place small water discs app-side. Landscape only.
    std::vector<unsigned char> pondMask;
};

/// @brief City tile set for the WFC solver: edge tags "r" = road, "s" = lot/sidewalk edge
/// (also the border tag, so the city ring is closed by blocks), "p" = plaza. Names:
/// "block" (building lot), "plaza", "streetH"/"streetV", "cross", "tE" (T junction, road
/// to E/S/W) and "endE" (dead end to E) — rotations cover the symmetric variants.
std::vector<WfcTile> makeCityTiles();

/// @brief True when all road tiles (tags containing "r") of a solved WFC grid form at most one
/// connected component through shared "r" edges — i.e. no path/park interrupts the streets.
bool wfcRoadsConnected(const std::vector<WfcCell>& cells, const std::vector<WfcTile>& tiles,
                       int gridW, int gridH);

/// @brief Slab color + uv scale per road surface; wet roads are darker (the app renders them
/// with low roughness + reflections).
void roadSurfaceStyle(RoadSurface s, bool wet, fuhgl::math::Vec3& color, float& uvScale);

/// @brief Generates a whole world from `params` (landscape or city, see WorldGenParams::mode).
/// Deterministic per seed; pure CPU, no GL.
GeneratedWorld generateWorld(const WorldGenParams& params);

} // namespace fuhglgraphics::procedural
