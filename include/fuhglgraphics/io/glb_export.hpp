#pragma once

/// @file glb_export.hpp
/// @brief G4 export counterpart — glTF 2.0 binary (.glb) writer, pure CPU (no GL):
/// exports the mesh entries of a scene::Scene whose MeshEntry::meshData is set (glTF-
/// imported or procedurally attached meshes) as one binary glTF: nodes (name + TRS +
/// children; one child node per mesh entry, carrying the entry's localOffset as TRS),
/// meshes (POSITION/NORMAL/COLOR_0/TEXCOORD_0 interleaved + uint32 indices) and
/// KHR-style pbrMetallicRoughness materials from the entry's material params.
/// Entries without meshData are skipped (counted via `skipped`). Used by the G12 asset
/// creator's "Export glTF (.glb)"; round-trips through io::loadGltf.

#include "../scene/scene.hpp"

#include <string>

namespace fuhglgraphics::io {

/// @brief Writes `scene` as a .glb file. Returns false + err on I/O failure; entries
/// without CPU meshData are skipped and counted into `skipped` (when non-null).
bool exportSceneGlb(const scene::Scene& scene, const std::string& path, int* skipped = nullptr,
                    std::string* err = nullptr);

} // namespace fuhglgraphics::io
