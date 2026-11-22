#pragma once

/// @file gltf.hpp
/// @brief G4: glTF 2.0 asset loading — `io::loadGltf(path)` parses .gltf (JSON) and .glb
/// (binary container) into plain data (FuhGLCore `render::MeshData` + material factors +
/// images + node hierarchy), and `io::instantiateGltf()` maps the default scene onto the
/// G9 scene graph (`scene::Scene` / `Node` / `MeshComponent`) with GL resources.
///
/// Supported: buffers (external .bin, embedded base64 `data:` URIs, GLB BIN chunk),
/// bufferViews + accessors (all component types, normalized, strided), mesh primitives
/// with mode TRIANGLES / TRIANGLE_STRIP / TRIANGLE_FAN and attributes POSITION, NORMAL,
/// TEXCOORD_0, COLOR_0 + indices; materials via KHR_materials_pbrMetallicRoughness
/// (baseColor/emissive/normal/occlusion/metallicRoughness factors + textures, alphaMode,
/// doubleSided); textures/images (external files or GLB-embedded; PNG/JPEG decoded to
/// RGBA8 via Qt QImage when built WITH_QT, otherwise skipped with a log and the material
/// falls back to its color factors); samplers; nodes (TRS or matrix); scenes.
/// Not supported (skipped with a log): skins, animations, morph targets, Draco/meshopt
/// compression, points/lines primitives, alpha BLEND (rendered opaque).
///
/// Conventions: no exceptions — fallible APIs return bool + err string. Parsing needs no
/// GL context; `instantiateGltf` needs a current GL context (creates Mesh/Texture objects).
/// No third-party assets are vendored in this repo — tests use generated fixtures.

#include "json.hpp"

#include <fuhgl/gl/texture.hpp>
#include <fuhgl/math.hpp>
#include <fuhgl/render/material.hpp>
#include <fuhgl/render/mesh.hpp>

#include <string>
#include <vector>

namespace fuhglgraphics::scene {
class Scene;
class Node;
}

namespace fuhglgraphics::io {

/// @brief One glTF image: external URI or GLB-embedded bufferView; decoded RGBA8 when possible.
struct GltfImage {
    std::string uri;                  ///< external URI relative to the asset (percent-decoded)
    std::string mimeType;             ///< "image/png" / "image/jpeg"
    int bufferView = -1;              ///< GLB-embedded image data
    int width = 0, height = 0;        ///< decoded size
    std::vector<unsigned char> rgba;  ///< decoded RGBA8 (empty when decoding unavailable)
    bool decoded = false;
};

/// @brief One glTF texture (image + sampler).
struct GltfTexture {
    int source = -1;  ///< index into GltfAsset::images
    int sampler = -1; ///< index into samplers (filtering is approximated — see makeTexture)
    std::string name;
};

/// @brief One glTF material (KHR_materials_pbrMetallicRoughness subset).
struct GltfMaterial {
    std::string name;
    fuhgl::math::Vec4 baseColorFactor{1.f, 1.f, 1.f, 1.f};
    fuhgl::math::Vec3 emissiveFactor{0.f, 0.f, 0.f};
    float metallicFactor = 1.f;
    float roughnessFactor = 1.f;
    float normalScale = 1.f;
    int baseColorTexture = -1;          ///< index into GltfAsset::textures
    int normalTexture = -1;
    int occlusionTexture = -1;
    int emissiveTexture = -1;
    int metallicRoughnessTexture = -1;
    int alphaMode = 0;                  ///< 0 = OPAQUE, 1 = MASK, 2 = BLEND (unsupported -> opaque)
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

/// @brief One glTF mesh: one MeshData per primitive plus the primitive's material index.
struct GltfMesh {
    std::string name;
    fuhgl::math::Vec3 boundsMin{0.f, 0.f, 0.f}, boundsMax{0.f, 0.f, 0.f}; ///< POSITION accessor min/max
    std::vector<fuhgl::render::MeshData> primitives;
    std::vector<int> primitiveMaterials; ///< parallel to primitives (-1 = default material)
};

/// @brief One glTF node (TRS; a "matrix" node is decomposed into TRS at load time).
struct GltfNode {
    std::string name;
    fuhgl::math::Vec3 translation{0.f, 0.f, 0.f};
    fuhgl::math::Quat rotation{}; ///< identity by default
    fuhgl::math::Vec3 scale{1.f, 1.f, 1.f};
    int mesh = -1;
    std::vector<int> children;
};

/// @brief A parsed glTF 2.0 asset (plain data, no GL objects yet).
struct GltfAsset {
    std::vector<GltfMesh> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfImage> images;
    std::vector<GltfTexture> textures;
    std::vector<GltfNode> nodes;
    std::vector<std::vector<int>> scenes; ///< root node indices per scene
    int defaultScene = -1;
    std::string generator;
    std::string baseDir; ///< directory of the .gltf/.glb file (external buffers/images resolve against it)

    /// @brief Decodes image bytes (PNG/JPEG) to RGBA8 into `img`. Only available when the
    /// library is built WITH_QT (Qt QImage); otherwise returns false (caller keeps factors).
    static bool decodeImage(const std::vector<unsigned char>& bytes, GltfImage& img,
                            std::string* err = nullptr);
};

/// @brief Loads a glTF 2.0 file (.gltf JSON or .glb binary) into `out`. Pure CPU —
/// no GL context needed. Returns false + err on malformed input (bool + log convention).
bool loadGltf(const std::string& path, GltfAsset& out, std::string* err = nullptr);

/// @brief Owned GL resources of an instantiated glTF scene (G9 mapping).
struct GltfGpuResources {
    std::vector<fuhgl::render::Mesh> meshes;        ///< one Mesh per (glTF mesh, primitive)
    std::vector<fuhgl::render::Material> materials; ///< one preset Material per glTF material
    std::vector<fuhgl::Texture> textures;           ///< decoded images as GL textures
};

/// @brief Instantiates the asset's default scene into the G9 scene graph: one scene::Node
/// per glTF node (TRS preserved), each mesh node gets a MeshComponent with one MeshEntry
/// per primitive (GL mesh, preset material, castShadow = true, running material ID).
/// Creates the GL objects in `gpu` — needs a current GL context. `parent` defaults to
/// scene.root(). Returns false + err on failure.
bool instantiateGltf(const GltfAsset& asset, fuhglgraphics::scene::Scene& scene,
                     GltfGpuResources& gpu, fuhglgraphics::scene::Node* parent = nullptr,
                     std::string* err = nullptr);

} // namespace fuhglgraphics::io
