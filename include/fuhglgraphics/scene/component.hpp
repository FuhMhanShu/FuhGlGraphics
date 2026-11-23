#pragma once

/// @file component.hpp
/// @brief G9 components: polymorphic Component list attached to a scene Node.
/// Implemented now: MeshComponent (a node can hold *several* meshes — one MeshEntry each,
/// with its own local offset, material parameters, textures, castShadow flag and material
/// ID), LightComponent, CameraComponent, ParticleEmitterComponent (G5),
/// AnimationComponent (G6), LightProbeComponent (G7), DecalComponent (G8),
/// WaterComponent (G13), TerrainComponent (G20).

#include "../anim/animation.hpp"
#include "../anim/gpu_skinner.hpp"
#include "../particles/particle_system.hpp"
#include "../probes/light_probes.hpp"
#include "../water/water_surface.hpp"
#include "../render/decals.hpp"

#include <fuhgl/cam/camera.hpp>
#include <fuhgl/math.hpp>
#include <fuhgl/render/light.hpp>
#include <fuhgl/render/lod.hpp>
#include <fuhgl/render/material.hpp>
#include <fuhgl/render/mesh.hpp>
#include <fuhgl/render/terrain.hpp>

#include <cstdint>
#include <vector>

namespace fuhglgraphics::scene {

class Node;
struct FrameData;

/// @brief Base class of all node components. Attach via Node::attach<T>(args...).
class Component {
public:
    virtual ~Component() = default;

    /// @brief The node this component is attached to (nullptr when detached).
    Node* node() const { return node_; }

    /// @brief Contributes to the frame (draw items / lights) — called by Scene::fillFrame()
    /// for each visible node. Default: nothing.
    virtual void fill(const Node& /*node*/, FrameData& /*frame*/) const {}

private:
    friend class Node;
    void setNode(Node* n) { node_ = n; }
    Node* node_ = nullptr;
};

/// @brief One mesh of a node (G9: several meshes per node). When `material` is nullptr,
/// the inline parameters below are synced into an owned preset material at fill time.
struct MeshEntry {
    fuhgl::render::Mesh* mesh = nullptr;              ///< drawn when set (non-const: instancing uploads instance data)
    fuhgl::math::Mat4 localOffset{};                  ///< extra local transform relative to the node
    const fuhgl::render::LodMesh* lod = nullptr;      ///< F41: optional LOD levels for this mesh
    bool castShadow = true;                           ///< drawn into the shadow depth pass
    std::uint32_t materialId = 0;                     ///< G-buffer MaterialId attachment (G8 decals)
    anim::GpuSkinner* gpuSkin = nullptr;              ///< G14: GPU skinning binding (drawn instead of `mesh` when Effect::GpuSkinning is on and GL >= 4.3)
    const fuhgl::render::MeshData* meshData = nullptr; ///< G18: CPU-side triangle data of `mesh` for ray-cast picking (glTF loader sets it; may be null)

    // --- material / textures (used when material == nullptr) ---
    const fuhgl::render::Material* material = nullptr; ///< explicit preset material wins
    fuhgl::math::Vec3 albedo{1.f, 1.f, 1.f};
    const fuhgl::Texture* albedoTexture = nullptr;    ///< albedo texture of this mesh (G-buffer unit 3)
    const fuhgl::Texture* normalMap = nullptr;        ///< optional normal map (G-buffer unit 4)
    fuhgl::math::Vec3 emissive{0.f, 0.f, 0.f};
    float metallic = 0.f;
    float roughness = 0.5f;
    float ao = 1.f;

    // --- culling: optional explicit local AABB (overrides the mesh bounding sphere) ---
    bool hasAabb = false;
    fuhgl::math::Vec3 aabbMin{0.f, 0.f, 0.f};
    fuhgl::math::Vec3 aabbMax{0.f, 0.f, 0.f};
};

/// @brief Mesh component: all MeshEntry meshes of the node, drawn at the node's world
/// matrix * entry.localOffset. When the node's `instancing` property is enabled, each entry
/// is drawn as one instanced draw call with the generated instance transforms.
class MeshComponent : public Component {
public:
    std::vector<MeshEntry> meshes; ///< several meshes per node

    /// @brief Adds a mesh entry and returns it (convenience).
    MeshEntry& addMesh(fuhgl::render::Mesh* mesh) {
        meshes.push_back(MeshEntry{});
        meshes.back().mesh = mesh;
        return meshes.back();
    }

    void fill(const Node& node, FrameData& frame) const override;

private:
    /// Owned preset materials, one per entry without an explicit `material` (synced at fill).
    mutable std::vector<fuhgl::render::Material> ownedPresets_;
};

/// @brief Light component: one fuhgl::render::Light, placed at the node's world transform
/// (position transformed by the world matrix; direction by its linear part).
class LightComponent : public Component {
public:
    fuhgl::render::Light light{};
    void fill(const Node& node, FrameData& frame) const override;
};

/// @brief Camera component: when Scene::fillFrame() is called without an explicit camera,
/// the first CameraComponent found in the graph provides the camera.
class CameraComponent : public Component {
public:
    fuhgl::Camera* camera = nullptr;
};

/// @brief G5 particle-emitter component: owns one particles::ParticleSystem plus its
/// EmitterConfig. fill() registers the system in FrameData::particleSystems; the app calls
/// system.update(dt) per frame and passes the systems to DeferredRenderer::render(...).
/// create() needs a current GL context (compute path on GL >= 4.3, CPU fallback below).
class ParticleEmitterComponent : public Component {
public:
    particles::EmitterConfig config;      ///< edit, then call system.setEmitter(config)
    particles::ParticleSystem system;     ///< create(maxParticles, config) once with a GL context

    void fill(const Node& node, FrameData& frame) const override; // defined in component.cpp
};

/// @brief G6 animation component: owns an anim::AnimationPlayer (clips). Per frame the app
/// calls `player.update(dt)` and `player.applyTo(scene)` (or applyTo(nodeMap)) before
/// Scene::fillFrame()/updateWorldTransforms(). For glTF assets, io::instantiateGltf() fills
/// `gpu.player` with the parsed animation clips.
class AnimationComponent : public Component {
public:
    anim::AnimationPlayer player;
};

/// @brief G7 light-probe component: owns one probes::LightProbeGrid (create() + bake() on
/// the CPU — no GL needed for the bake; the renderer uploads the 3D textures lazily).
/// fill() registers the grid in FrameData::lightProbes (the first component in the graph
/// wins); pass it to DeferredRenderer::render(..., frame.lightProbes) and enable
/// render::Effect::LightProbes.
class LightProbeComponent : public Component {
public:
    probes::LightProbeGrid grid;

    void fill(const Node& node, FrameData& frame) const override; // defined in component.cpp
};

/// @brief G8 decal component: one render::Decal. Edit `decal` (halfExtents, albedo,
/// albedoTexture/normalMap, opacity, materialIdMask); fill() sets the decal's world
/// matrix from the node's world transform and appends it to FrameData::decals. Pass
/// `&frame.decals` to DeferredRenderer::render(...) and enable render::Effect::Decals.
class DecalComponent : public Component {
public:
    render::Decal decal;

    void fill(const Node& node, FrameData& frame) const override; // defined in component.cpp
};

/// @brief G13 water-surface component: owns one water::WaterSurface (create() once with a
/// GL context, then tune waves/colors and call surface.update(dt) per frame). fill()
/// appends the surface's grid mesh as a draw item at the node's world matrix (no shadow
/// casting) and registers the surface in FrameData::water (the first component in the
/// graph wins); pass it to DeferredRenderer::render(..., decals, frame.water) and enable
/// render::Effect::Water. The node's transform places the water plane (local y = 0).
class WaterComponent : public Component {
public:
    water::WaterSurface surface;

    void fill(const Node& node, FrameData& frame) const override; // defined in component.cpp
};

/// @brief G20 terrain component: owns a FuhGLCore F38/F41 heightmap terrain as a LodMesh
/// (terrainLods(): `levels` grid resolutions with screen-space switch distances from
/// LodMesh::defaultDistances). create() generates a procedural heightmap
/// (procedural::heightmap) and uploads the LOD levels — needs a current GL context.
/// fill() appends one draw item whose `lod` points at the LodMesh, so the renderer picks
/// the level from the camera distance per frame (resolveLodMesh; SceneView::lastLodLevel
/// exposes the chosen level for introspection/tests; view.lodEnabled = false forces
/// level 0). heightAt() places objects on the terrain (bilinear CPU heightmap sample,
/// node-local xz, terrain centered at the node origin).
class TerrainComponent : public Component {
public:
    fuhgl::render::LodMesh lodMesh;   ///< terrain LOD levels (level 0 = full grid)
    std::vector<float> heights;       ///< CPU heightmap copy (heightAt, 0..1 row-major)
    int gridW = 0, gridH = 0;
    float sizeX = 0.f, sizeZ = 0.f, heightScale = 1.f;
    // create() parameters:
    unsigned seed = 11u;              ///< procedural::heightmap seed
    int octaves = 6;                  ///< procedural::heightmap octaves
    float noiseScale = 0.045f;        ///< procedural::heightmap scale
    int levels = 4;                   ///< LOD levels (1 = plain mesh, no switching)
    float fovYDeg = 60.f;             ///< for the default LOD switch distances
    std::vector<float> customDistances; ///< when non-empty: overrides the default distances
    // material (inline, like MeshEntry):
    fuhgl::math::Vec3 albedo{1.f, 1.f, 1.f};
    float metallic = 0.f;
    float roughness = 1.f;
    float ao = 1.f;
    bool castShadow = false;
    std::uint32_t materialId = 1;

    /// @brief Generates the heightmap and the LOD meshes. Needs a current GL context.
    bool create(int gridWidth, int gridHeight, float sizeX_, float sizeZ_, float heightScale_);
    /// @brief Rebuilds the LOD meshes from the current `heights` (e.g. after editing the
    /// heightmap in place to carve a lake basin) — `heightAt()` then matches too.
    /// Needs a current GL context. Returns false when `heights` does not match gridW*gridH.
    bool rebuildLod();
    /// @brief Terrain height at node-local (x, z) — bilinear heightmap sample * heightScale.
    float heightAt(float x, float z) const;
    void fill(const Node& node, FrameData& frame) const override; // defined in component.cpp

private:
    mutable fuhgl::render::Material ownedPreset_; ///< stable pointer for the DrawItem
};

} // namespace fuhglgraphics::scene
