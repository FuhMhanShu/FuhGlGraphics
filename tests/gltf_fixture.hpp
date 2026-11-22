#pragma once

// Shared generated glTF fixture for test_io.cpp / gl_smoke_gltf.cpp: a single quad
// (4 vertices POSITION/NORMAL/TEXCOORD_0 + 6 u16 indices) with one PBR material and a
// 2-node hierarchy (parent with translation, child with rotation+scale, child has the mesh).
// Written either as .gltf with a base64 data-URI buffer or as .glb (JSON + BIN chunks).
// Own generated fixture — no third-party assets (REQUIREMENTS N7).

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fuhglgraphicstest {

inline std::string base64Encode(const std::vector<unsigned char>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const unsigned a = in[i];
        const unsigned b = i + 1 < in.size() ? in[i + 1] : 0;
        const unsigned c = i + 2 < in.size() ? in[i + 2] : 0;
        out += t[a >> 2];
        out += t[((a & 3) << 4) | (b >> 4)];
        out += i + 1 < in.size() ? t[((b & 15) << 2) | (c >> 6)] : '=';
        out += i + 2 < in.size() ? t[c & 63] : '=';
    }
    return out;
}

inline void putU32(std::string& s, uint32_t v) {
    char b[4];
    std::memcpy(b, &v, 4);
    s.append(b, 4);
}

// Quad buffer: positions 48B (offset 0), normals 48B (48), uvs 32B (96), indices 12B (128) = 140B.
inline std::vector<unsigned char> buildQuadBuffer() {
    const float pos[4][3] = {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
    const float nrm[4][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    std::vector<unsigned char> b;
    auto append = [&b](const void* p, std::size_t n) {
        const auto* c = static_cast<const unsigned char*>(p);
        b.insert(b.end(), c, c + n);
    };
    append(pos, sizeof(pos));
    append(nrm, sizeof(nrm));
    append(uv, sizeof(uv));
    append(idx, sizeof(idx));
    return b;
}

// JSON of the fixture. `embedded`: buffers[0] carries a base64 data URI; otherwise the
// buffer is the GLB BIN chunk (no uri).
inline std::string buildQuadJson(bool embedded) {
    std::string uriPart;
    if (embedded)
        uriPart = ", \"uri\": \"data:application/octet-stream;base64," + base64Encode(buildQuadBuffer()) + "\"";
    std::string json = R"JSON({
  "asset": {"version": "2.0", "generator": "fuhglgraphics-test-fixture"},
  "scene": 0,
  "scenes": [{"nodes": [0], "name": "fixture"}],
  "nodes": [
    {"name": "parent", "translation": [2, 3, 4], "children": [1]},
    {"name": "quad", "mesh": 0, "rotation": [0, 0.7071068, 0, 0.7071068], "scale": [2, 2, 2]}
  ],
  "meshes": [{"name": "quadMesh", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0, "mode": 4}]}],
  "materials": [{"name": "mat", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.25, 0.1, 1.0], "metallicFactor": 0.9, "roughnessFactor": 0.3}, "doubleSided": true, "alphaMode": "MASK", "alphaCutoff": 0.4}],
  "buffers": [{"byteLength": 140@URI@}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32},
    {"buffer": 0, "byteOffset": 128, "byteLength": 12}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, 0, -1], "max": [1, 0, 1]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ]
})JSON";
    const std::string placeholder = "@URI@";
    const std::size_t pos = json.find(placeholder);
    if (pos != std::string::npos)
        json.replace(pos, placeholder.size(), uriPart);
    return json;
}

/// @brief Writes quad.gltf (data URI) and quad.glb (BIN chunk) into `dir`. Returns the .gltf path.
inline std::string writeGltfFixture(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    const std::vector<unsigned char> buffer = buildQuadBuffer();

    {
        std::ofstream f(dir / "quad.gltf", std::ios::binary);
        f << buildQuadJson(true);
    }

    std::string jsonGlb = buildQuadJson(false);
    std::string glb;
    putU32(glb, 0x46546C67u); // "glTF"
    putU32(glb, 2u);
    putU32(glb, 0u); // length placeholder
    const std::size_t jsonPad = (4 - jsonGlb.size() % 4) % 4;
    putU32(glb, static_cast<uint32_t>(jsonGlb.size() + jsonPad));
    putU32(glb, 0x4E4F534Au); // "JSON"
    glb += jsonGlb;
    glb.append(jsonPad, ' ');
    const std::size_t binPad = (4 - buffer.size() % 4) % 4;
    putU32(glb, static_cast<uint32_t>(buffer.size() + binPad));
    putU32(glb, 0x004E4942u); // "BIN\0"
    glb.append(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    glb.append(binPad, '\0');
    const uint32_t total = static_cast<uint32_t>(glb.size());
    std::memcpy(glb.data() + 8, &total, 4);
    {
        std::ofstream f(dir / "quad.glb", std::ios::binary);
        f << glb;
    }
    return (dir / "quad.gltf").string();
}

} // namespace fuhglgraphicstest
