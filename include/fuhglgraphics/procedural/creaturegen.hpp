#pragma once

/// @file creaturegen.hpp
/// @brief G32: full procedural-creature pipeline (design: docs/CREATURE_GENERATION.md).
/// Language- and engine-agnostic algorithms implemented on top of G24 noise, G25 marching
/// cubes, G29 SDF smooth-min and the G30 Skeleton. Pure CPU, deterministic per seed.
///
/// 1. Allometry: `deriveBlueprint(Ecology)` maps ecological inputs (body mass, locomotion,
///    diet, climate) through allometric power laws (bone radius ~ M^0.36, body length ~
///    M^0.27, ...) plus categorical rules (predator -> frontal eyes, prey -> lateral eyes;
///    cold climate -> fat/fur/compact body, Bergmann/Allen) to a coupled `Blueprint`.
///    `buildBlueprintSkeleton()` instantiates it as a bilateral-symmetry skeleton graph
///    (left side mirrored from the right, plus small seeded micro-asymmetry).
/// 2. Layered anatomy SDF: `anatomySdf()` combines bone anchors (small blend radius, hard),
///    muscle strands (tapered capsules with a fusiform radius profile, medium radius) and a
///    skin/fat overlay (large radius + gravity sag) via smooth-min, then modulates simplex
///    fBm (folds) and Worley cells (scales) directly into the field.
/// 3. `extractSurface()` meshes the field via G25 marching cubes and adds per-vertex analytic
///    gradient normals, Laplacian curvature and a material mask (skin/keratin/eye) from the
///    nearest primitive's BodyPart.
/// 4. `reactionDiffusion()` runs a Gray-Scott Turing pattern on the seamless cylindrical
///    domain (u wraps); `bakeMaterial()` bakes UV-free object-space albedo + normal maps with
///    countershading, curvature darkening and keratin material transitions.
/// 5. `solveTwoBoneIk()` (analytic, law of cosines) + `creatureGaitPose()` produce per-frame
///    poses: footholds on a terrain heightfield, gait phase offsets per leg, a traveling
///    wave along spine/tail and a breathing cycle.

#include <fuhglgraphics/procedural/creature.hpp>

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace fuhglgraphics::procedural {

// --- 1. evolutionary blueprint & allometry -----------------------------------------

/// @brief Locomotion class of the creature (drives leg count / torso orientation / tail).
enum class Locomotion { Quadruped, Biped, Serpentine };
/// @brief Diet drives eye position (predator vs. prey) and horn rules.
enum class Diet { Herbivore, Carnivore, Omnivore };
/// @brief Climate zone drives fat thickness, fur density, ear size (Bergmann/Allen rules).
enum class Climate { Polar, Temperate, Tropical, Arid };

/// @brief Ecological input vector: the only free inputs of the generator.
struct Ecology {
    float bodyMassKg = 20.f;                     ///< body mass M
    Locomotion locomotion = Locomotion::Quadruped;
    Diet diet = Diet::Omnivore;
    Climate climate = Climate::Temperate;
};

/// @brief Coupled proportion vector (allometric output). All lengths in world units.
struct Blueprint {
    float bodyLength = 1.f;      ///< torso length along +Z
    float bodyRadius = 0.30f;    ///< torso radius
    float legLength = 0.60f;     ///< hip/shoulder to ground at rest
    float boneRadius = 0.05f;    ///< allometric: ~ mass^0.36
    float neckLength = 0.40f;
    float headSize = 0.20f;
    float tailLength = 0.80f;
    float eyeSize = 0.05f;
    float eyeForward = 0.3f;     ///< 0 = lateral (prey), 1 = frontal (predator)
    float earSize = 0.10f;       ///< Allen's rule: small in cold climates
    float fatThickness = 0.02f;  ///< subcutaneous fat (climate insulation)
    float furDensity = 0.5f;     ///< 0..1 bump-only fur
    bool horns = false;
    float hornLength = 0.20f;
    int legPairs = 2;            ///< 2 quadruped, 1 biped, 0 serpentine
    bool upright = false;        ///< biped: vertical torso ellipsoid
    unsigned seed = 1u;
};

/// @brief Rule-based parameter generator: ecological inputs -> coupled proportions.
/// Power laws for size-driven parameters, decision table for categorical ones
/// (diet -> eyeForward, climate -> fat/fur/ears/compactness, locomotion -> legs/tail).
Blueprint deriveBlueprint(const Ecology& eco, unsigned seed);

/// @brief Instantiates a blueprint as a G30 skeleton graph with bilateral symmetry
/// (right side mirrored to the left, x -> -x) plus small seeded micro-asymmetry.
/// Node naming: "torso", "neck", "head", "jaw", "eyeL"/"eyeR", "earL"/"earR",
/// "hornL"/"hornR" (if any), "shoulderL/R"+"elbowL/R"+"footFL/R",
/// "hipL/R"+"kneeL/R"+"footHL/HR", "tail1..n". Finalized.
Skeleton buildBlueprintSkeleton(const Blueprint& bp);

// --- 2. anatomically layered SDF ----------------------------------------------------

/// @brief Parameters of the three anatomical SDF layers + surface detail.
struct Anatomy {
    unsigned seed = 1u;
    float boneBlendK = 0.02f;     ///< small blend radius -> hard bone anchors / sharp keratin
    float muscleBlendK = 0.08f;   ///< medium radius -> muscle strands
    float skinBlendK = 0.25f;     ///< large radius -> soft skin/fat overlay
    float boneRadiusScale = 0.55f;///< bone anchors smaller than the muscle radius
    float sagAmp = 0.04f;         ///< gravity sag amplitude of the skin layer
    float sagHeight = 0.6f;       ///< sag decay height above ground
    float detailAmp = 0.02f;      ///< simplex fBm amplitude (folds/wrinkles) in the field
    float detailFreq = 6.f;
    int detailOctaves = 2;
    float scaleAmp = 0.012f;      ///< worley scale amplitude (cones per cell)
    float scaleFreq = 9.f;        ///< worley cell frequency
};

/// @brief Layered creature SDF (negative inside): smin of bone layer (k_bone), muscle layer
/// (fusiform-profile capsules, k_muscle) and skin layer (full radius + fat + gravity sag,
/// k_skin), plus simplex fBm detail and worley scale cones modulated into the field.
float anatomySdf(const Skeleton& skel, const Anatomy& an, const fuhgl::math::Vec3& p);

// --- 3. surface extraction & masks --------------------------------------------------

/// @brief Material classification per surface point.
enum class SurfaceMaterial : std::uint8_t { Skin = 0, Keratin = 1, Eye = 2 };

/// @brief Meshed creature surface with analytic per-vertex data.
struct SurfaceMasks {
    fuhgl::render::MeshData mesh;          ///< 11 floats/vertex: pos, normal, color, uv
    std::vector<float> curvature;          ///< per-vertex ~mean curvature (Laplacian-based)
    std::vector<std::uint8_t> material;    ///< per-vertex SurfaceMaterial from nearest prim
    fuhgl::math::Vec3 bmin{0.f, 0.f, 0.f}, bmax{0.f, 0.f, 0.f};
};

/// @brief Meshes `anatomySdf` over the skeleton bounds + margin via G25 marching cubes, then
/// fills per-vertex gradient normals, Laplacian curvature (negative = concave crease) and the
/// material mask (Horn/Foot/Jaw prims -> Keratin, sharp via the small bone blend radius).
SurfaceMasks extractSurface(const Skeleton& skel, const Anatomy& an, int resolution,
                            const fuhgl::math::Vec3& baseColor = {1.f, 1.f, 1.f});

// --- 4. UV-free texturing & materiality ---------------------------------------------

/// @brief Color palette + roughness of the creature.
struct Palette {
    fuhgl::math::Vec3 dorsal{0.35f, 0.32f, 0.25f};  ///< back color (countershading dark)
    fuhgl::math::Vec3 ventral{0.75f, 0.70f, 0.58f}; ///< belly color (light)
    fuhgl::math::Vec3 accent{0.50f, 0.20f, 0.12f};  ///< Turing pattern (spots/stripes)
    fuhgl::math::Vec3 keratin{0.85f, 0.82f, 0.72f}; ///< horns/claws/hooves
    float roughness = 0.55f;
    float furDensity = 0.5f;     ///< 0..1: bump-only fur in the baked height field
};

/// @brief Gray-Scott reaction-diffusion (Turing) parameters on the cylindrical domain
/// (u = angle around the body axis, periodic/seamless; v = along +Z, clamped).
struct Pattern {
    unsigned seed = 1u;
    int gridU = 96;               ///< pattern grid resolution (u = around the body)
    int gridV = 192;              ///< (v = along the body)
    float feed = 0.055f;          ///< Gray-Scott F
    float kill = 0.062f;          ///< Gray-Scott k
    float diffU = 0.16f;          ///< D_U (Laplacian coefficient, in cells/step)
    float diffV = 0.08f;          ///< D_V
    int iterations = 600;
    float anisotropy = 1.f;       ///< >1 stretches cells along the body axis -> stripes
    float threshold = 0.5f;       ///< pattern mask threshold on V normalized by its max
    float softness = 0.08f;       ///< smoothstep width around the threshold
    bool countershading = true;   ///< dark back / light belly from the up/down position
    float curvatureDarkening = 4.f; ///< darken concave creases by the curvature mask
    float bumpStrength = 1.5f;    ///< normal-map strength of the baked height field
};

/// @brief Runs the Gray-Scott reaction-diffusion on the cylindrical grid. Returns the
/// pattern mask `m(u,v)` in [0,1] (size gridU*gridV, index i + gridU*j), deterministic per
/// seed; `anisotropy > 1` yields stripes along the body axis, ~1 yields spots.
std::vector<float> reactionDiffusion(const Pattern& pt);

/// @brief Baked UV-free material maps (RGBA8, size x size, cylindrical unwrap: u wraps,
/// v along the body axis). The surface point of each texel is found by bisecting a ray
/// from the body axis on the SDF, so all noise is evaluated at the true 3D object-space
/// point (seamless). Albedo = countershading * Turing pattern * curvature darkening *
/// keratin override; the normal map comes from an fBm + worley-groove height field.
struct BakedMaps {
    int size = 0;
    std::vector<unsigned char> albedo;    ///< RGBA8
    std::vector<unsigned char> normal;    ///< tangent-space RGBA8
    std::vector<unsigned char> material;  ///< per-texel SurfaceMaterial
    fuhgl::math::Vec3 bmin{0.f, 0.f, 0.f}, bmax{0.f, 0.f, 0.f};
};

BakedMaps bakeMaterial(const Skeleton& skel, const Anatomy& an, const Palette& pal,
                       const Pattern& pt, int size);

// --- 5. procedural locomotion & kinematics ------------------------------------------

/// @brief Terrain heightfield query: y = terrain(x, z). Used for footholds and root height.
using TerrainFn = std::function<float(float x, float z)>;

/// @brief Gait + secondary-motion parameters.
struct Gait {
    float cycleHz = 0.8f;         ///< gait cycle frequency (scales ~ M^-0.18)
    float strideLength = 0.5f;    ///< foothold travel per cycle (world units)
    float liftHeight = 0.08f;     ///< swing apex height
    float spineWaveAmp = 0.03f;   ///< traveling-wave amplitude at the tail tip
    float spineWaveLength = 0.8f; ///< wave length along the spine (world units)
    float breathAmp = 0.02f;      ///< thorax breathing displacement (fraction of radius)
    float breathHz = 0.25f;       ///< breaths per second (~ M^-0.17, Kleiber)
    fuhgl::math::Vec3 moveDir{0.f, 0.f, 1.f}; ///< travel direction (xz-plane, normalized inside)
    std::vector<float> legPhases; ///< per-leg phase offsets in [0,1); default: lateral walk
};

/// @brief Analytic two-bone IK (law of cosines): computes the mid joint (knee/elbow) for a
/// chain root -> mid -> end with segment lengths `l1`, `l2` reaching `target`. The bend axis
/// `bendAxis` (e.g. the outward body normal for legs) fixes the plane of the solution.
/// Unreachable targets are clamped to |l1 +- l2|. Returns false when degenerate.
bool solveTwoBoneIk(const fuhgl::math::Vec3& root, const fuhgl::math::Vec3& target,
                    float l1, float l2, const fuhgl::math::Vec3& bendAxis,
                    fuhgl::math::Vec3& midOut);

/// @brief One posed frame: world-space joint positions, one per skeleton node (same order as
/// `skel.nodes`). Leg chains (LegUpper->LegLower->Foot) are IK-solved onto footholds on the
/// terrain (phase per leg, swing lift, stride along `moveDir`); the spine/tail chain gets a
/// traveling lateral wave with an envelope growing toward the tail; thorax nodes get the
/// breathing displacement. `terrain` may be empty -> flat ground y=0.
std::vector<fuhgl::math::Vec3> creatureGaitPose(const Skeleton& skel, const Gait& gait,
                                                float time, const TerrainFn& terrain);

} // namespace fuhglgraphics::procedural
