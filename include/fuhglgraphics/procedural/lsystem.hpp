#pragma once

/// @file lsystem.hpp
/// @brief G26: L-systems (Lindenmayer systems) — string rewriting + 3D turtle interpretation
/// producing `fuhgl::render::MeshData` (tapered cylinders per "draw" segment, optional leaf
/// quads). Classic use: trees, bushes, corals as G9 mesh nodes or instanced vegetation.
/// Deterministic; no GL needed for generation (upload via Mesh::setMeshData).

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <string>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief One L-system: axiom, production rules, iterations + turtle parameters.
/// Turtle symbols: F = draw forward (cylinder), f = move forward, + - = roll around the
/// heading, & ^ = pitch, \\ | = yaw, [ = push frame (branch, radius tapers), ] = pop,
/// L = leaf quad pair at the current frame. The turtle starts with heading +Y, so roll
/// (+ -) alone never changes the drawing direction — combine it with pitch (& ^) or yaw
/// (\\ |) to get real 3D branches (see the presets below).
struct LSystem {
    std::string axiom;
    std::vector<std::pair<char, std::string>> rules; ///< production rules (first match wins)
    int iterations = 4;
    float angleDeg = 25.f;      ///< turn angle for + - & ^ \ |
    float step = 1.f;           ///< F/f segment length (world units)
    float radius0 = 0.09f;      ///< trunk radius at depth 0
    float radiusTaper = 0.68f;  ///< radius multiplier per [ depth level
    int sides = 6;              ///< cylinder sides per segment
    bool leaves = false;        ///< emit leaf quads for 'L'
    float leafSize = 0.55f;     ///< leaf quad half-size

    /// @brief Applies the production rules `iterations` times (deterministic string rewriting).
    std::string derive() const;

    /// @brief 3D turtle interpretation of derive() -> MeshData (11 floats/vertex).
    /// `trunkColor` for segments, `leafColor` for leaf quads (used when leaves=true).
    fuhgl::render::MeshData generate(const fuhgl::math::Vec3& trunkColor = {0.32f, 0.22f, 0.14f},
                                     const fuhgl::math::Vec3& leafColor = {0.16f, 0.42f, 0.18f}) const;

    // --- presets ---------------------------------------------------------------------
    /// @brief Classic branching tree (variant 0..2 select different rule sets).
    static LSystem makeTree(int variant = 0, unsigned seedJitter = 0);
    /// @brief Barnsley-fern-like frond.
    static LSystem makeFern();
    /// @brief Bushy shrub.
    static LSystem makeBush();
};

} // namespace fuhglgraphics::procedural
