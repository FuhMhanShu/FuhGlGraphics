#pragma once

/// @file serialize.hpp
/// @brief G19 — scene serialization: Scene <-> io::Json round-trip (plus saveScene/loadScene
/// for .json files). Serializes the node hierarchy (name + TRS), per-node effects (list +
/// params) and instancing, and the pure-data components: MeshComponent entries (mesh
/// reference via a resolver callback + material params + materialId + castShadow +
/// localOffset), LightComponent, DecalComponent, AnimationComponent clips.
/// GL-resource components (particles, light probes, water) are not serialized.
/// Mesh references: `mesh` is a JSON spec (e.g. {"shape":"box","halfExtents":[..],"color":[..]});
/// the caller's MeshResolver turns it into a fuhgl::render::Mesh owned by `meshArena`
/// (needs a current GL context for real meshes). Basis for G12 .fasset project files.

#include "../io/json.hpp"
#include "scene.hpp"

#include <fuhgl/render/mesh.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fuhglgraphics::scene {

/// @brief Turns a serialized mesh spec into a GL mesh (owned by the arena). May return nullptr
/// (entry is kept, mesh-less) when the spec is unknown — no GL context needed then.
using MeshResolver = std::function<std::unique_ptr<fuhgl::render::Mesh>(const io::Json& meshSpec)>;

/// @brief Resolver for the built-in procedural shapes: {"shape":"box","halfExtents":[x,y,z],
/// "color":[r,g,b]} and {"shape":"quad","halfSize":s,"color":[r,g,b]}. Creates a GL mesh —
/// needs a current GL context. Unknown specs -> nullptr.
std::unique_ptr<fuhgl::render::Mesh> defaultShapeResolver(const io::Json& meshSpec);

/// @brief Serializes the scene graph to JSON (see file header for the layout).
io::Json sceneToJson(const Scene& scene);

/// @brief Rebuilds a scene graph from JSON into `scene` (root is cleared first). Mesh specs
/// are resolved via `resolveMesh` into `meshArena` (owns the created GL meshes).
/// Returns false + err on malformed input (bool + log convention).
bool sceneFromJson(Scene& scene, const io::Json& j, const MeshResolver& resolveMesh,
                   std::vector<std::unique_ptr<fuhgl::render::Mesh>>& meshArena,
                   std::string* err = nullptr);

/// @brief saveScene/loadScene: file-level conveniences around sceneToJson/sceneFromJson.
bool saveScene(const Scene& scene, const std::string& path, std::string* err = nullptr);
bool loadScene(Scene& scene, const std::string& path, const MeshResolver& resolveMesh,
               std::vector<std::unique_ptr<fuhgl::render::Mesh>>& meshArena,
               std::string* err = nullptr);

} // namespace fuhglgraphics::scene
