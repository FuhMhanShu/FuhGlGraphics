#pragma once

/// @file bridgegen.hpp
/// @brief G38 bridge generation (BridgeGenerator) — the bridge half of the road network:
/// detection of bridge runs over water / deep ravines with shore anchors (Widerlager points),
/// span measurement, bridge-type selection from span + tech level + local biome, the
/// three-level geometry along the road spline (deck with curbs/gutters/parapets, substructure
/// of arches / truss / catenary cables, piers raycast down to the riverbed), abutment blocks
/// and shore terrain blending, plus entrance props (stone lions, watchtower, toll house,
/// bollards). Design: docs/ROAD_GENERATION.md section 5.
///
/// Pure CPU, deterministic per seed, no GL. The module only reads the heightmap/flow/biome
/// grids via RoadInput and writes MeshData + RoadProp — the same data flow as RoadGenerator;
/// RoadGenerator::generate() calls analyzeRuns() once per path (the spans travel inside
/// RoadPath::spans), carveTerrain() calls blendShoreTerrain(), buildGeometry() calls
/// buildSpan() per span.

#include "roadgen.hpp"

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Bridge generator: analyzes road paths into BridgeSpan runs and builds their
/// geometry. Stateless apart from the params passed in (same style as RoadGenerator).
class BridgeGenerator {
public:
    /// @brief Analyzes all bridge runs of `path` (contiguous bridge-flagged points, extended
    /// to the shore anchors) and returns them ordered along the path. An anchor is the first
    /// point beyond the run whose terrain is >= waterLevel + floodMargin (Hochwasser-Abstand);
    /// the span is the arc distance between the two anchors. Sets deckY (flat deck at the
    /// anchors, at least water + deckClearance), a parabolic rise when the river is navigable
    /// (flow >= navigableFlow at the run center), maxDepth (deepest ground below the deck) and
    /// the BridgeType from span / techLevel / biome. Deterministic; exposed for tests.
    static std::vector<BridgeSpan> analyzeRuns(const RoadPath& path, const RoadInput& in,
                                               const RoadGenParams& p);

    /// @brief Deck height (world y) at normalized position `s` in [0,1] along the span:
    /// flat at the anchors + parabolic arch (Bogen-Spline) rise in the middle when navigable.
    static float deckHeightAt(const BridgeSpan& span, float s);

    /// @brief Builds one bridge span into `bridgeMesh`: the deck slab (extruded cross-section
    /// with Bordsteinen, Entwaesserungsrinnen and Gelaender/Brüstungen on both sides), the
    /// substructure per type (wood piles + cross beams / stone arches + piers in the riverbed /
    /// steel truss / pylons + catenary main cables + hangers), the two abutment blocks sunk
    /// into the banks, and the entrance props into `props`. Piers/piles are raycast straight
    /// down to the ground (heightmap) so they always reach the riverbed exactly.
    void buildSpan(fuhgl::render::MeshData& bridgeMesh, std::vector<RoadProp>& props,
                   const RoadPath& path, const RoadInput& in, const RoadGenParams& p,
                   const BridgeSpan& span, std::uint32_t seed) const;

    /// @brief Shore terrain blending (Terrain-Aufschuettung): land cells near the abutments
    /// are smoothly pulled up to the deck approach height (smoothstep falloff over
    /// BridgeParams::abutmentBlend) so no edges/gaps remain at the bridge heads. Water cells
    /// and the outermost grid ring are never touched. Called from RoadGenerator::carveTerrain().
    static void blendShoreTerrain(std::vector<float>& heightmap, const RoadNetwork& net,
                                  const RoadGenParams& p);
};

} // namespace fuhglgraphics::procedural
