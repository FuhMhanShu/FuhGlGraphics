#include "fuhglgraphics/scene/serialize.hpp"

#include <fuhglgraphics/scene/component.hpp>

#include <fuhgl/render/light.hpp>

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace fuhglgraphics::scene {

namespace {

using io::Json;

Json num(double n) {
    Json j;
    j.type = Json::Type::Number;
    j.number = n;
    return j;
}
Json str(const std::string& s) {
    Json j;
    j.type = Json::Type::String;
    j.str = s;
    return j;
}
Json boolean(bool b) {
    Json j;
    j.type = Json::Type::Bool;
    j.boolean = b;
    return j;
}
Json arr(std::initializer_list<Json> items) {
    Json j;
    j.type = Json::Type::Array;
    j.arr.assign(items.begin(), items.end());
    return j;
}
Json vec3(const fuhgl::math::Vec3& v) { return arr({num(v.x), num(v.y), num(v.z)}); }
Json vec4(const fuhgl::math::Vec4& v) { return arr({num(v.x), num(v.y), num(v.z), num(v.w)}); }
Json quat(const fuhgl::math::Quat& q) { return arr({num(q.x), num(q.y), num(q.z), num(q.w)}); }
Json mat4(const fuhgl::math::Mat4& m) {
    Json j = arr({});
    for (int i = 0; i < 16; ++i) j.arr.push_back(num(m.m[i]));
    return j;
}

fuhgl::math::Vec3 asVec3(const Json& j, const fuhgl::math::Vec3& def) {
    if (!j.isArray() || j.arr.size() != 3) return def;
    return {static_cast<float>(j.arr[0].asNumber()), static_cast<float>(j.arr[1].asNumber()),
            static_cast<float>(j.arr[2].asNumber())};
}
fuhgl::math::Quat asQuat(const Json& j, const fuhgl::math::Quat& def) {
    if (!j.isArray() || j.arr.size() != 4) return def;
    return {static_cast<float>(j.arr[0].asNumber()), static_cast<float>(j.arr[1].asNumber()),
            static_cast<float>(j.arr[2].asNumber()), static_cast<float>(j.arr[3].asNumber())};
}
fuhgl::math::Mat4 asMat4(const Json& j, const fuhgl::math::Mat4& def) {
    fuhgl::math::Mat4 m = def;
    if (j.isArray() && j.arr.size() == 16)
        for (int i = 0; i < 16; ++i) m.m[i] = static_cast<float>(j.arr[i].asNumber());
    return m;
}

// --- stable effect keys (order matches render::Effect) ------------------------------
const char* const kEffectKeys[] = {"taa",       "upscale",  "tonemap",  "msaa",   "shadows",
                                   "csm_shadows", "ibl",    "point_shadows", "contact_shadows",
                                   "particles"};
static_assert(sizeof(kEffectKeys) / sizeof(kEffectKeys[0]) == render::kEffectCount,
              "effect key table must match render::Effect");

render::Effect effectFromKey(const std::string& key, bool& ok) {
    for (int i = 0; i < render::kEffectCount; ++i)
        if (key == kEffectKeys[i]) return static_cast<render::Effect>(i);
    ok = false;
    return render::Effect::Taa;
}

Json serializeParams(const EffectParams& p) {
    Json j;
    j.type = Json::Type::Object;
    auto& o = j.obj;
    o["exposure"] = num(p.exposure);
    o["taaBlend"] = num(p.taaBlend);
    o["upscaleSharpness"] = num(p.upscaleSharpness);
    o["shadowStrength"] = num(p.shadowStrength);
    o["shadowCenter"] = vec3(p.shadowCenter);
    o["shadowRadius"] = num(p.shadowRadius);
    o["shadowZNear"] = num(p.shadowZNear);
    o["shadowZFar"] = num(p.shadowZFar);
    o["shadowFilter"] = num(static_cast<int>(p.shadowFilter));
    o["shadowSnapToTexels"] = boolean(p.shadowSnapToTexels);
    o["shadowCascadeBlend"] = num(p.shadowCascadeBlend);
    o["shadowLightSize"] = num(p.shadowLightSize);
    o["shadowMinVariance"] = num(p.shadowMinVariance);
    o["shadowNormalOffset"] = num(p.shadowNormalOffset);
    o["shadowPcfRadius"] = num(p.shadowPcfRadius);
    o["renderScale"] = num(p.renderScale);
    o["msaaSamples"] = num(p.msaaSamples);
    o["shadowMapSize"] = num(p.shadowMapSize);
    o["csmCascades"] = num(p.csmCascades);
    return j;
}

void deserializeParams(const Json& j, EffectParams& p) {
    if (!j.isObject()) return;
    p.exposure = static_cast<float>(j.at("exposure").asNumber(p.exposure));
    p.taaBlend = static_cast<float>(j.at("taaBlend").asNumber(p.taaBlend));
    p.upscaleSharpness = static_cast<float>(j.at("upscaleSharpness").asNumber(p.upscaleSharpness));
    p.shadowStrength = static_cast<float>(j.at("shadowStrength").asNumber(p.shadowStrength));
    p.shadowCenter = asVec3(j.at("shadowCenter"), p.shadowCenter);
    p.shadowRadius = static_cast<float>(j.at("shadowRadius").asNumber(p.shadowRadius));
    p.shadowZNear = static_cast<float>(j.at("shadowZNear").asNumber(p.shadowZNear));
    p.shadowZFar = static_cast<float>(j.at("shadowZFar").asNumber(p.shadowZFar));
    p.shadowFilter = static_cast<fuhgl::render::ShadowFilter>(j.at("shadowFilter").asInt(static_cast<int>(p.shadowFilter)));
    p.shadowSnapToTexels = j.at("shadowSnapToTexels").asBool(p.shadowSnapToTexels);
    p.shadowCascadeBlend = static_cast<float>(j.at("shadowCascadeBlend").asNumber(p.shadowCascadeBlend));
    p.shadowLightSize = static_cast<float>(j.at("shadowLightSize").asNumber(p.shadowLightSize));
    p.shadowMinVariance = static_cast<float>(j.at("shadowMinVariance").asNumber(p.shadowMinVariance));
    p.shadowNormalOffset = static_cast<float>(j.at("shadowNormalOffset").asNumber(p.shadowNormalOffset));
    p.shadowPcfRadius = static_cast<float>(j.at("shadowPcfRadius").asNumber(p.shadowPcfRadius));
    p.renderScale = static_cast<float>(j.at("renderScale").asNumber(p.renderScale));
    p.msaaSamples = j.at("msaaSamples").asInt(p.msaaSamples);
    p.shadowMapSize = j.at("shadowMapSize").asInt(p.shadowMapSize);
    p.csmCascades = j.at("csmCascades").asInt(p.csmCascades);
}

Json serializeEffects(const Effects& fx) {
    Json j;
    j.type = Json::Type::Object;
    Json list;
    list.type = Json::Type::Array;
    for (render::Effect e : fx.list)
        list.arr.push_back(str(kEffectKeys[static_cast<int>(e)]));
    j.obj["list"] = list;
    j.obj["params"] = serializeParams(fx.params);
    return j;
}

bool deserializeEffects(const Json& j, Effects& fx, std::string* err) {
    if (!j.isObject()) return true;
    const Json& list = j.at("list");
    if (list.isArray()) {
        bool ok = true;
        for (const Json& e : list.arr) {
            if (!e.isString()) continue;
            fx.enable(effectFromKey(e.str, ok));
        }
        if (!ok) {
            if (err) *err = "unknown effect key in effects.list";
            return false;
        }
    }
    deserializeParams(j.at("params"), fx.params);
    return true;
}

Json serializeInstancing(const Instancing& in) {
    Json j;
    j.type = Json::Type::Object;
    j.obj["enabled"] = boolean(in.enabled);
    j.obj["count"] = num(in.count);
    j.obj["layout"] = str(in.layout == InstanceLayout::Grid ? "grid" : "random");
    j.obj["extent"] = vec3(in.extent);
    j.obj["seed"] = num(in.seed);
    return j;
}

void deserializeInstancing(const Json& j, Instancing& in) {
    if (!j.isObject()) return;
    in.enabled = j.at("enabled").asBool(in.enabled);
    in.count = j.at("count").asInt(in.count);
    const std::string layout = j.at("layout").asString();
    in.layout = layout == "random" ? InstanceLayout::Random : InstanceLayout::Grid;
    in.extent = asVec3(j.at("extent"), in.extent);
    in.seed = static_cast<unsigned>(j.at("seed").asInt(static_cast<int>(in.seed)));
}

Json serializeMeshEntry(const MeshEntry& e) {
    Json j;
    j.type = Json::Type::Object;
    auto& o = j.obj;
    if (!e.assetRef.empty()) o["asset"] = str(e.assetRef);
    o["localOffset"] = mat4(e.localOffset);
    o["castShadow"] = boolean(e.castShadow);
    o["materialId"] = num(e.materialId);
    o["albedo"] = vec3(e.albedo);
    if (!e.albedoTexturePath.empty()) o["albedoTexture"] = str(e.albedoTexturePath);
    if (!e.normalMapPath.empty()) o["normalMap"] = str(e.normalMapPath);
    o["emissive"] = vec3(e.emissive);
    o["metallic"] = num(e.metallic);
    o["roughness"] = num(e.roughness);
    o["ao"] = num(e.ao);
    if (e.hasAabb) {
        o["aabbMin"] = vec3(e.aabbMin);
        o["aabbMax"] = vec3(e.aabbMax);
    }
    return j;
}

void deserializeMeshEntry(const Json& j, MeshEntry& e) {
    if (!j.isObject()) return;
    e.assetRef = j.at("asset").asString();
    e.localOffset = asMat4(j.at("localOffset"), fuhgl::math::Mat4{});
    e.castShadow = j.at("castShadow").asBool(e.castShadow);
    e.materialId = static_cast<std::uint32_t>(j.at("materialId").asInt(static_cast<int>(e.materialId)));
    e.albedo = asVec3(j.at("albedo"), e.albedo);
    e.albedoTexturePath = j.at("albedoTexture").asString();
    e.normalMapPath = j.at("normalMap").asString();
    e.emissive = asVec3(j.at("emissive"), e.emissive);
    e.metallic = static_cast<float>(j.at("metallic").asNumber(e.metallic));
    e.roughness = static_cast<float>(j.at("roughness").asNumber(e.roughness));
    e.ao = static_cast<float>(j.at("ao").asNumber(e.ao));
    if (j.contains("aabbMin") && j.contains("aabbMax")) {
        e.hasAabb = true;
        e.aabbMin = asVec3(j.at("aabbMin"), e.aabbMin);
        e.aabbMax = asVec3(j.at("aabbMax"), e.aabbMax);
    }
}

Json serializeLight(const fuhgl::render::Light& l) {
    Json j;
    j.type = Json::Type::Object;
    auto& o = j.obj;
    o["type"] = num(static_cast<int>(l.type));
    o["position"] = vec3(l.position);
    o["color"] = vec3(l.color);
    o["intensity"] = num(l.intensity);
    o["direction"] = vec3(l.direction);
    o["attenuationConstant"] = num(l.attenuationConstant);
    o["attenuationLinear"] = num(l.attenuationLinear);
    o["attenuationQuadratic"] = num(l.attenuationQuadratic);
    o["range"] = num(l.range);
    o["coneInner"] = num(l.coneInner);
    o["coneOuter"] = num(l.coneOuter);
    return j;
}

void deserializeLight(const Json& j, fuhgl::render::Light& l) {
    if (!j.isObject()) return;
    l.type = static_cast<fuhgl::render::LightType>(j.at("type").asInt(static_cast<int>(l.type)));
    l.position = asVec3(j.at("position"), l.position);
    l.color = asVec3(j.at("color"), l.color);
    l.intensity = static_cast<float>(j.at("intensity").asNumber(l.intensity));
    l.direction = asVec3(j.at("direction"), l.direction);
    l.attenuationConstant = static_cast<float>(j.at("attenuationConstant").asNumber(l.attenuationConstant));
    l.attenuationLinear = static_cast<float>(j.at("attenuationLinear").asNumber(l.attenuationLinear));
    l.attenuationQuadratic = static_cast<float>(j.at("attenuationQuadratic").asNumber(l.attenuationQuadratic));
    l.range = static_cast<float>(j.at("range").asNumber(l.range));
    l.coneInner = static_cast<float>(j.at("coneInner").asNumber(l.coneInner));
    l.coneOuter = static_cast<float>(j.at("coneOuter").asNumber(l.coneOuter));
}

Json serializeNode(const Node& node) {
    Json j;
    j.type = Json::Type::Object;
    auto& o = j.obj;
    o["name"] = str(node.name());
    o["translation"] = vec3(node.translation);
    o["rotation"] = quat(node.rotation);
    o["scale"] = vec3(node.scale);
    o["effects"] = serializeEffects(node.effects);
    o["instancing"] = serializeInstancing(node.instancing);

    Json comps;
    comps.type = Json::Type::Array;
    for (const auto& c : node.components()) {
        Json cj;
        cj.type = Json::Type::Object;
        if (const auto* mc = dynamic_cast<const MeshComponent*>(c.get())) {
            cj.obj["type"] = str("mesh");
            Json meshes;
            meshes.type = Json::Type::Array;
            for (const MeshEntry& e : mc->meshes) meshes.arr.push_back(serializeMeshEntry(e));
            cj.obj["meshes"] = meshes;
            comps.arr.push_back(cj);
        } else if (const auto* lc = dynamic_cast<const LightComponent*>(c.get())) {
            cj.obj["type"] = str("light");
            cj.obj["light"] = serializeLight(lc->light);
            comps.arr.push_back(cj);
        }
        // CameraComponent / ParticleEmitterComponent / AnimationComponent: runtime
        // GL/animation state — intentionally not serialized (keeps the roundtrip stable).
    }
    o["components"] = comps;

    Json children;
    children.type = Json::Type::Array;
    for (const auto& ch : node.children()) children.arr.push_back(serializeNode(*ch));
    o["children"] = children;
    return j;
}

bool deserializeNode(Node& node, const Json& j, std::string* err) {
    if (!j.isObject()) {
        if (err) *err = "node entry is not an object";
        return false;
    }
    node.setName(j.at("name").asString());
    node.translation = asVec3(j.at("translation"), node.translation);
    node.rotation = asQuat(j.at("rotation"), node.rotation);
    node.scale = asVec3(j.at("scale"), node.scale);
    if (!deserializeEffects(j.at("effects"), node.effects, err)) return false;
    deserializeInstancing(j.at("instancing"), node.instancing);

    const Json& comps = j.at("components");
    if (comps.isArray()) {
        for (const Json& cj : comps.arr) {
            const std::string type = cj.at("type").asString();
            if (type == "mesh") {
                auto& mc = node.attach<MeshComponent>();
                const Json& meshes = cj.at("meshes");
                if (meshes.isArray())
                    for (const Json& mj : meshes.arr) {
                        MeshEntry e;
                        deserializeMeshEntry(mj, e);
                        mc.meshes.push_back(e); // mesh = nullptr; editor creates GL on load
                    }
            } else if (type == "light") {
                auto& lc = node.attach<LightComponent>();
                deserializeLight(cj.at("light"), lc.light);
            }
            // "camera" and runtime components are intentionally not restored
        }
    }

    const Json& children = j.at("children");
    if (children.isArray())
        for (const Json& ch : children.arr) {
            Node& child = node.createChild();
            if (!deserializeNode(child, ch, err)) return false;
        }
    return true;
}

} // namespace

bool serializeScene(const Scene& scene, io::Json& out, std::string* err) {
    (void)err;
    out = serializeNode(scene.root()); // the root node object incl. children
    return true;
}

bool deserializeScene(Scene& scene, const io::Json& in, std::string* err) {
    if (!in.isObject()) {
        if (err) *err = "scene JSON is not an object";
        return false;
    }
    if (scene.root().childCount() != 0 || !scene.root().components().empty()) {
        if (err) *err = "deserializeScene: scene must be fresh/empty";
        return false;
    }
    return deserializeNode(scene.root(), in, err);
}

} // namespace fuhglgraphics::scene
