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

/// @brief Konstruktionsweise einer Bruecke — gewaehlt aus Spannweite, Wohlstand/Tech-Level
/// und lokalen Ressourcen (Biome) des Brueckenstandorts, siehe BridgeGenerator::analyzeRuns().
enum class BridgeType {
    WoodBeam = 0,   ///< Holzsteg / Balkenbruecke: Quertraeger auf eingerammten Holzpfahlen + Bohlenbelag
    StoneArch = 1,  ///< Steinbogenbruecke: Halbkreis-/Ellipsenboegen mit massiven Pfeilern im Flussbett
    Truss = 2,      ///< Fachwerkbruecke: Stahl-Fachwerk ueber dem Deck (grosse Spannweite / hoher Tech-Level)
    Suspension = 3, ///< Haengebruecke: Pylone an den Ufern + Hauptseile als Kettenlinie (tiefe Schlucht)
};

/// @brief Knobs der Bruecken-Generierung (BridgeGenerator) — alle mit Defaults.
struct BridgeParams {
    // --- Erkennung & Ankerpunkte ---
    float floodMargin = 0.5f;      ///< Hochwasser-Sicherheitsabstand: Ufer-Anker liegen >= Wasserspiegel + dies
    float deckClearance = 0.7f;    ///< lichte Hoehe des Decks ueber dem Wasserspiegel (Welt-Einheiten)
    float maxAnchorSearch = 30.f;  ///< maximale Ufersuche vom Brueckenlauf-Ende (Welt-Einheiten)
    float ravineDepth = 12.f;      ///< ab dieser Tiefe unter dem Deck gilt die Querung als tiefe Schlucht
                                   ///< (oder als Brueckenlauf ueber einem trockenen Tobel)
    float navigableFlow = 0.6f;    ///< flow accumulation ab der ein Fluss als schiffbar gilt (Durchfahrtshoehe)
    float clearanceRise = 1.4f;    ///< parabolische Anhebung der Brueckenmitte bei schiffbaren Fluessen

    // --- Statik-Auswahl (Spannweiten-Schwellen) ---
    float woodSpanMax = 10.f;      ///< Spannweite < 10 m (Dorf/Wald) -> Holzsteg / Balkenbruecke
    float archSpanMax = 60.f;      ///< 10..60 m (Stein-Region) -> Steinbogenbruecke
    float techLevel = 0.5f;        ///< Wohlstand/Tech-Level 0..1 (worldgen: mittlere Siedlungs-Importance)

    // --- Geometrie-Aufbau ---
    float slabThickness = 0.35f;   ///< Deckplatten-Dicke (Welt-Einheiten)
    float parapetHeight = 0.9f;    ///< Brusthoehe (Gelaender/Bruestung) an beiden Deckkanten
    float curbHeight = 0.10f;      ///< Bordstein-Hoehe
    float archHeight = 2.2f;       ///< Stichhoehe der Boegen (Steinbogenbruecke)
    float pierEvery = 6.f;         ///< Pfeiler-Abstand im Flussbett (Bogenbruecke, Welt-Einheiten)
    float pileEvery = 3.f;         ///< Pfahl-Abstand (Holzbruecke)
    float pylonHeight = 7.f;       ///< Pylonhoehe ueber dem Deck (Haengebruecke)
    float cableSag = 1.6f;         ///< Durchhang des Hauptseils in Brueckenmitte (Kettenlinie)
    float hangerEvery = 3.f;       ///< Haenger-Abstand Hauptseil -> Fahrbahn (Haengebruecke)
    float trussHeight = 2.4f;      ///< Fachwerkhoehe ueber dem Deck (Fachwerkbruecke)

    // --- Ufer-Verschmelzung (Widerlager / Terrain-Blending) ---
    float abutmentDepth = 2.5f;    ///< Einbindetiefe der Widerlager-Bloecke in den Hang (Welt-Einheiten)
    float abutmentSize = 2.4f;     ///< Kantenlaenge der massiven Widerlager-Bloecke am Ufer
    float abutmentBlend = 4.f;     ///< weiche Terrain-Aufschuettung am Ufer (Smoothstep-Bereich)
};

/// @brief Ein Brueckenlauf (Spannweite) entlang eines RoadPath: erkannter Lauf ueber Wasser /
/// Schlucht mit Ufer-Ankern, Spannweite, Deck-Profil und gewaehltem Brueckentyp.
/// Wird in RoadGenerator::generate() analysiert und von carveTerrain() (Ufer-Blending) sowie
/// buildGeometry() (Meshes + Props) konsumiert.
struct BridgeSpan {
    std::size_t first = 0, last = 0; ///< inkl. Punktbereich des Decks [first..last] (inkl. beider Anker)
    BridgeType type = BridgeType::WoodBeam; ///< gewaehlte Konstruktionsweise
    float span = 0.f;                ///< Abstand der Ufer-Anker entlang des Weges (Welt-Einheiten)
    float deckY = 0.f;               ///< Deckhoehe an den Ankern (flach; Welt-y)
    float rise = 0.f;                ///< parabolische Mittelerhoehung (schiffbarer Fluss, Welt-Einheiten)
    float maxDepth = 0.f;            ///< tiefster Boden (Raycast) unter dem Deck entlang des Laufs
    bool navigable = false;          ///< Fluss schiffbar -> Durchfahrtshoehe via Parabel-Bogen
    int biome = -1;                  ///< Biome am Lauf-Mittelpunkt (-1 = unbekannt)
    fuhgl::math::Vec3 anchorA{0.f, 0.f, 0.f}; ///< linker Ufer-Anker (erstes stabiles Land ueber Wasser + floodMargin)
    fuhgl::math::Vec3 anchorB{0.f, 0.f, 0.f}; ///< rechter Ufer-Anker
};

/// @brief Knobs of RoadGenerator::generate() — all with sensible defaults.
struct RoadGenParams {
    unsigned seed = 1;

    BridgeParams bridges;           ///< G38 bridge generation knobs (ankerpoints, types, geometry, blending)

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
    std::vector<BridgeSpan> spans;         ///< analyzed bridge runs (anchors, span, type) — see
                                           ///< BridgeGenerator::analyzeRuns(), filled by generate()
    float length() const;
};

/// @brief A prop placed along a road (milestone / lantern / sign / guard wall / bridge
/// entrance props) as local mesh + placement; the caller wraps it into a scene PlacedMesh.
struct RoadProp {
    enum class Kind {
        Milestone, Lantern, Sign, Wall,
        StoneLion,   ///< Steinloewe am Brueckeneingang (Steinbogenbruecke)
        Watchtower,  ///< Wachturm am Brueckeneingang (grosse Bruecken)
        TollHouse,   ///< Zollhaeuschen am Brueckeneingang (Handelsstrassen-Bruecken)
        Bollard      ///< Prellstein am Brueckeneingang (Holzstege)
    } kind = Kind::Milestone;
    fuhgl::math::Vec3 position{0.f, 0.f, 0.f};
    float rotY = 0.f;
    fuhgl::render::MeshData mesh;      ///< local space, base at y = 0
};

/// @brief Result of RoadGenerator::generate() + buildGeometry(): the network as plain data.
struct RoadNetwork {
    std::vector<RoadPath> paths;
    std::vector<RoadProp> props;
    fuhgl::render::MeshData roadMesh;   ///< extruded road profiles (vertex colors per class)
    fuhgl::render::MeshData bridgeMesh; ///< bridge decks + substructure (arches/truss/cables/piles/piers)
                                        ///< + abutments, built by BridgeGenerator (separate mesh: wood/stone/steel)
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
