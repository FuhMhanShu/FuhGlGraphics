#pragma once

/// @file house.hpp
/// @brief HouseGenerator: a class-based, highly variadic procedural house generator.
/// One call `generate(params)` builds a complete house as three mesh parts (walls / details /
/// roof) plus a spec describing what was built. Variants per style (Village / Town / Highrise):
/// roof types gable (Satteldach), lean-to (Schleppdach), hipped (Walmdach) and flat; features
/// bay window (Erker), dormers (Dachgauben), side annex (Anbau), porch canopy (Vordach),
/// chimney, timber framing (Fachwerk) and highrise window grids with setbacks. Pure CPU,
/// deterministic per seed, no GL.

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>

namespace fuhglgraphics::procedural {

/// @brief Architectural style of a generated house (also used by WorldGenParams::houseStyle).
enum class HouseStyle { Village, Town, Highrise };

/// @brief Roof shape of a generated house.
enum class RoofType { Gable = 0, Shed = 1, Hipped = 2, Flat = 3 };

/// @brief Facade material — selects the geometry detail level and tells the app which
/// texture/POM set to attach to the walls (brick = Backstein POM, plaster = Rauhputz,
/// stone = Naturstein with relief courses).
enum class FacadeMaterial { Brick = 0, Plaster = 1, Stone = 2 };

/// @brief What was built — introspection for tests/apps (which variants fired).
struct HouseSpec {
    RoofType roofType = RoofType::Gable;
    FacadeMaterial facade = FacadeMaterial::Brick;
    int floors = 1;
    bool hasBay = false;       ///< bay window (Erker)
    bool hasAnnex = false;     ///< side annex (Anbau)
    bool hasPorch = false;     ///< porch canopy over the door (Vordach)
    bool hasDormers = false;   ///< dormers (Dachgauben) on the roof slope
    bool hasChimney = false;
    bool timberFrame = false;  ///< Fachwerk beams on the facade (village)
    bool hasGutter = false;    ///< Dachrinne along the eaves + downpipe
    bool hasShutters = false;  ///< Fensterläden beside the windows
    bool hasGarage = false;    ///< Garage (side annex with roller door)
    bool hasBalcony = false;   ///< balcony with balcony door on an upper floor
    bool wallIrregular = false;///< walls got a slight per-vertex jitter (bump-like relief)
    float width = 0.f, depth = 0.f, wallHeight = 0.f;
};

/// @brief The generated house: mesh parts so the app can give each its own material
/// (walls: facade POM set per FacadeMaterial; glass: reflective window panes; details: plain
/// vertex colors; roof: shingle texture).
struct HouseMesh {
    fuhgl::render::MeshData walls;    ///< wall boxes + annex + garage + setback (target for facade POM)
    fuhgl::render::MeshData details;  ///< door, window frames, shutters, chimney, bay, dormer fronts, timber frame, gutters, balcony railing, garage door
    fuhgl::render::MeshData roof;     ///< roof solid(s): main roof, annex roof, dormer roofs, porch, garage roof
    fuhgl::render::MeshData glass;    ///< window glass panes (own MeshEntry — reflective material)
    HouseSpec spec;
};

/// @brief Class-based variadic house generator. Stateless apart from the params passed in —
/// deterministic per seed.
class HouseGenerator {
public:
    struct Params {
        HouseStyle style = HouseStyle::Town;
        FacadeMaterial facade = FacadeMaterial::Brick; ///< facade material (texture choice app-side)
        float width = 5.f;        ///< footprint X (world units)
        float depth = 4.f;        ///< footprint Z
        int floors = 2;           ///< storeys (village/town); highrise derives its height from this
        float floorHeight = 2.8f; ///< storey height
        unsigned seed = 1;        ///< variant seed (roof type, features, palette)
        float wallIrregularity = 0.012f; ///< per-vertex wall jitter amplitude (bump-like relief; 0 = flat)
    };

    /// @brief Generates one house. Local space, base at y = 0, front door faces +z.
    HouseMesh generate(const Params& p) const;

    /// @brief Random-but-deterministic params for a style within a city cell of `cellSize`
    /// (footprint as a fraction of the cell, storeys per style).
    static Params randomParams(HouseStyle style, float cellSize, unsigned seed);
};

} // namespace fuhglgraphics::procedural
