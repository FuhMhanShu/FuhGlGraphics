#include "fuhglgraphics/io/glb_export.hpp"

#include "fuhglgraphics/io/json.hpp"

#include "fuhglgraphics/scene/component.hpp"
#include "fuhglgraphics/scene/node.hpp"

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

namespace fuhglgraphics::io {

namespace {

Json numJson(double d) {
    Json j;
    j.type = Json::Type::Number;
    j.number = d;
    return j;
}
Json strJson(const std::string& s) {
    Json j;
    j.type = Json::Type::String;
    j.str = s;
    return j;
}
Json arrJson(std::vector<Json> v) {
    Json j;
    j.type = Json::Type::Array;
    j.arr = std::move(v);
    return j;
}
Json objJson(std::map<std::string, Json> o) {
    Json j;
    j.type = Json::Type::Object;
    j.obj = std::move(o);
    return j;
}
Json vec3Json(const fuhgl::math::Vec3& v) {
    return arrJson({numJson(v.x), numJson(v.y), numJson(v.z)});
}

/// @brief Decomposes a Mat4 into TRS (translation = column 3, scale = column lengths,
/// rotation = normalized 3x3 as quat).
void decompose(const fuhgl::math::Mat4& m, fuhgl::math::Vec3& t, fuhgl::math::Quat& r,
               fuhgl::math::Vec3& s) {
    t = {m(3, 0), m(3, 1), m(3, 2)};
    const fuhgl::math::Vec3 c0{m(0, 0), m(0, 1), m(0, 2)};
    const fuhgl::math::Vec3 c1{m(1, 0), m(1, 1), m(1, 2)};
    const fuhgl::math::Vec3 c2{m(2, 0), m(2, 1), m(2, 2)};
    s = {fuhgl::math::length(c0), fuhgl::math::length(c1), fuhgl::math::length(c2)};
    fuhgl::math::Mat3 r3;
    for (int col = 0; col < 3; ++col) {
        const fuhgl::math::Vec3 c = col == 0 ? c0 : (col == 1 ? c1 : c2);
        const float l = s[col] > 1e-9f ? s[col] : 1.f;
        r3(col, 0) = c.x / l;
        r3(col, 1) = c.y / l;
        r3(col, 2) = c.z / l;
    }
    r = fuhgl::math::fromMat3(r3);
}

struct Exporter {
    Json nodes, meshes, materials, accessors, bufferViews;
    std::vector<unsigned char> bin;
    int skipped = 0;

    Exporter() {
        for (Json* j : {&nodes, &meshes, &materials, &accessors, &bufferViews})
            j->type = Json::Type::Array;
    }

    void appendBin(const void* data, std::size_t bytes) {
        const auto* p = static_cast<const unsigned char*>(data);
        bin.insert(bin.end(), p, p + bytes);
    }
    void padBin() {
        while (bin.size() % 4 != 0) bin.push_back(0);
    }

    int addMaterial(const scene::MeshEntry& e, const std::string& name) {
        Json pbr = objJson({
            {"baseColorFactor", arrJson({numJson(e.albedo.x), numJson(e.albedo.y), numJson(e.albedo.z), numJson(1.0)})},
            {"metallicFactor", numJson(e.metallic)},
            {"roughnessFactor", numJson(e.roughness)},
        });
        Json m = objJson({{"name", strJson(name)},
                          {"pbrMetallicRoughness", pbr},
                          {"emissiveFactor", vec3Json(e.emissive)}});
        materials.arr.push_back(m);
        return static_cast<int>(materials.arr.size()) - 1;
    }

    /// @brief Adds one mesh (accessors + bufferViews + bin + mesh json); returns mesh index.
    int addMesh(const scene::MeshEntry& e, const std::string& name) {
        const fuhgl::render::MeshData& md = *e.meshData;
        const std::size_t vcount = md.vertexCount();
        const std::size_t vbytes = md.vertices.size() * sizeof(float);

        const std::size_t vbOff = bin.size();
        appendBin(md.vertices.data(), vbytes);
        padBin();
        const std::size_t ibOff = bin.size();
        if (!md.indices.empty()) {
            appendBin(md.indices.data(), md.indices.size() * sizeof(unsigned int));
        } else { // non-indexed source: sequential indices
            std::vector<unsigned int> seq(vcount);
            for (std::size_t i = 0; i < vcount; ++i) seq[i] = static_cast<unsigned int>(i);
            appendBin(seq.data(), seq.size() * sizeof(unsigned int));
        }
        padBin();
        const std::size_t ibLen = bin.size() - ibOff;

        const int bvVert = static_cast<int>(bufferViews.arr.size());
        bufferViews.arr.push_back(objJson({{"buffer", numJson(0)},
                                           {"byteOffset", numJson(static_cast<double>(vbOff))},
                                           {"byteLength", numJson(static_cast<double>(vbytes))},
                                           {"target", numJson(34962)}}));
        const int bvIdx = static_cast<int>(bufferViews.arr.size());
        bufferViews.arr.push_back(objJson({{"buffer", numJson(0)},
                                           {"byteOffset", numJson(static_cast<double>(ibOff))},
                                           {"byteLength", numJson(static_cast<double>(ibLen))},
                                           {"target", numJson(34963)}}));

        // POSITION min/max
        fuhgl::math::Vec3 mn{0.f, 0.f, 0.f}, mx{0.f, 0.f, 0.f};
        for (std::size_t i = 0; i < vcount; ++i) {
            const float* p = &md.vertices[i * fuhgl::render::MeshData::kFloatsPerVertex];
            const fuhgl::math::Vec3 v{p[0], p[1], p[2]};
            if (i == 0) {
                mn = mx = v;
            } else {
                mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)};
                mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)};
            }
        }
        const int matIdx = addMaterial(e, name);
        auto acc = [&](int bv, double byteOffset, const std::string& type, int compType,
                       std::size_t count) {
            accessors.arr.push_back(objJson({{"bufferView", numJson(bv)},
                                             {"byteOffset", numJson(byteOffset)},
                                             {"byteStride", numJson(44)}, // interleaved 11 floats
                                             {"componentType", numJson(compType)},
                                             {"count", numJson(static_cast<double>(count))},
                                             {"type", strJson(type)}}));
            return static_cast<int>(accessors.arr.size()) - 1;
        };
        const int aPos = [&] {
            accessors.arr.push_back(objJson({{"bufferView", numJson(bvVert)},
                                             {"byteOffset", numJson(0)},
                                             {"byteStride", numJson(44)},
                                             {"componentType", numJson(5126)},
                                             {"count", numJson(static_cast<double>(vcount))},
                                             {"type", strJson("VEC3")},
                                             {"min", vec3Json(mn)},
                                             {"max", vec3Json(mx)}}));
            return static_cast<int>(accessors.arr.size()) - 1;
        }();
        const int aNrm = acc(bvVert, 12, "VEC3", 5126, vcount);
        const int aCol = acc(bvVert, 24, "VEC3", 5126, vcount);
        const int aUv = acc(bvVert, 36, "VEC2", 5126, vcount);
        const std::size_t icount = ibLen / sizeof(unsigned int);
        accessors.arr.push_back(objJson({{"bufferView", numJson(bvIdx)},
                                         {"componentType", numJson(5125)},
                                         {"count", numJson(static_cast<double>(icount))},
                                         {"type", strJson("SCALAR")}}));
        const int aIdx = static_cast<int>(accessors.arr.size()) - 1;

        Json prim = objJson({{"attributes", objJson({{"POSITION", numJson(aPos)},
                                                     {"NORMAL", numJson(aNrm)},
                                                     {"COLOR_0", numJson(aCol)},
                                                     {"TEXCOORD_0", numJson(aUv)}})},
                             {"indices", numJson(aIdx)},
                             {"material", numJson(matIdx)}});
        meshes.arr.push_back(objJson({{"name", strJson(name)}, {"primitives", arrJson({prim})}}));
        return static_cast<int>(meshes.arr.size()) - 1;
    }

    int addNode(const scene::Node& n) {
        Json j = objJson({{"name", strJson(n.name())},
                          {"translation", vec3Json(n.translation)},
                          {"rotation", arrJson({numJson(n.rotation.x), numJson(n.rotation.y),
                                                numJson(n.rotation.z), numJson(n.rotation.w)})},
                          {"scale", vec3Json(n.scale)}});
        const int self = static_cast<int>(nodes.arr.size());
        nodes.arr.push_back(j);
        Json children;
        children.type = Json::Type::Array;
        if (const scene::MeshComponent* mc = n.find<scene::MeshComponent>()) {
            int i = 0;
            for (const scene::MeshEntry& e : mc->meshes) {
                ++i;
                if (!e.meshData) {
                    ++skipped;
                    continue;
                }
                const int meshIdx = addMesh(e, n.name() + "#" + std::to_string(i));
                fuhgl::math::Vec3 t, s;
                fuhgl::math::Quat r;
                decompose(e.localOffset, t, r, s);
                const int childIdx = static_cast<int>(nodes.arr.size());
                nodes.arr.push_back(objJson({{"name", strJson(n.name() + "#" + std::to_string(i))},
                                             {"translation", vec3Json(t)},
                                             {"rotation", arrJson({numJson(r.x), numJson(r.y),
                                                                   numJson(r.z), numJson(r.w)})},
                                             {"scale", vec3Json(s)},
                                             {"mesh", numJson(meshIdx)}}));
                children.arr.push_back(numJson(childIdx));
            }
        }
        for (const auto& ch : n.children()) children.arr.push_back(numJson(addNode(*ch)));
        nodes.arr[self].obj["children"] = children;
        return self;
    }
};

} // namespace

bool exportSceneGlb(const scene::Scene& scene, const std::string& path, int* skipped,
                    std::string* err) {
    Exporter ex;
    const int rootIdx = ex.addNode(scene.root());

    Json root = objJson({
        {"asset", objJson({{"version", strJson("2.0")},
                           {"generator", strJson("fuhglgraphics io::exportSceneGlb")}})},
        {"scene", numJson(0)},
        {"scenes", arrJson({objJson({{"nodes", arrJson({numJson(rootIdx)})}})})},
        {"nodes", ex.nodes},
        {"meshes", ex.meshes},
        {"materials", ex.materials},
        {"accessors", ex.accessors},
        {"bufferViews", ex.bufferViews},
        {"buffers", arrJson({objJson({{"byteLength", numJson(static_cast<double>(ex.bin.size()))}})})},
    });

    std::string jsonStr = writeJson(root, 0);
    while (jsonStr.size() % 4 != 0) jsonStr.push_back(' ');
    auto& bin = ex.bin;
    while (bin.size() % 4 != 0) bin.push_back(0);

    // GLB container (little-endian; glTF mandates LE — fine on x86/ARM targets)
    const std::uint32_t total =
        12 + 8 + static_cast<std::uint32_t>(jsonStr.size()) + 8 + static_cast<std::uint32_t>(bin.size());
    std::vector<unsigned char> out;
    out.reserve(total);
    auto put32 = [&out](std::uint32_t v) {
        unsigned char b[4];
        std::memcpy(b, &v, 4);
        out.insert(out.end(), b, b + 4);
    };
    put32(0x46546C67u); // "glTF"
    put32(2u);
    put32(total);
    put32(static_cast<std::uint32_t>(jsonStr.size()));
    put32(0x4E4F534Au); // "JSON"
    out.insert(out.end(), jsonStr.begin(), jsonStr.end());
    put32(static_cast<std::uint32_t>(bin.size()));
    put32(0x004E4942u); // "BIN\0"
    out.insert(out.end(), bin.begin(), bin.end());

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
        if (err) *err = "write failed " + path;
        return false;
    }
    if (skipped) *skipped = ex.skipped;
    return true;
}

} // namespace fuhglgraphics::io
