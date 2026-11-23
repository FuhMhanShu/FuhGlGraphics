#pragma once

/// @file creature.hpp
/// @brief G30: modular procedural creatures (quadrupeds, reptiles). A `Skeleton` is a graph of
/// nodes (name, parent, offset, radius, ellipsoid scale, `BodyPart`); `finalize()` bakes it into
/// world-space primitives (one ellipsoid per node + one tapered capsule per parent link, each
/// with a broadphase AABB — cache-friendly linear evaluation). `creatureSdf()` joins the prims
/// with the G29 `sdf::smoothMin` and modulates the field with G24 simplex noise (scales/ribs/
/// folds directly in the geometry). `meshCreature()` meshes the field via G25 marching cubes,
/// computes analytic normals from the SDF gradient (finite differences), per-vertex curvature
/// (SDF Laplacian) into the vertex color channel (the G-buffer pass multiplies vColor into
/// albedo, so creases darken automatically) and a seamless cylindrical UV unwrap.
/// `bakeCreatureMaps()` bakes albedo + tangent-space normal maps from 3D object-space noise.
/// Pure CPU, deterministic per seed; no GL needed for generation.

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <string>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Body-part classification (drives blend strength / texturing accents).
enum class BodyPart { Torso, Neck, Head, Jaw, LegUpper, LegLower, Foot, Tail, Horn, Fin };

/// @brief One skeleton node. Body axis = +Z (head forward), up = +Y. `offset` is relative to
/// the parent node (translation only, parents accumulate).
struct SkeletonNode {
    std::string name;
    int parent = -1;                        ///< -1 = root
    fuhgl::math::Vec3 offset{0.f, 0.f, 0.f};///< relative to parent
    float radius = 0.3f;                    ///< base radius of the node's ellipsoid
    fuhgl::math::Vec3 ellipsoid{1.f, 1.f, 1.f}; ///< ellipsoid radii multiplier (x,y,z)
    float blend = 1.f;                      ///< smooth-min weight factor for this node
    BodyPart part = BodyPart::Torso;
};

/// @brief Skeleton graph; `finalize()` computes world positions and the primitive list.
class Skeleton {
public:
    /// @brief Adds a node (parents must be added before children). Returns the node index.
    int addNode(const std::string& name, int parent, const fuhgl::math::Vec3& offset,
                float radius, const fuhgl::math::Vec3& ellipsoid, BodyPart part,
                float blend = 1.f);

    /// @brief World-space primitive: an ellipsoid at `a` (radii = `ra` * `ell`) when
    /// `capsule == false`, or a tapered capsule from `a` to `b` (radii `ra`..`rb`).
    /// `aabbMin/aabbMax` is the broadphase box (already expanded by the max blend radius).
    struct Prim {
        fuhgl::math::Vec3 a{0.f, 0.f, 0.f}, b{0.f, 0.f, 0.f};
        float ra = 0.f, rb = 0.f;
        bool capsule = false;
        fuhgl::math::Vec3 ell{1.f, 1.f, 1.f};
        float blend = 1.f;
        BodyPart part = BodyPart::Torso;
        fuhgl::math::Vec3 aabbMin{0.f, 0.f, 0.f}, aabbMax{0.f, 0.f, 0.f};
    };

    void finalize();                        ///< computes world positions + prims (call once)
    bool finalized() const { return m_finalized; }
    fuhgl::math::Vec3 worldPos(int node) const;
    const std::vector<Prim>& prims() const { return m_prims; }
    /// @brief Bounds over all prim AABBs.
    void bounds(fuhgl::math::Vec3& mn, fuhgl::math::Vec3& mx) const;

    std::vector<SkeletonNode> nodes;

private:
    std::vector<fuhgl::math::Vec3> m_world;
    std::vector<Prim> m_prims;
    bool m_finalized = false;
};

/// @brief Parameters of a creature: SDF blending + noise detail + texture palette.
struct CreatureParams {
    unsigned seed = 1u;
    float smoothK = 0.22f;        ///< smooth-min radius (organic muscle/skin transitions)
    float detailAmp = 0.02f;      ///< 3D-noise modulation amplitude on the SDF (world units)
    float detailFreq = 6.f;       ///< noise frequency of the geometry detail
    int detailOctaves = 2;
    float scaleFreq = 9.f;        ///< scale-cell frequency for the baked texture (object space)
    float stripeFreq = 1.4f;      ///< stripes along the body axis (per world unit z)
    float bumpStrength = 1.5f;    ///< normal-map strength of the baked detail
    fuhgl::math::Vec3 bodyColor{0.42f, 0.40f, 0.30f};
    fuhgl::math::Vec3 bellyColor{0.72f, 0.66f, 0.52f};
    fuhgl::math::Vec3 accentColor{0.50f, 0.22f, 0.15f}; ///< stripes / horns
    float roughness = 0.55f;
};

/// @brief The creature SDF (negative inside): smooth-min of all prims + noise detail.
float creatureSdf(const Skeleton& skel, const CreatureParams& pr, const fuhgl::math::Vec3& p);

/// @brief Meshes the creature SDF over its bounds + margin at `resolution`^3 via G25 marching
/// cubes. Vertex normals = analytic SDF gradient; vertex color = `baseColor` * curvature shade
/// (Laplacian of the SDF, concave creases darker); uv = cylindrical unwrap around the body
/// axis (+Z): u = angle in the XY plane, v = z normalized to the bounds.
struct CreatureMesh {
    fuhgl::render::MeshData mesh;
    fuhgl::math::Vec3 bmin{0.f, 0.f, 0.f}, bmax{0.f, 0.f, 0.f};
};
CreatureMesh meshCreature(const Skeleton& skel, const CreatureParams& pr, int resolution,
                          const fuhgl::math::Vec3& baseColor = {1.f, 1.f, 1.f},
                          float curvatureGain = 6.f);

/// @brief CPU-baked texture maps (RGBA8, `size` x `size`): albedo from 3D object-space noise
/// (belly lighter, stripes on the back, scale-cell grooves via worley, fbm variation) and a
/// tangent-space normal map from the same height field. Seamless in u (noise evaluated at the
/// reconstructed 3D surface point, u wraps the full circle).
struct CreatureMaps {
    int size = 0;
    std::vector<unsigned char> albedo;  ///< RGBA8, size*size*4
    std::vector<unsigned char> normal;  ///< RGBA8 tangent-space normal map, size*size*4
    fuhgl::math::Vec3 bmin{0.f, 0.f, 0.f}, bmax{0.f, 0.f, 0.f};
};
CreatureMaps bakeCreatureMaps(const Skeleton& skel, const CreatureParams& pr, int size);

// --- presets ---------------------------------------------------------------------
/// @brief Dog-like quadruped: torso, neck, head + jaw + horns, 4 legs (upper/lower/foot), tail.
Skeleton makeQuadruped();
/// @brief Lizard-like reptile: flat wide torso, splayed legs, long tail, flat head + jaw.
Skeleton makeReptile();

} // namespace fuhglgraphics::procedural
