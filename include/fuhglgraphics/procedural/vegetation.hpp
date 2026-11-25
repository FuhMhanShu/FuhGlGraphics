#pragma once

/// @file vegetation.hpp
/// @brief G37: ecophysiological vegetation generator (design: docs/VEGETATION_GENERATION.md).
/// Same environmental parameters as terrain and buildings (G31 worldgen climate maps
/// `temperature`/`humidity`, plus wind exposure and stand density) drive one algorithm that
/// produces completely different but biologically plausible plants:
///
/// 1. Light & space competition — Space Colonization Algorithm: invisible "light attractors"
///    are distributed in the crown volume (dense stand: only near the top, because light then
///    comes only from above). Branch tips actively grow toward the nearest attractor and kill
///    it when within kill distance. A single tree on a meadow gets light from all sides ->
///    broad spreading crown (old oak); a tree in a dense forest gets light only from above ->
///    lower branches die off (self-pruning), the trunk grows straight and tall and only a
///    small crown forms at the top (rainforest / conifer forest).
/// 2. Climate-to-geometry rules (`deriveGrowthForm`): humidity -> huge thin drip-tip leaves
///    (tropics) vs. small thick waxy leaves or thorns/succulents (dry); temperature/snow ->
///    needle trees with conical crown and drooping branches (cold) vs. broadleaf trees with
///    horizontal branches (warm, frost-free); wind exposure -> a constant wind vector added to
///    the growth direction plus reduced max height (crooked, stocky "wind-flagged" trees or
///    flat ground cover on coasts/peaks).
/// 3. Hydraulic architecture — Da Vinci rule / pipe model (`applyPipeModel`): the cross-section
///    of a parent branch equals the sum of the cross-sections of its children
///    (R^n = r1^n + r2^n + ..., n ~ 2), so trunks and branches taper at every fork with
///    statically plausible proportions.
/// 4. Procedural leaf & bark texturing: the generator emits per-vertex growth attributes in the
///    standard MeshData layout (uv.x = age/radius, uv.y = wet-side factor) and a
///    `VegetationMaterial` with the shader parameters — leaf age & sun (young outer shoots
///    light yellow-green via `leafTint`, old inner leaves dark; autumn/drought shifts the
///    chlorophyll green over yellow/orange to red-brown via `autumnRamp`), subsurface scattering
///    through thin leaves (backlighting; engine-side `render::Effect::Subsurface` G33 with
///    `MeshEntry::subsurface` = material.sss), bark by age & humidity (young smooth green-gray,
///    older cracked by vertically stretched simplex noise `barkTint`, moss on the wet weather
///    side `wetSideFactor` * humidity). The same formulas are documented as the shader contract;
///    the CPU-side vertex colors are the stock-renderer fallback (the G-buffer multiplies
///    vColor into albedo).
///
/// Pure CPU, deterministic per seed, no GL — output is plain `MeshData` (branches + leaves) to
/// attach to a scene::Scene (upload via Mesh::setMeshData).

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>
#include <vector>

namespace fuhglgraphics::procedural {

// --- 0. environmental input vector (same parameter space as G31 terrain/buildings) --------

/// @brief Ecological environment of one plant site. `temperature`/`humidity` are the same
/// [0,1] maps the G31 world generator computes per cell (`GeneratedWorld::temperature` /
/// `humidity`); `windExposure`/`windDir` come from slope/altitude (coasts, ridges, peaks);
/// `standDensity` from the local tree spacing; `season` drives the autumn leaf-color ramp.
struct EcoEnvironment {
    float temperature = 0.5f;                 ///< [0,1]: 0 = cold/snowy, 1 = warm/frost-free
    float humidity = 0.5f;                    ///< [0,1]: 0 = dry (steppe), 1 = very wet (tropics)
    float windExposure = 0.f;                 ///< [0,1]: 0 = sheltered, 1 = coast/peak
    fuhgl::math::Vec3 windDir{1.f, 0.f, 0.f}; ///< normalized: direction the wind blows toward
    float standDensity = 0.f;                 ///< [0,1]: 0 = single tree/meadow, 1 = dense forest
    float season = 0.f;                       ///< [0,1]: 0 = summer, 1 = late autumn
    unsigned seed = 1;                        ///< variant seed (attractor jitter, branching)
};

// --- 1. climate-to-geometry rules ----------------------------------------------------------

/// @brief Leaf morphology selected by the climate rules.
enum class LeafMorphology {
    Broad = 0,  ///< broadleaf (warm, frost-free)
    DripTip,    ///< huge thin leaf with elongated drip tip (very wet tropics)
    Needle,     ///< conifer needle (cold/snowy)
    Thorn,      ///< thorn/spike instead of leaves (dry)
    None        ///< succulent: no leaves, thick water-storing trunk
};

/// @brief Growth form derived from the environment (the "climate -> geometry" rule table).
struct GrowthForm {
    float maxHeight = 6.f;        ///< max tree height in local units (wind reduces it)
    float crownBase = 1.f;        ///< y where the crown envelope starts (rises with density)
    float crownRadius = 2.2f;     ///< crown envelope radius (shrinks with density)
    float branchAngleDeg = 70.f;  ///< side-branch angle from the parent direction
    float droop = 0.f;            ///< >0: branches bend downward (snow sheds, wind-flagged)
    fuhgl::math::Vec3 windLean{0.f, 0.f, 0.f}; ///< constant wind vector added to growth dirs
    LeafMorphology leaf = LeafMorphology::Broad;
    float leafSize = 0.55f;       ///< leaf quad size
    float leafThickness = 0.5f;   ///< 0 = thin (SSS backlight), 1 = thick waxy (dry)
    float dripTip = 0.f;          ///< 0..1 elongation of the leaf tip (tropical drip tip)
    bool succulent = false;       ///< thick water-storing trunk, no leaves
    float succulence = 0.f;       ///< trunk radius multiplier (succulent)
    bool selfPrune = false;       ///< dense stand: shaded lower branches die off
    float pruneBelow = 0.f;       ///< branches below this height are pruned (dense stand)
};

/// @brief Maps the environment to a growth form: temperature/snow -> needle tree with conical
/// crown and drooping branches vs. broadleaf with horizontal branches; humidity -> drip-tip
/// leaves (wet) vs. small waxy leaves / thorns / succulent (dry); wind -> lean vector + reduced
/// height + ground cover at high exposure; stand density -> high crown base + self-pruning.
GrowthForm deriveGrowthForm(const EcoEnvironment& eco);

// --- 2. space colonization (light & space competition) --------------------------------------

/// @brief One node of the space-colonization skeleton (node 0 = root at the origin).
struct ScaNode {
    fuhgl::math::Vec3 pos{0.f, 0.f, 0.f};
    int parent = -1;      ///< index of the parent node (-1 for the root)
    float radius = 0.f;   ///< pipe-model radius (set by applyPipeModel)
    int depth = 0;        ///< branching depth
    bool alive = true;    ///< false = self-pruned
    float age = 0.f;      ///< normalized creation order in [0,1]: 0 = root, 1 = youngest tip
};

/// @brief The grown skeleton: nodes in creation order (children always after their parent).
struct ScaTree {
    std::vector<ScaNode> nodes;
    float height = 0.f;   ///< measured max y
    int attractorsLeft = 0;
};

/// @brief Space Colonization parameters.
struct ScaParams {
    int attractors = 320;        ///< light attractors in the crown volume
    float killDistance = 0.58f; ///< an attractor closer than this is consumed by the tip
    float step = 0.38f;          ///< growth step per iteration (local units)
    int maxIterations = 40;
    float branchProbability = 0.28f; ///< chance for a side branch per growth step
    int pruneGrace = 8;          ///< iterations without light capture before a side tip dies
    int maxNodes = 2200;         ///< hard cap on skeleton nodes
};

/// @brief Space Colonization Algorithm: distributes `p.attractors` light attractors in the
/// crown envelope (stand density biases them to the top = light only from above in a dense
/// forest, and shrinks the crown radius), then grows branch tips toward the nearest attractor
/// (wind vector added to the growth direction), consuming attractors within kill distance.
/// Tips that capture no light for `pruneGrace` iterations die when the form self-prunes, and
/// branches below `form.pruneBelow` are pruned — dense-stand lower branches dying off.
/// Deterministic per seed.
ScaTree spaceColonization(const GrowthForm& form, const EcoEnvironment& eco, const ScaParams& p,
                          unsigned seed);

/// @brief Da Vinci rule / pipe model: bottom-up (nodes are created children-after-parent, so
/// iterate in reverse) each node's radius satisfies R^n = sum of children radii^n plus the tip
/// bud radius at leaves (n = `exponent`, ~2 = area-preserving). Dead (pruned) nodes do not
/// contribute. Result is stored in ScaNode::radius.
void applyPipeModel(ScaTree& tree, float tipRadius, float exponent = 2.f);

// --- 3. shader/material contract (leaf & bark texturing) ------------------------------------

/// @brief Material + shader parameters of one plant (leaf/bark texturing, see file doc §4).
struct VegetationMaterial {
    fuhgl::math::Vec3 leafYoung{0.55f, 0.76f, 0.22f}; ///< young outer shoots: light yellow-green
    fuhgl::math::Vec3 leafOld{0.09f, 0.30f, 0.11f};   ///< old inner leaves: dark saturated green
    fuhgl::math::Vec3 autumnLow{0.95f, 0.78f, 0.14f};  ///< chlorophyll ramp: yellow
    fuhgl::math::Vec3 autumnMid{0.92f, 0.46f, 0.08f};  ///< orange
    fuhgl::math::Vec3 autumnHigh{0.42f, 0.16f, 0.07f}; ///< red-brown
    float autumn = 0.f;        ///< [0,1] autumn/drought factor driving the chlorophyll ramp
    float sss = 0.6f;          ///< subsurface scattering strength of thin leaves
                               ///< (MeshEntry::subsurface with render::Effect::Subsurface, G33)
    fuhgl::math::Vec3 barkYoung{0.56f, 0.58f, 0.47f}; ///< young wood: smooth greenish-gray
    fuhgl::math::Vec3 barkOld{0.24f, 0.17f, 0.11f};   ///< old bark: dark brown
    float barkCrackScale = 7.f;   ///< frequency of the vertically stretched simplex noise
    float barkCrackStretch = 4.f; ///< y-stretch of the crack noise (cracks run vertically)
    fuhgl::math::Vec3 moss{0.15f, 0.38f, 0.12f};      ///< moss color on the wet weather side
    float mossCoverage = 0.f;     ///< [0,1] moss amount = humidity on the wet side
};

/// @brief Chlorophyll ramp over t in [0,1]: green -> yellow -> orange -> red-brown
/// (autumn or drought). t = 0 keeps the green base (handled by leafTint), the returned color
/// is the ramp target at `t`.
fuhgl::math::Vec3 autumnRampColor(const VegetationMaterial& m, float t);

/// @brief Leaf tint from growth age and autumn factor: young outer shoots (age -> 1) are light
/// yellow-green, old inner leaves (age -> 0) dark saturated; `autumn` in [0,1] shifts the
/// green over yellow/orange to red-brown. This is baked into the vertex color (stock renderer)
/// and is the formula a custom leaf shader evaluates from uv.x = age.
fuhgl::math::Vec3 leafTint(const VegetationMaterial& m, float age, float autumn);

/// @brief Wet-side factor in [0,1]: 1 on surfaces facing the incoming wind/rain
/// (normal . -windDir), 0 on the lee side. Drives the moss mask in the bark shader
/// (`moss = mossCoverage * wetSideFactor(normal, windDir)`) and is stored in uv.y.
float wetSideFactor(const fuhgl::math::Vec3& normal, const fuhgl::math::Vec3& windDir);

// --- 4. generator ----------------------------------------------------------------------------

/// @brief Generator input: environment + overall scale + SCA tuning.
struct VegetationParams {
    EcoEnvironment eco;
    float scale = 1.f;      ///< overall size multiplier (local units -> world units)
    ScaParams sca;
};

/// @brief Generated plant: branch mesh + leaf mesh (both 11 floats/vertex; branches:
/// uv.x = radius/rootRadius (bark age), uv.y = wet-side factor; leaves: uv.x = age,
/// uv.y = wet-side factor; colors are the baked tints) + the material for the shader side.
struct VegetationMesh {
    fuhgl::render::MeshData branches;  ///< wood (cylinders, pipe-model radii)
    fuhgl::render::MeshData leaves;    ///< leaves / needles / thorns (kite quads, may be empty)
    VegetationMaterial material;
    GrowthForm form;
    float height = 0.f;                ///< measured height in world units
    int attractorsLeft = 0;
};

/// @brief Class-based ecophysiological plant generator. Stateless apart from the params passed
/// in — deterministic per seed. Local space, base at y = 0, grows along +Y (wind leans aside).
class VegetationGenerator {
public:
    /// @brief Grows one plant from the environment params: growth form -> space colonization
    /// -> pipe model -> branch/leaf meshes with baked tints + material.
    VegetationMesh generate(const VegetationParams& p) const;

    /// @brief Random-but-deterministic params for an environment (scale/attractor jitter).
    static VegetationParams randomParams(const EcoEnvironment& eco, unsigned seed);
};

} // namespace fuhglgraphics::procedural
