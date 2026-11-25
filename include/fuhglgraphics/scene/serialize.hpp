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

/// @brief Serializes a node's Effects (list + params) to JSON — layout {"list":[names],
/// "params":{...}}. Public because the G12 MCP render tools and the G11 JSON config use it.
io::Json effectsToJson(const Effects& fx);

/// @brief Inverse of effectsToJson(): rebuilds list + params. Unknown effect names fail
/// with err (bool + log convention); unknown param names are ignored.
bool effectsFromJson(const io::Json& j, Effects& fx, std::string* err = nullptr);

/// @brief Sets one EffectParams field by name (e.g. "bloomThreshold"; vec3 fields take a
/// [x,y,z] array; "tonemapOperator" also accepts "aces"/"reinhard"/"uncharted2"/"linear").
/// Returns false + err on an unknown name or wrong value type. Used by the MCP tool
/// `render_set_param` and by the G11 JSON renderer config.
bool setEffectParam(EffectParams& p, const std::string& name, const io::Json& value,
                    std::string* err = nullptr);

/// @brief Serializes one EffectParams field by name (Null when the name is unknown).
io::Json effectParamToJson(const EffectParams& p, const std::string& name);

/// @brief All settable EffectParams field names (for `render_get_settings` / tool schemas).
std::vector<std::string> effectParamNames();

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
