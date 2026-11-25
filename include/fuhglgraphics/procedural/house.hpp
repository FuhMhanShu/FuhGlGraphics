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
/// stone = Naturstein with relief courses, wood = timber-frame plaster, adobe = Lehmputz,
/// concrete = Beton/Glas).
enum class FacadeMaterial { Brick = 0, Plaster = 1, Stone = 2, Wood = 3, Adobe = 4, Concrete = 5 };

/// @brief Locally available building resource ("build with what is around"). `Auto` resolves
/// from BuildingContext::biome (worldgen Biome cast: 2 desert, 3 grassland, 4 forest,
/// 5 jungle, 6 taiga, 7 tundra, 8 rock, 9 snow, 1 shore); high-density wealthy zones override
/// to Concrete (metropole decouples from natural resources).
enum class LocalMaterial { Auto = 0, Wood, Stone, Brick, Adobe, Concrete };

/// @brief Environment + socio-economy context of one building site. The generator derives
/// form, materials and colors from these few values (climate -> architecture, local resources
/// -> materials, wealth/density -> storeys, symmetry, shop fronts). All floats are [0,1].
struct BuildingContext {
    float temperature = 0.5f;     ///< 0 = cold, 1 = hot
    float precipitation = 0.5f;   ///< rain/snow amount (snow = precipitation * (1 - temperature))
    float humidity = 0.5f;        ///< dampness (drives moss/lichen weathering + stilts)
    float waterProximity = 0.f;   ///< 1 = flood zone / coast -> stilts or high stone foundation
    float wealth = 0.5f;          ///< 0 = poor (patches, asymmetry, weathering), 1 = rich (ornaments)
    float density = 0.5f;         ///< population density: low = detached + garden fence,
                                  ///< high = block edge + 4-6 storeys + ground-floor shop
    int biome = -1;               ///< worldgen Biome cast (used when localMaterial == Auto); <0 = none
    LocalMaterial localMaterial = LocalMaterial::Auto; ///< local resource -> facade/roof material
    fuhgl::math::Vec3 soilColor{0.36f, 0.29f, 0.21f};  ///< local soil (plinth + shader splash stain)
};

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
    bool hasParapet = false;   ///< flat roof with a low railing (arid climate / rooftop terrace)
    bool onStilts = false;     ///< flood zone: house lifted on posts over a stone foundation
    bool hasShopFront = false; ///< ground floor is a shop: big display windows + sign band
    bool hasFence = false;     ///< low garden fence (low density / detached house)
    bool hasPatches = false;   ///< patched walls (corrugated sheets / mixed materials, low wealth)
    bool hasCornice = false;   ///< ornamental cornice bands per floor (high wealth)
    float roofHeight = 0.f;    ///< roof rise (steep when wet/snowy)
    float roofOverhang = 0.14f;///< eave overhang (wide when rainy — keeps the facade dry)
    float windowScale = 1.f;   ///< window size factor (small when hot, large when cold)
    float stiltHeight = 0.f;   ///< lift of the house above ground when onStilts
    float width = 0.f, depth = 0.f, wallHeight = 0.f;
    BuildingContext context;  ///< echo of the context the house was derived from
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
        BuildingContext context;  ///< environment + socio-economy -> derived architecture rules
    };

    /// @brief Generates one house. Local space, base at y = 0 (stilt houses: base at the
    /// stilt top), front door faces +z. Climate/material/wealth/density rules from
    /// `p.context` are applied automatically.
    HouseMesh generate(const Params& p) const;

    /// @brief Random-but-deterministic params for a style within a city cell of `cellSize`
    /// (footprint as a fraction of the cell, storeys per style). The overload with `ctx`
    /// additionally derives facade material and storeys from the BuildingContext.
    static Params randomParams(HouseStyle style, float cellSize, unsigned seed);
    static Params randomParams(HouseStyle style, float cellSize, unsigned seed,
                               const BuildingContext& ctx);

    /// @brief Maps a BuildingContext to a facade material (local-resource rules; Auto resolves
    /// via biome, metropole override via density+wealth).
    static FacadeMaterial resolveFacadeMaterial(const BuildingContext& ctx, unsigned seed);
};

} // namespace fuhglgraphics::procedural
