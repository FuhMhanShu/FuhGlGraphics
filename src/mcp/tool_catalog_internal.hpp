#pragma once

// Internal helpers shared by tool_catalog.cpp (scene/render/water tools + dispatch table)
// and tool_catalog_procedural.cpp (G24-G31 procedural tools). Not installed, not public API.

#include "fuhglgraphics/mcp/tool_catalog.hpp"

#include "fuhglgraphics/mcp/jsonrpc.hpp"
#include "fuhglgraphics/scene/component.hpp"
#include "fuhglgraphics/scene/node.hpp"

#include <fuhgl/math.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fuhglgraphics::mcp {
namespace tc {

using fuhgl::math::Vec2;
using fuhgl::math::Vec3;
using fuhgl::render::MeshData;

// --- JSON schema + payload helpers ---------------------------------------------------

inline io::Json prop(const std::string& type, const std::string& desc,
                     std::vector<std::string> enumVals = {}) {
    io::Json p = jsonObject({{"type", jsonString(type)}, {"description", jsonString(desc)}});
    if (!enumVals.empty()) {
        std::vector<io::Json> ev;
        for (const std::string& e : enumVals) ev.push_back(jsonString(e));
        p.obj["enum"] = jsonArray(std::move(ev));
    }
    return p;
}

inline io::Json arrProp(const std::string& itemType, const std::string& desc) {
    return jsonObject({{"type", jsonString("array")},
                       {"items", jsonObject({{"type", jsonString(itemType)}})},
                       {"description", jsonString(desc)}});
}

inline io::Json schema(std::map<std::string, io::Json> props, std::vector<std::string> required = {}) {
    io::Json s = jsonObject({{"type", jsonString("object")}, {"properties", jsonObject(std::move(props))}});
    if (!required.empty()) {
        std::vector<io::Json> r;
        for (const std::string& k : required) r.push_back(jsonString(k));
        s.obj["required"] = jsonArray(std::move(r));
    }
    return s;
}

inline io::Json payload(std::map<std::string, io::Json> members) { return jsonObject(std::move(members)); }

// --- argument getters (missing keys fall back to defaults) ---------------------------

inline double argNum(const io::Json& a, const char* k, double def) { return a.at(k).asNumber(def); }
inline int argInt(const io::Json& a, const char* k, int def) { return a.at(k).asInt(def); }
inline bool argBool(const io::Json& a, const char* k, bool def) { return a.at(k).asBool(def); }
inline std::string argStr(const io::Json& a, const char* k, const std::string& def = {}) {
    return a.at(k).asString(def);
}
inline Vec3 argVec3(const io::Json& a, const char* k, const Vec3& def) {
    const io::Json& v = a.at(k);
    if (v.type != io::Json::Type::Array || v.arr.size() < 3) return def;
    return {static_cast<float>(v.arr[0].asNumber(def.x)), static_cast<float>(v.arr[1].asNumber(def.y)),
            static_cast<float>(v.arr[2].asNumber(def.z))};
}
inline Vec2 argVec2(const io::Json& a, const char* k, const Vec2& def) {
    const io::Json& v = a.at(k);
    if (v.type != io::Json::Type::Array || v.arr.size() < 2) return def;
    return {static_cast<float>(v.arr[0].asNumber(def.x)), static_cast<float>(v.arr[1].asNumber(def.y))};
}
inline int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
inline float clampFloat(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

// --- scene helpers -------------------------------------------------------------------

inline scene::Node* findNode(scene::Scene& scene, const std::string& name) {
    if (name.empty() || name == "root") return &scene.root();
    std::vector<scene::Node*> stack{&scene.root()};
    while (!stack.empty()) {
        scene::Node* n = stack.back();
        stack.pop_back();
        if (n->name() == name) return n;
        for (const auto& c : n->children()) stack.push_back(c.get());
    }
    return nullptr;
}

inline scene::Node* findParentOf(scene::Scene& scene, const std::string& name, scene::Node** childOut) {
    std::vector<scene::Node*> stack{&scene.root()};
    while (!stack.empty()) {
        scene::Node* n = stack.back();
        stack.pop_back();
        for (const auto& c : n->children()) {
            if (c->name() == name) {
                *childOut = c.get();
                return n;
            }
            stack.push_back(c.get());
        }
    }
    return nullptr;
}

inline io::Json nodeToJson(const scene::Node& n) {
    std::vector<io::Json> children;
    for (const auto& c : n.children()) children.push_back(nodeToJson(*c));
    return payload({{"name", jsonString(n.name())},
                    {"translation", jsonArray({jsonNumber(n.translation.x), jsonNumber(n.translation.y),
                                                jsonNumber(n.translation.z)})},
                    {"rotation", jsonArray({jsonNumber(n.rotation.x), jsonNumber(n.rotation.y),
                                             jsonNumber(n.rotation.z), jsonNumber(n.rotation.w)})},
                    {"scale", jsonArray({jsonNumber(n.scale.x), jsonNumber(n.scale.y), jsonNumber(n.scale.z)})},
                    {"children", jsonArray(std::move(children))}});
}

/// @brief Attaches `md` as a new child node (GL mesh via host.meshDataResolver when set).
/// `materialId` (when non-zero) is stored on the entry — e.g. 10 for G38 bridge meshes so the
/// lighting pass can apply the waterline band to them. Returns nullptr when attaching is
/// impossible (no scene / no resolver).
inline scene::Node* attachMeshData(ToolHost& host, const std::string& name, const MeshData& md,
                                   scene::Node* parent, const Vec3& translation,
                                   std::uint32_t materialId = 0) {
    if (!host.scene || !parent || !host.meshDataResolver || !host.meshArena) return nullptr;
    scene::Node& node = parent->createChild(name);
    node.translation = translation;
    if (host.meshDataArena) host.meshDataArena->push_back(std::make_unique<MeshData>(md));
    auto mesh = host.meshDataResolver(md);
    if (!mesh) return nullptr;
    scene::MeshEntry& entry = node.attach<scene::MeshComponent>().addMesh(mesh.get());
    if (host.meshDataArena && !host.meshDataArena->empty()) entry.meshData = host.meshDataArena->back().get();
    if (materialId != 0) entry.materialId = materialId;
    host.meshArena->push_back(std::move(mesh));
    return &node;
}

inline io::Json meshStats(const MeshData& md) {
    Vec3 bmin{1e30f, 1e30f, 1e30f}, bmax{-1e30f, -1e30f, -1e30f};
    const std::size_t vc = md.vertexCount();
    for (std::size_t i = 0; i < vc; ++i) {
        const float* v = &md.vertices[i * MeshData::kFloatsPerVertex];
        bmin.x = std::min(bmin.x, v[0]); bmin.y = std::min(bmin.y, v[1]); bmin.z = std::min(bmin.z, v[2]);
        bmax.x = std::max(bmax.x, v[0]); bmax.y = std::max(bmax.y, v[1]); bmax.z = std::max(bmax.z, v[2]);
    }
    if (vc == 0) { bmin = bmax = Vec3{0.f, 0.f, 0.f}; }
    return payload({{"vertices", jsonNumber(static_cast<double>(vc))},
                    {"triangles", jsonNumber(static_cast<double>(md.indices.empty() ? vc / 3 : md.indices.size() / 3))},
                    {"boundsMin", jsonArray({jsonNumber(bmin.x), jsonNumber(bmin.y), jsonNumber(bmin.z)})},
                    {"boundsMax", jsonArray({jsonNumber(bmax.x), jsonNumber(bmax.y), jsonNumber(bmax.z)})}});
}

/// @brief meshStats + optional attach (attach=true) — the standard procedural tool result.
inline io::Json meshResult(ToolHost& host, const io::Json& a, const std::string& defaultName,
                           const MeshData& md) {
    io::Json r = meshStats(md);
    if (argBool(a, "attach", false)) {
        scene::Node* parent = host.scene ? findNode(*host.scene, argStr(a, "parent", "root")) : nullptr;
        scene::Node* node = attachMeshData(host, argStr(a, "name", defaultName), md, parent,
                                           argVec3(a, "translation", Vec3{0.f, 0.f, 0.f}));
        r.obj["attached"] = jsonBool(node != nullptr);
        r.obj["node"] = jsonString(node ? node->name() : argStr(a, "name", defaultName));
        if (!node) r.obj["reason"] = jsonString("no scene or GL mesh resolver in host");
    }
    return r;
}

/// @brief Unit box MeshData (12 triangles, per-face-ish normals) for WFC cell previews.
inline MeshData boxMeshData(const Vec3& half, const Vec3& color) {
    MeshData md;
    const float h[8][3] = {{-half.x, -half.y, -half.z}, {half.x, -half.y, -half.z},
                           {half.x, half.y, -half.z},   {-half.x, half.y, -half.z},
                           {-half.x, -half.y, half.z},  {half.x, -half.y, half.z},
                           {half.x, half.y, half.z},    {-half.x, half.y, half.z}};
    const unsigned int idx[36] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                  1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
    md.vertices.reserve(8 * MeshData::kFloatsPerVertex);
    for (int i = 0; i < 8; ++i) {
        const Vec3 n = fuhgl::math::normalize(Vec3{h[i][0], h[i][1], h[i][2]});
        const float v[11] = {h[i][0], h[i][1], h[i][2], n.x, n.y, n.z, color.x, color.y, color.z, 0.f, 0.f};
        for (float f : v) md.vertices.push_back(f);
    }
    md.indices.assign(std::begin(idx), std::end(idx));
    return md;
}

// --- tool implementations (one TU each group) -------------------------------------------

io::Json tSceneList(const io::Json& a, ToolHost& host, std::string* err);
io::Json tSceneAddNode(const io::Json& a, ToolHost& host, std::string* err);
io::Json tSceneRemoveNode(const io::Json& a, ToolHost& host, std::string* err);
io::Json tSceneSetTrs(const io::Json& a, ToolHost& host, std::string* err);
io::Json tSceneAddShape(const io::Json& a, ToolHost& host, std::string* err);
io::Json tSceneAddLight(const io::Json& a, ToolHost& host, std::string* err);
io::Json tRenderListEffects(const io::Json& a, ToolHost& host, std::string* err);
io::Json tRenderSetEffect(const io::Json& a, ToolHost& host, std::string* err);
io::Json tRenderGetSettings(const io::Json& a, ToolHost& host, std::string* err);
io::Json tRenderSetParam(const io::Json& a, ToolHost& host, std::string* err);
io::Json tRenderSetParams(const io::Json& a, ToolHost& host, std::string* err);
io::Json tWaterGetParams(const io::Json& a, ToolHost& host, std::string* err);
io::Json tWaterSetParams(const io::Json& a, ToolHost& host, std::string* err);
io::Json tWaterAddToScene(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralNoise(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralMarchingCubes(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralLsystem(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralWfc(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralVoronoi(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralSdf(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralCreature(const io::Json& a, ToolHost& host, std::string* err);
io::Json tProceduralWorldgen(const io::Json& a, ToolHost& host, std::string* err);

} // namespace tc
} // namespace fuhglgraphics::mcp
