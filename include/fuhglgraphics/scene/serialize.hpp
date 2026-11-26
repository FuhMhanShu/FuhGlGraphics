#pragma once

/// @file serialize.hpp
/// @brief G12 asset creator: `.fasset` project serialization — scene::Scene <-> io::Json.
/// Pure CPU (no GL context needed): GL resources (Mesh/Texture) are stored as path
/// references (MeshEntry::assetRef / albedoTexturePath / normalMapPath); the editor
/// recreates the GL objects when it loads the project with a current GL context.
///
/// Conventions like the rest of the library: bool + err string, no exceptions.
/// Layout (see docs/ASSET_CREATOR.md §6): `out` is the root node object —
///   { "name", "translation", "rotation", "scale",
///     "effects": { "list": ["taa", ...], "params": {...} },
///     "instancing": {...}, "components": [ {"type": "mesh"|"light", ...} ],
///     "children": [ ...recursive... ] }

#include "../io/json.hpp"
#include "scene.hpp"

#include <string>

namespace fuhglgraphics::scene {

/// @brief Serializes the scene graph (root name + all descendants) into `out`.
bool serializeScene(const Scene& scene, io::Json& out, std::string* err = nullptr);

/// @brief Rebuilds the graph into `scene`. The scene must be fresh/empty (the root is
/// reused, its children must not exist yet) — pass a default-constructed Scene.
/// Mesh entries are created with mesh = nullptr and the stored asset references; camera,
/// particle and animation components are runtime-only and skipped (logged as markers).
bool deserializeScene(Scene& scene, const io::Json& in, std::string* err = nullptr);

} // namespace fuhglgraphics::scene
