#include "fuhglgraphics/io/gltf.hpp"

#include "fuhglgraphics/scene/component.hpp"
#include "fuhglgraphics/scene/node.hpp"
#include "fuhglgraphics/scene/scene.hpp"

#include <fuhgl/render/procedural.hpp>

#ifdef FUHGLGRAPHICS_WITH_QT
#include <QImage>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace fuhglgraphics::io {
namespace {

// --- small file/URI helpers -----------------------------------------------------------

bool readFileBytes(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(n));
    const bool ok = n == 0 || std::fread(out.data(), 1, static_cast<std::size_t>(n), f) == static_cast<std::size_t>(n);
    std::fclose(f);
    return ok;
}

std::string dirOf(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? std::string(".") : path.substr(0, pos);
}

std::string percentDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

// --- load context -----------------------------------------------------------------------

struct LoadContext {
    GltfAsset& asset;
    const Json* root = nullptr;      ///< parsed glTF JSON (accessors/bufferViews lookups)
    std::vector<std::vector<unsigned char>> buffers;
    std::vector<unsigned char> glbBin;
    bool hasGlbBin = false;
    std::string* err = nullptr;

    explicit LoadContext(GltfAsset& a) : asset(a) {}
    void log(const std::string& msg) const {
        std::fprintf(stderr, "fuhglgraphics::io::loadGltf: %s\n", msg.c_str());
        if (err && err->empty()) *err = msg;
    }
};

// --- accessor decoding ------------------------------------------------------------------

int compsForType(const std::string& t) {
    if (t == "SCALAR") return 1;
    if (t == "VEC2") return 2;
    if (t == "VEC3") return 3;
    if (t == "VEC4" || t == "MAT2") return 4;
    if (t == "MAT3") return 9;
    if (t == "MAT4") return 16;
    return 0;
}

int compSizeBytes(int componentType) {
    switch (componentType) {
        case 5120: // BYTE
        case 5121: // UNSIGNED_BYTE
            return 1;
        case 5122: // SHORT
        case 5123: // UNSIGNED_SHORT
            return 2;
        case 5125: // UNSIGNED_INT
        case 5126: // FLOAT
            return 4;
        default:
            return 0;
    }
}

float readComponent(const unsigned char* p, int componentType, bool normalized) {
    switch (componentType) {
        case 5120: {
            int8_t v;
            std::memcpy(&v, p, 1);
            return normalized ? std::max(static_cast<float>(v) / 127.f, -1.f) : static_cast<float>(v);
        }
        case 5121: {
            uint8_t v;
            std::memcpy(&v, p, 1);
            return normalized ? static_cast<float>(v) / 255.f : static_cast<float>(v);
        }
        case 5122: {
            int16_t v;
            std::memcpy(&v, p, 2);
            return normalized ? std::max(static_cast<float>(v) / 32767.f, -1.f) : static_cast<float>(v);
        }
        case 5123: {
            uint16_t v;
            std::memcpy(&v, p, 2);
            return normalized ? static_cast<float>(v) / 65535.f : static_cast<float>(v);
        }
        case 5125: {
            uint32_t v;
            std::memcpy(&v, p, 4);
            return static_cast<float>(v);
        }
        case 5126: {
            float v;
            std::memcpy(&v, p, 4);
            return v;
        }
        default:
            return 0.f;
    }
}

/// @brief Reads accessor `accIdx` as `count * comps` floats (normalized ints -> [0,1]/[-1,1]).
bool readAccessorFloats(const LoadContext& ctx, int accIdx, std::vector<float>& out, int& comps) {
    const Json& accessors = ctx.root->at("accessors");
    const Json& acc = accessors[static_cast<std::size_t>(accIdx)];
    if (!acc.isObject()) return false;
    const int componentType = acc.at("componentType").asInt();
    const int count = acc.at("count").asInt();
    const bool normalized = acc.at("normalized").asBool(false);
    const std::string type = acc.at("type").asString();
    comps = compsForType(type);
    const int cs = compSizeBytes(componentType);
    if (comps <= 0 || cs <= 0 || count < 0) return false;
    if (acc.contains("sparse")) ctx.log("sparse accessors are not supported; using base data");
    out.assign(static_cast<std::size_t>(count) * comps, 0.f);
    const Json* bvIdx = acc.find("bufferView");
    if (!bvIdx) return true; // no bufferView: all zeros (allowed by spec for sparse base)
    const Json& bufferViews = ctx.root->at("bufferViews");
    const Json& bv = bufferViews[static_cast<std::size_t>(bvIdx->asInt())];
    if (!bv.isObject()) return false;
    const int bufferIdx = bv.at("buffer").asInt();
    if (bufferIdx < 0 || bufferIdx >= static_cast<int>(ctx.buffers.size())) return false;
    const std::vector<unsigned char>& buf = ctx.buffers[static_cast<std::size_t>(bufferIdx)];
    const std::size_t bvOffset = static_cast<std::size_t>(bv.at("byteOffset").asInt(0));
    const std::size_t bvLength = static_cast<std::size_t>(bv.at("byteLength").asInt(0));
    const int elemSize = comps * cs;
    const std::size_t stride =
        bv.contains("byteStride") ? static_cast<std::size_t>(bv.at("byteStride").asInt()) : static_cast<std::size_t>(elemSize);
    const std::size_t accOffset = static_cast<std::size_t>(acc.at("byteOffset").asInt(0));
    if (stride < static_cast<std::size_t>(elemSize)) return false;
    if (accOffset + static_cast<std::size_t>(count > 0 ? count - 1 : 0) * stride + elemSize > bvLength ||
        bvOffset + bvLength > buf.size()) {
        ctx.log("accessor out of bufferView bounds");
        return false;
    }
    const unsigned char* base = buf.data() + bvOffset + accOffset;
    for (int e = 0; e < count; ++e)
        for (int c = 0; c < comps; ++c)
            out[static_cast<std::size_t>(e) * comps + c] =
                readComponent(base + static_cast<std::size_t>(e) * stride + static_cast<std::size_t>(c) * cs, componentType,
                              normalized);
    return true;
}

// --- buffers ----------------------------------------------------------------------------

bool loadBuffer(LoadContext& ctx, int bufferIdx) {
    const Json& buffers = ctx.root->at("buffers");
    const Json& buf = buffers[static_cast<std::size_t>(bufferIdx)];
    if (!buf.isObject()) return false;
    const Json* uri = buf.find("uri");
    if (!uri || !uri->isString()) {
        // GLB: buffer 0 without uri refers to the BIN chunk
        if (ctx.hasGlbBin && bufferIdx == 0) {
            ctx.buffers.push_back(ctx.glbBin);
            return true;
        }
        ctx.log("buffer without uri (and no GLB BIN chunk)");
        return false;
    }
    const std::string& u = uri->str;
    if (u.rfind("data:", 0) == 0) {
        const std::size_t pos = u.find("base64,");
        if (pos == std::string::npos) {
            ctx.log("data URI without base64 payload");
            return false;
        }
        std::vector<unsigned char> decoded;
        if (!base64Decode(u.substr(pos + 7), decoded)) {
            ctx.log("invalid base64 in data URI");
            return false;
        }
        ctx.buffers.push_back(std::move(decoded));
        return true;
    }
    std::vector<unsigned char> bytes;
    const std::string path = ctx.asset.baseDir + "/" + percentDecode(u);
    if (!readFileBytes(path, bytes)) {
        ctx.log("cannot read external buffer: " + path);
        return false;
    }
    ctx.buffers.push_back(std::move(bytes));
    return true;
}

// --- sections ---------------------------------------------------------------------------

void parseImages(LoadContext& ctx) {
    const Json& images = ctx.root->at("images");
    if (!images.isArray()) return;
    for (std::size_t i = 0; i < images.size(); ++i) {
        const Json& im = images[i];
        GltfImage out;
        out.mimeType = im.at("mimeType").asString();
        const Json* uri = im.find("uri");
        if (uri && uri->isString()) out.uri = percentDecode(uri->str);
        const Json* bv = im.find("bufferView");
        if (bv) out.bufferView = bv->asInt();
        // load raw bytes (external file or GLB-embedded) and decode when possible
        std::vector<unsigned char> bytes;
        if (out.bufferView >= 0) {
            const Json& bufferViews = ctx.root->at("bufferViews");
            const Json& bvv = bufferViews[static_cast<std::size_t>(out.bufferView)];
            const int bufferIdx = bvv.at("buffer").asInt();
            if (bufferIdx >= 0 && bufferIdx < static_cast<int>(ctx.buffers.size())) {
                const auto& buf = ctx.buffers[static_cast<std::size_t>(bufferIdx)];
                const std::size_t off = static_cast<std::size_t>(bvv.at("byteOffset").asInt(0));
                const std::size_t len = static_cast<std::size_t>(bvv.at("byteLength").asInt(0));
                if (off + len <= buf.size()) bytes.assign(buf.begin() + off, buf.begin() + off + len);
            }
        } else if (!out.uri.empty()) {
            if (!readFileBytes(ctx.asset.baseDir + "/" + out.uri, bytes))
                ctx.log("cannot read external image: " + out.uri);
        }
        if (!bytes.empty()) {
            std::string derr;
            if (GltfAsset::decodeImage(bytes, out, &derr)) {
                // decoded
            } else {
                ctx.log("image not decoded (" + derr + ") — material falls back to color factors");
            }
        }
        ctx.asset.images.push_back(std::move(out));
    }
}

void parseTextures(LoadContext& ctx) {
    const Json& textures = ctx.root->at("textures");
    if (!textures.isArray()) return;
    for (std::size_t i = 0; i < textures.size(); ++i) {
        const Json& t = textures[i];
        GltfTexture out;
        out.source = t.at("source").asInt(-1);
        out.sampler = t.at("sampler").asInt(-1);
        out.name = t.at("name").asString();
        ctx.asset.textures.push_back(out);
    }
}

fuhgl::math::Vec4 vec4Of(const Json& j, const fuhgl::math::Vec4& def) {
    if (!j.isArray() || j.arr.size() < 3) return def;
    fuhgl::math::Vec4 v = def;
    float* c[4] = {&v.x, &v.y, &v.z, &v.w};
    for (std::size_t i = 0; i < j.arr.size() && i < 4; ++i) *c[i] = static_cast<float>(j.arr[i].asNumber());
    return v;
}

fuhgl::math::Vec3 vec3Of(const Json& j, const fuhgl::math::Vec3& def) {
    if (!j.isArray() || j.arr.size() < 3) return def;
    return {static_cast<float>(j.arr[0].asNumber()), static_cast<float>(j.arr[1].asNumber()),
            static_cast<float>(j.arr[2].asNumber())};
}

void parseMaterials(LoadContext& ctx) {
    const Json& materials = ctx.root->at("materials");
    if (!materials.isArray()) return;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const Json& m = materials[i];
        GltfMaterial out;
        out.name = m.at("name").asString();
        const Json* pbr = m.find("pbrMetallicRoughness");
        if (pbr && pbr->isObject()) {
            out.baseColorFactor = vec4Of(pbr->at("baseColorFactor"), {1.f, 1.f, 1.f, 1.f});
            out.metallicFactor = static_cast<float>(pbr->at("metallicFactor").asNumber(1.0));
            out.roughnessFactor = static_cast<float>(pbr->at("roughnessFactor").asNumber(1.0));
            const Json* bct = pbr->find("baseColorTexture");
            if (bct) out.baseColorTexture = bct->at("index").asInt(-1);
        }
        const Json* nt = m.find("normalTexture");
        if (nt && nt->isObject()) {
            out.normalTexture = nt->at("index").asInt(-1);
            out.normalScale = static_cast<float>(nt->at("scale").asNumber(1.0));
        }
        const Json* ot = m.find("occlusionTexture");
        if (ot) out.occlusionTexture = ot->at("index").asInt(-1);
        const Json* et = m.find("emissiveTexture");
        if (et) out.emissiveTexture = et->at("index").asInt(-1);
        out.emissiveFactor = vec3Of(m.at("emissiveFactor"), {0.f, 0.f, 0.f});
        const Json* mr = pbr ? pbr->find("metallicRoughnessTexture") : nullptr;
        if (mr) out.metallicRoughnessTexture = mr->at("index").asInt(-1);
        const std::string alphaMode = m.at("alphaMode").asString("OPAQUE");
        out.alphaMode = alphaMode == "MASK" ? 1 : (alphaMode == "BLEND" ? 2 : 0);
        out.alphaCutoff = static_cast<float>(m.at("alphaCutoff").asNumber(0.5));
        out.doubleSided = m.at("doubleSided").asBool(false);
        ctx.asset.materials.push_back(std::move(out));
    }
}

/// @brief Converts a triangle list/strip/fan index list (mode 4/5/6) to a triangle list.
std::vector<unsigned int> toTriangles(const std::vector<unsigned int>& idx, int mode) {
    if (mode == 4) return idx;
    std::vector<unsigned int> out;
    if (mode == 5) { // TRIANGLE_STRIP
        for (std::size_t i = 0; i + 2 < idx.size(); ++i) {
            if (i % 2 == 0) out.insert(out.end(), {idx[i], idx[i + 1], idx[i + 2]});
            else out.insert(out.end(), {idx[i + 1], idx[i], idx[i + 2]});
        }
    } else if (mode == 6) { // TRIANGLE_FAN
        for (std::size_t i = 1; i + 1 < idx.size(); ++i)
            out.insert(out.end(), {idx[0], idx[i], idx[i + 1]});
    }
    return out;
}

bool parseMeshPrimitive(LoadContext& ctx, const Json& prim, GltfMesh& meshOut) {
    const Json& attrs = prim.at("attributes");
    const Json* posIdx = attrs.find("POSITION");
    if (!posIdx) {
        ctx.log("primitive without POSITION skipped");
        return false;
    }
    const int mode = prim.at("mode").asInt(4);
    if (prim.contains("extensions")) ctx.log("primitive extensions (e.g. Draco) unsupported — skipped if compressed");
    if (prim.at("extensions").contains("KHR_draco_mesh_compression")) {
        ctx.log("Draco-compressed primitive skipped");
        return false;
    }
    if (mode != 4 && mode != 5 && mode != 6) {
        ctx.log("primitive mode " + std::to_string(mode) + " (non-triangle) skipped");
        return false;
    }
    std::vector<float> pos, nrm, uv, col;
    int posComps = 0, nrmComps = 0, uvComps = 0, colComps = 0;
    if (!readAccessorFloats(ctx, posIdx->asInt(), pos, posComps) || posComps != 3) return false;
    const std::size_t n = pos.size() / 3;
    if (const Json* ni = attrs.find("NORMAL"))
        if (!readAccessorFloats(ctx, ni->asInt(), nrm, nrmComps)) return false;
    if (const Json* ti = attrs.find("TEXCOORD_0"))
        if (!readAccessorFloats(ctx, ti->asInt(), uv, uvComps)) return false;
    if (const Json* ci = attrs.find("COLOR_0"))
        if (!readAccessorFloats(ctx, ci->asInt(), col, colComps)) return false;
    // G6: skinning attributes JOINTS_0 (u8/u16, 4 per vertex) + WEIGHTS_0 (float vec4)
    std::vector<float> jointsF, weights;
    int jointsComps = 0, weightsComps = 0;
    if (const Json* ji = attrs.find("JOINTS_0")) {
        if (!readAccessorFloats(ctx, ji->asInt(), jointsF, jointsComps) || jointsComps != 4)
            ctx.log("JOINTS_0 with != 4 components ignored");
        else {
            std::vector<std::uint8_t> j(n * 4);
            for (std::size_t i = 0; i < n * 4 && i < jointsF.size(); ++i)
                j[i] = static_cast<std::uint8_t>(jointsF[i]);
            meshOut.primitiveJoints.push_back(std::move(j));
        }
    }
    if (const Json* wi = attrs.find("WEIGHTS_0")) {
        if (!readAccessorFloats(ctx, wi->asInt(), weights, weightsComps) || weightsComps != 4)
            ctx.log("WEIGHTS_0 with != 4 components ignored");
        else
            meshOut.primitiveWeights.push_back(weights);
    }

    std::vector<unsigned int> indices;
    if (const Json* ii = prim.find("indices")) {
        std::vector<float> raw;
        int ic = 0;
        if (!readAccessorFloats(ctx, ii->asInt(), raw, ic) || ic != 1) return false;
        indices.reserve(raw.size());
        for (float f : raw) indices.push_back(static_cast<unsigned int>(f));
    } else {
        indices.resize(n);
        for (std::size_t i = 0; i < n; ++i) indices[i] = static_cast<unsigned int>(i);
    }
    indices = toTriangles(indices, mode);

    fuhgl::render::MeshData data;
    data.vertices.resize(n * fuhgl::render::MeshData::kFloatsPerVertex);
    for (std::size_t i = 0; i < n; ++i) {
        float* v = &data.vertices[i * fuhgl::render::MeshData::kFloatsPerVertex];
        v[0] = pos[i * 3 + 0];
        v[1] = pos[i * 3 + 1];
        v[2] = pos[i * 3 + 2];
        if (nrmComps == 3 && i * 3 + 2 < nrm.size()) {
            v[3] = nrm[i * 3 + 0];
            v[4] = nrm[i * 3 + 1];
            v[5] = nrm[i * 3 + 2];
        } else {
            v[3] = 0.f;
            v[4] = 1.f;
            v[5] = 0.f;
        }
        if (colComps >= 3 && i * colComps + 2 < col.size()) {
            v[6] = col[i * colComps + 0];
            v[7] = col[i * colComps + 1];
            v[8] = col[i * colComps + 2];
        } else {
            v[6] = v[7] = v[8] = 1.f;
        }
        if (uvComps == 2 && i * 2 + 1 < uv.size()) {
            v[9] = uv[i * 2 + 0];
            v[10] = uv[i * 2 + 1];
        } else {
            v[9] = v[10] = 0.f;
        }
    }
    data.indices = std::move(indices);
    meshOut.primitives.push_back(std::move(data));
    meshOut.primitiveMaterials.push_back(prim.at("material").asInt(-1));
    // keep the skin attribute arrays parallel to primitives
    while (meshOut.primitiveJoints.size() < meshOut.primitives.size()) meshOut.primitiveJoints.push_back({});
    while (meshOut.primitiveWeights.size() < meshOut.primitives.size()) meshOut.primitiveWeights.push_back({});
    // bounds from the POSITION accessor min/max (or computed)
    const Json& accessors = ctx.root->at("accessors");
    const Json& posAcc = accessors[static_cast<std::size_t>(posIdx->asInt())];
    meshOut.boundsMin = vec3Of(posAcc.at("min"), meshOut.boundsMin);
    meshOut.boundsMax = vec3Of(posAcc.at("max"), meshOut.boundsMax);
    return true;
}

void parseMeshes(LoadContext& ctx) {
    const Json& meshes = ctx.root->at("meshes");
    if (!meshes.isArray()) return;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        const Json& m = meshes[i];
        GltfMesh mesh;
        mesh.name = m.at("name").asString();
        const Json& prims = m.at("primitives");
        if (prims.isArray())
            for (std::size_t p = 0; p < prims.size(); ++p) parseMeshPrimitive(ctx, prims[p], mesh);
        ctx.asset.meshes.push_back(std::move(mesh));
    }
}

void parseNodes(LoadContext& ctx) {
    const Json& nodes = ctx.root->at("nodes");
    if (!nodes.isArray()) return;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Json& nd = nodes[i];
        GltfNode out;
        out.name = nd.at("name").asString();
        out.mesh = nd.at("mesh").asInt(-1);
        out.skin = nd.at("skin").asInt(-1); // G6
        out.translation = vec3Of(nd.at("translation"), {0.f, 0.f, 0.f});
        const Json& rot = nd.at("rotation");
        if (rot.isArray() && rot.arr.size() == 4)
            out.rotation = fuhgl::math::Quat{static_cast<float>(rot.arr[0].asNumber()),
                                             static_cast<float>(rot.arr[1].asNumber()),
                                             static_cast<float>(rot.arr[2].asNumber()),
                                             static_cast<float>(rot.arr[3].asNumber())};
        out.scale = vec3Of(nd.at("scale"), {1.f, 1.f, 1.f});
        const Json& children = nd.at("children");
        if (children.isArray())
            for (const Json& c : children.arr) out.children.push_back(c.asInt(-1));
        const Json& matrix = nd.at("matrix");
        if (matrix.isArray() && matrix.arr.size() == 16) {
            // column-major 4x4 -> decompose into TRS
            fuhgl::math::Mat4 m;
            for (int k = 0; k < 16; ++k) m.m[k] = static_cast<float>(matrix.arr[static_cast<std::size_t>(k)].asNumber());
            out.translation = {m.m[12], m.m[13], m.m[14]};
            const fuhgl::math::Vec3 c0{m.m[0], m.m[1], m.m[2]}, c1{m.m[4], m.m[5], m.m[6]},
                c2{m.m[8], m.m[9], m.m[10]};
            fuhgl::math::Vec3 s{fuhgl::math::length(c0), fuhgl::math::length(c1), fuhgl::math::length(c2)};
            if (s.x <= 0.f) s.x = 1.f;
            if (s.y <= 0.f) s.y = 1.f;
            if (s.z <= 0.f) s.z = 1.f;
            out.scale = s;
            fuhgl::math::Mat3 r;
            r.m[0] = c0.x / s.x; r.m[1] = c0.y / s.x; r.m[2] = c0.z / s.x;
            r.m[3] = c1.x / s.y; r.m[4] = c1.y / s.y; r.m[5] = c1.z / s.y;
            r.m[6] = c2.x / s.z; r.m[7] = c2.y / s.z; r.m[8] = c2.z / s.z;
            out.rotation = fuhgl::math::normalize(fuhgl::math::fromMat3(r));
        }
        ctx.asset.nodes.push_back(std::move(out));
    }
}

void parseScenes(LoadContext& ctx) {
    const Json& scenes = ctx.root->at("scenes");
    if (scenes.isArray())
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            std::vector<int> roots;
            const Json& nodes = scenes[i].at("nodes");
            if (nodes.isArray())
                for (const Json& n : nodes.arr) roots.push_back(n.asInt(-1));
            ctx.asset.scenes.push_back(std::move(roots));
        }
    ctx.asset.defaultScene = ctx.root->at("scene").asInt(-1);
}

// G6: animations — channels (node + path) + samplers (input times / output values)
void parseAnimations(LoadContext& ctx) {
    const Json& anims = ctx.root->at("animations");
    if (!anims.isArray()) return;
    const Json& samplersOf = ctx.root->at("accessors"); // accessors shared
    (void)samplersOf;
    for (std::size_t i = 0; i < anims.size(); ++i) {
        const Json& a = anims[i];
        GltfAnimation anim;
        anim.name = a.at("name").asString("anim" + std::to_string(i));
        const Json& samplers = a.at("samplers");
        const Json& channels = a.at("channels");
        if (!channels.isArray()) {
            ctx.asset.animations.push_back(std::move(anim));
            continue;
        }
        for (const Json& ch : channels.arr) {
            GltfAnimChannel c;
            const Json& target = ch.at("target");
            c.node = target.at("node").asInt(-1);
            const std::string path = target.at("path").asString("");
            c.path = path == "translation" ? 0 : path == "rotation" ? 1 : path == "scale" ? 2 : -1;
            const int samp = ch.at("sampler").asInt(-1);
            if (samp >= 0 && samplers.isArray() && static_cast<std::size_t>(samp) < samplers.size()) {
                const Json& s = samplers[static_cast<std::size_t>(samp)];
                c.interpolation = s.at("interpolation").asString("LINEAR") == "LINEAR" ? 0 : 1;
                std::vector<float> t;
                int tc = 0;
                if (readAccessorFloats(ctx, s.at("input").asInt(-1), t, tc) && tc == 1) c.times = std::move(t);
                std::vector<float> v;
                int vc = 0;
                if (readAccessorFloats(ctx, s.at("output").asInt(-1), v, vc)) c.values = std::move(v);
            }
            if (c.node >= 0 && c.path >= 0) anim.channels.push_back(std::move(c));
        }
        ctx.asset.animations.push_back(std::move(anim));
    }
}

// G6: skins — joints (node indices) + inverseBindMatrices accessor (mat4 per joint)
void parseSkins(LoadContext& ctx) {
    const Json& skins = ctx.root->at("skins");
    if (!skins.isArray()) return;
    for (std::size_t i = 0; i < skins.size(); ++i) {
        const Json& s = skins[i];
        GltfSkin skin;
        skin.name = s.at("name").asString("skin" + std::to_string(i));
        skin.skeleton = s.at("skeleton").asInt(-1);
        const Json& joints = s.at("joints");
        if (joints.isArray())
            for (const Json& j : joints.arr) skin.joints.push_back(j.asInt(-1));
        const int ibm = s.at("inverseBindMatrices").asInt(-1);
        if (ibm >= 0) {
            std::vector<float> raw;
            int comps = 0;
            if (readAccessorFloats(ctx, ibm, raw, comps) && comps == 16) {
                for (std::size_t j = 0; j + 15 < raw.size(); j += 16) {
                    fuhgl::math::Mat4 m;
                    for (int k = 0; k < 16; ++k) m.m[k] = raw[j + static_cast<std::size_t>(k)];
                    skin.inverseBindMatrices.push_back(m);
                }
            }
        }
        ctx.asset.skins.push_back(std::move(skin));
    }
}

} // namespace

bool GltfAsset::decodeImage(const std::vector<unsigned char>& bytes, GltfImage& img, std::string* err) {
#ifdef FUHGLGRAPHICS_WITH_QT
    const QImage q = QImage::fromData(bytes.data(), static_cast<int>(bytes.size()));
    if (q.isNull()) {
        if (err) *err = "QImage could not decode the image bytes";
        return false;
    }
    const QImage rgba = q.convertToFormat(QImage::Format_RGBA8888);
    img.width = rgba.width();
    img.height = rgba.height();
    img.rgba.assign(rgba.constBits(), rgba.constBits() + static_cast<std::size_t>(rgba.width()) * rgba.height() * 4);
    img.decoded = true;
    return true;
#else
    (void)bytes;
    (void)img;
    if (err) *err = "image decoding requires a build with FUHGLGRAPHICS_WITH_QT=ON (Qt QImage)";
    return false;
#endif
}

bool loadGltf(const std::string& path, GltfAsset& out, std::string* err) {
    out = GltfAsset{};
    out.baseDir = dirOf(path);
    std::vector<unsigned char> file;
    if (!readFileBytes(path, file)) {
        if (err) *err = "cannot read file: " + path;
        return false;
    }
    if (file.size() < 4) {
        if (err) *err = "file too small: " + path;
        return false;
    }

    Json root;
    LoadContext ctx(out);
    ctx.err = err;

    const uint32_t magic = static_cast<uint32_t>(file[0]) | (static_cast<uint32_t>(file[1]) << 8) |
                           (static_cast<uint32_t>(file[2]) << 16) | (static_cast<uint32_t>(file[3]) << 24);
    if (magic == 0x46546C67u) { // "glTF" — GLB binary container
        if (file.size() < 12) {
            if (err) *err = "truncated GLB header";
            return false;
        }
        uint32_t version = 0, length = 0;
        std::memcpy(&version, file.data() + 4, 4);
        std::memcpy(&length, file.data() + 8, 4);
        if (version != 2) {
            if (err) *err = "unsupported GLB version " + std::to_string(version);
            return false;
        }
        if (length > file.size()) {
            if (err) *err = "GLB length field exceeds file size";
            return false;
        }
        std::size_t pos = 12;
        bool haveJson = false;
        while (pos + 8 <= length) {
            uint32_t clen = 0, ctype = 0;
            std::memcpy(&clen, file.data() + pos, 4);
            std::memcpy(&ctype, file.data() + pos + 4, 4);
            pos += 8;
            if (pos + clen > length) {
                if (err) *err = "GLB chunk exceeds container length";
                return false;
            }
            if (ctype == 0x4E4F534Au) { // "JSON"
                const std::string jsonText(reinterpret_cast<const char*>(file.data() + pos), clen);
                if (!parseJson(jsonText, root, err)) return false;
                haveJson = true;
            } else if (ctype == 0x004E4942u) { // "BIN\0"
                ctx.glbBin.assign(file.begin() + pos, file.begin() + pos + clen);
                ctx.hasGlbBin = true;
            }
            pos += clen;
            pos = (pos + 3) & ~static_cast<std::size_t>(3); // chunks are 4-byte aligned
        }
        if (!haveJson) {
            if (err) *err = "GLB without JSON chunk";
            return false;
        }
    } else {
        const std::string text(file.begin(), file.end());
        if (!parseJson(text, root, err)) {
            if (err) *err = "invalid .gltf JSON in " + path + ": " + *err;
            return false;
        }
    }

    const Json& assetInfo = root.at("asset");
    if (!assetInfo.isObject() || assetInfo.at("version").asString().rfind("2.", 0) != 0) {
        if (err) *err = "not a glTF 2.x asset: " + path;
        return false;
    }
    out.generator = assetInfo.at("generator").asString();
    ctx.root = &root;

    const Json& buffers = root.at("buffers");
    if (buffers.isArray())
        for (std::size_t i = 0; i < buffers.size(); ++i)
            if (!loadBuffer(ctx, static_cast<int>(i))) {
                if (err) *err = "failed to load buffer " + std::to_string(i);
                return false;
            }

    parseTextures(ctx);
    parseImages(ctx);
    parseMaterials(ctx);
    parseMeshes(ctx);
    parseNodes(ctx);
    parseScenes(ctx);
    parseAnimations(ctx); // G6
    parseSkins(ctx);      // G6
    return true;
}

bool instantiateGltf(const GltfAsset& asset, fuhglgraphics::scene::Scene& scene, GltfGpuResources& gpu,
                     fuhglgraphics::scene::Node* parent, std::string* err) {
    using fuhglgraphics::scene::MeshComponent;
    using fuhglgraphics::scene::MeshEntry;
    using fuhglgraphics::scene::Node;
    if (!parent) parent = &scene.root();

    gpu.meshes.clear();
    gpu.materials.clear();
    gpu.textures.clear();

    // --- textures (decoded images -> GL textures) ---
    gpu.textures.reserve(asset.images.size());
    std::vector<const fuhgl::Texture*> texPtr(asset.images.size(), nullptr);
    for (std::size_t i = 0; i < asset.images.size(); ++i) {
        const GltfImage& img = asset.images[i];
        if (!img.decoded) continue;
        gpu.textures.emplace_back();
        fuhgl::render::procedural::Image pimg;
        pimg.width = img.width;
        pimg.height = img.height;
        pimg.rgba = img.rgba;
        gpu.textures.back() = fuhgl::render::procedural::makeTexture(pimg, true);
        texPtr[i] = &gpu.textures.back();
    }
    const auto texturePtrOf = [&](int texIdx) -> const fuhgl::Texture* {
        if (texIdx < 0 || texIdx >= static_cast<int>(asset.textures.size())) return nullptr;
        const int src = asset.textures[static_cast<std::size_t>(texIdx)].source;
        if (src < 0 || src >= static_cast<int>(texPtr.size())) return nullptr;
        return texPtr[static_cast<std::size_t>(src)];
    };

    // --- materials (preset materials for the G3 G-buffer pass) ---
    gpu.materials.reserve(asset.materials.size());
    std::vector<const fuhgl::render::Material*> matPtr(asset.materials.size(), nullptr);
    for (std::size_t i = 0; i < asset.materials.size(); ++i) {
        const GltfMaterial& gm = asset.materials[i];
        gpu.materials.emplace_back();
        fuhgl::render::Material& m = gpu.materials.back();
        m.builtinShader = true;
        m.albedo = {gm.baseColorFactor.x, gm.baseColorFactor.y, gm.baseColorFactor.z};
        m.emissive = gm.emissiveFactor;
        m.metallic = gm.metallicFactor;
        m.roughness = gm.roughnessFactor;
        m.ao = 1.f; // occlusion texture not wired into the G-buffer pass yet
        m.albedoTexture = texturePtrOf(gm.baseColorTexture);
        m.normalMap = texturePtrOf(gm.normalTexture);
        if (gm.alphaMode == 2 && err && err->empty())
            *err = "glTF alphaMode BLEND is rendered opaque (no alpha blending in the G-buffer pass)";
        matPtr[i] = &m;
    }

    // --- meshes (one GL Mesh per primitive) ---
    std::size_t totalPrims = 0;
    for (const GltfMesh& gm : asset.meshes) totalPrims += gm.primitives.size();
    gpu.meshes.reserve(totalPrims);
    std::vector<std::vector<int>> meshGpuIdx(asset.meshes.size());
    for (std::size_t mi = 0; mi < asset.meshes.size(); ++mi) {
        const GltfMesh& gm = asset.meshes[mi];
        for (std::size_t pi = 0; pi < gm.primitives.size(); ++pi) {
            gpu.meshes.emplace_back();
            fuhgl::render::Mesh& mesh = gpu.meshes.back();
            if (!mesh.create()) {
                if (err) *err = "Mesh::create failed (no current GL context?)";
                return false;
            }
            mesh.setMeshData(gm.primitives[pi]);
            meshGpuIdx[mi].push_back(static_cast<int>(gpu.meshes.size() - 1));
        }
    }

    // --- nodes -> scene::Node tree ---
    std::uint32_t runningMatId = 1;
    gpu.nodePtrs.assign(asset.nodes.size(), nullptr);
    std::function<void(int, Node*)> buildNode = [&](int ni, Node* par) {
        if (ni < 0 || ni >= static_cast<int>(asset.nodes.size())) return;
        const GltfNode& gn = asset.nodes[static_cast<std::size_t>(ni)];
        Node& n = par->createChild(gn.name.empty() ? "node" : gn.name);
        gpu.nodePtrs[static_cast<std::size_t>(ni)] = &n; // G6
        n.translation = gn.translation;
        n.rotation = gn.rotation;
        n.scale = gn.scale;
        if (gn.mesh >= 0 && gn.mesh < static_cast<int>(asset.meshes.size())) {
            const GltfMesh& gm = asset.meshes[static_cast<std::size_t>(gn.mesh)];
            MeshComponent& mc = n.attach<MeshComponent>();
            for (std::size_t pi = 0; pi < gm.primitives.size(); ++pi) {
                MeshEntry& e = mc.addMesh(&gpu.meshes[static_cast<std::size_t>(meshGpuIdx[static_cast<std::size_t>(gn.mesh)][pi])]);
                const int mat = gm.primitiveMaterials[pi];
                e.material = (mat >= 0 && mat < static_cast<int>(matPtr.size())) ? matPtr[static_cast<std::size_t>(mat)]
                                                                                 : nullptr;
                e.castShadow = true;
                e.materialId = runningMatId++;
            }
        }
        for (int c : gn.children) buildNode(c, &n);
    };

    const int sceneIdx = asset.defaultScene >= 0 ? asset.defaultScene : 0;
    if (sceneIdx >= 0 && sceneIdx < static_cast<int>(asset.scenes.size()))
        for (int rootIdx : asset.scenes[static_cast<std::size_t>(sceneIdx)]) buildNode(rootIdx, parent);
    else if (!asset.scenes.empty())
        for (int rootIdx : asset.scenes.front()) buildNode(rootIdx, parent);

    // --- G6: animations -> AnimationPlayer clips (node index -> node name) ---
    gpu.player = anim::AnimationPlayer{};
    for (const GltfAnimation& ga : asset.animations) {
        anim::AnimationClip clip;
        clip.name = ga.name;
        float dur = 0.f;
        for (const GltfAnimChannel& ch : ga.channels) {
            if (ch.node < 0 || ch.node >= static_cast<int>(asset.nodes.size())) continue;
            anim::TrsChannel tc;
            tc.nodeName = asset.nodes[static_cast<std::size_t>(ch.node)].name;
            tc.times = ch.times;
            const int comps = ch.path == 1 ? 4 : 3;
            for (std::size_t k = 0; k + static_cast<std::size_t>(comps) <= ch.values.size();
                 k += static_cast<std::size_t>(comps)) {
                if (ch.path == 0)
                    tc.translations.push_back({ch.values[k], ch.values[k + 1], ch.values[k + 2]});
                else if (ch.path == 1)
                    tc.rotations.push_back(fuhgl::math::normalize(fuhgl::math::Quat{ch.values[k], ch.values[k + 1],
                                                                                       ch.values[k + 2], ch.values[k + 3]}));
                else
                    tc.scales.push_back({ch.values[k], ch.values[k + 1], ch.values[k + 2]});
            }
            if (!tc.times.empty()) dur = std::max(dur, tc.times.back());
            clip.channels.push_back(std::move(tc));
        }
        clip.duration = dur;
        gpu.player.addClip(clip);
    }

    // --- G6: skins -> Skeleton (joint node ptrs + IBMs) + skinned-mesh records ---
    gpu.skeletons.clear();
    gpu.skinnedMeshes.clear();
    for (std::size_t si = 0; si < asset.skins.size(); ++si) {
        const GltfSkin& gs = asset.skins[si];
        anim::Skeleton sk;
        sk.jointNodes = gs.joints;
        sk.inverseBindMatrices = gs.inverseBindMatrices;
        gpu.skeletons.push_back(std::move(sk));
    }
    for (std::size_t ni = 0; ni < asset.nodes.size(); ++ni) {
        const GltfNode& gn = asset.nodes[ni];
        if (gn.mesh < 0 || gn.skin < 0 || gn.mesh >= static_cast<int>(asset.meshes.size()) ||
            gn.skin >= static_cast<int>(asset.skins.size()))
            continue;
        const GltfMesh& gm = asset.meshes[static_cast<std::size_t>(gn.mesh)];
        for (std::size_t pi = 0; pi < gm.primitives.size(); ++pi) {
            if (pi >= gm.primitiveJoints.size() || gm.primitiveJoints[pi].empty()) continue;
            GltfGpuResources::SkinnedMesh sm;
            sm.node = static_cast<int>(ni);
            sm.skin = gn.skin;
            sm.primitive = pi;
            sm.base = gm.primitives[pi];
            sm.weights.joints = gm.primitiveJoints[pi];
            sm.weights.weights = pi < gm.primitiveWeights.size() ? gm.primitiveWeights[pi]
                                                                  : std::vector<float>{};
            gpu.skinnedMeshes.push_back(std::move(sm));
        }
    }
    return true;
}

} // namespace fuhglgraphics::io
