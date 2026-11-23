#include "fuhglgraphics/scene/serialize.hpp"

#include "fuhglgraphics/render/effects.hpp"
#include "fuhglgraphics/scene/component.hpp"
#include "fuhglgraphics/scene/node.hpp"

#include <fuhgl/render/mesh.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace fuhglgraphics::scene {

namespace {

io::Json vec3Json(const fuhgl::math::Vec3& v) {
    io::Json j;
    j.type = io::Json::Type::Array;
    for (float f : {v.x, v.y, v.z}) {
        io::Json n;
        n.type = io::Json::Type::Number;
        n.number = f;
        j.arr.push_back(n);
    }
    return j;
}

io::Json numJson(double v) {
    io::Json n;
    n.type = io::Json::Type::Number;
    n.number = v;
    return n;
}

io::Json strJson(const std::string& s) {
    io::Json j;
    j.type = io::Json::Type::String;
    j.str = s;
    return j;
}

io::Json boolJson(bool b) {
    io::Json j;
    j.type = io::Json::Type::Bool;
    j.boolean = b;
    return j;
}

io::Json mat4Json(const fuhgl::math::Mat4& m) {
    io::Json j;
    j.type = io::Json::Type::Array;
    for (int i = 0; i < 16; ++i) j.arr.push_back(numJson(m.data()[i]));
    return j;
}

fuhgl::math::Vec3 vec3Of(const io::Json& j, const fuhgl::math::Vec3& def) {
    if (j.type != io::Json::Type::Array || j.arr.size() < 3) return def;
    return {static_cast<float>(j.arr[0].asNumber(def.x)), static_cast<float>(j.arr[1].asNumber(def.y)),
            static_cast<float>(j.arr[2].asNumber(def.z))};
}

io::Json effectsToJson(const Effects& fx) {
    io::Json j;
    j.type = io::Json::Type::Object;
    io::Json list;
    list.type = io::Json::Type::Array;
    for (render::Effect e : fx.list) list.arr.push_back(strJson(render::effectName(e)));
    j.obj["list"] = list;
    io::Json p;
    p.type = io::Json::Type::Object;
    const EffectParams& q = fx.params;
    p.obj["exposure"] = numJson(q.exposure);
    p.obj["taaBlend"] = numJson(q.taaBlend);
    p.obj["upscaleSharpness"] = numJson(q.upscaleSharpness);
    p.obj["shadowStrength"] = numJson(q.shadowStrength);
    p.obj["shadowCenter"] = vec3Json(q.shadowCenter);
    p.obj["shadowRadius"] = numJson(q.shadowRadius);
    p.obj["shadowZNear"] = numJson(q.shadowZNear);
    p.obj["shadowZFar"] = numJson(q.shadowZFar);
    p.obj["shadowFilter"] = numJson(static_cast<int>(q.shadowFilter));
    p.obj["shadowSnapToTexels"] = boolJson(q.shadowSnapToTexels);
    p.obj["shadowCascadeBlend"] = numJson(q.shadowCascadeBlend);
    p.obj["shadowLightSize"] = numJson(q.shadowLightSize);
    p.obj["shadowMinVariance"] = numJson(q.shadowMinVariance);
    p.obj["shadowNormalOffset"] = numJson(q.shadowNormalOffset);
    p.obj["shadowPcfRadius"] = numJson(q.shadowPcfRadius);
    p.obj["renderScale"] = numJson(q.renderScale);
    p.obj["msaaSamples"] = numJson(q.msaaSamples);
    p.obj["shadowMapSize"] = numJson(q.shadowMapSize);
    p.obj["csmCascades"] = numJson(q.csmCascades);
    p.obj["lightProbeStrength"] = numJson(q.lightProbeStrength);
    p.obj["dofFocusDist"] = numJson(q.dofFocusDist);
    p.obj["dofStrength"] = numJson(q.dofStrength);
    p.obj["dofMaxCoC"] = numJson(q.dofMaxCoC);
    p.obj["ssaoRadius"] = numJson(q.ssaoRadius);
    p.obj["ssaoIntensity"] = numJson(q.ssaoIntensity);
    p.obj["ssaoBias"] = numJson(q.ssaoBias);
    p.obj["ssaoSamples"] = numJson(q.ssaoSamples);
    p.obj["ssrMaxSteps"] = numJson(q.ssrMaxSteps);
    p.obj["ssrThickness"] = numJson(q.ssrThickness);
    p.obj["ssrMaxDist"] = numJson(q.ssrMaxDist);
    p.obj["ssrStrength"] = numJson(q.ssrStrength);
    p.obj["bloomThreshold"] = numJson(q.bloomThreshold);
    p.obj["bloomStrength"] = numJson(q.bloomStrength);
    p.obj["bloomRadius"] = numJson(q.bloomRadius);
    p.obj["volumetricDensity"] = numJson(q.volumetricDensity);
    p.obj["volumetricCloudiness"] = numJson(q.volumetricCloudiness);
    p.obj["volumetricHeightFalloff"] = numJson(q.volumetricHeightFalloff);
    p.obj["volumetricNoiseScale"] = numJson(q.volumetricNoiseScale);
    p.obj["volumetricAnisotropy"] = numJson(q.volumetricAnisotropy);
    p.obj["volumetricMaxDist"] = numJson(q.volumetricMaxDist);
    p.obj["volumetricStrength"] = numJson(q.volumetricStrength);
    p.obj["volumetricSteps"] = numJson(q.volumetricSteps);
    p.obj["wetnessAmount"] = numJson(q.wetnessAmount);
    p.obj["wetnessPuddle"] = numJson(q.wetnessPuddle);
    p.obj["wetnessNoiseScale"] = numJson(q.wetnessNoiseScale);
    j.obj["params"] = p;
    return j;
}

bool effectsFromJson(const io::Json& j, Effects& fx, std::string* err) {
    fx = Effects{};
    const io::Json* list = j.find("list");
    if (list && list->type == io::Json::Type::Array) {
        for (const io::Json& e : list->arr) {
            render::Effect eff;
            if (!render::effectFromName(e.asString(), eff)) {
                if (err) *err = "unknown effect name: " + e.asString();
                return false;
            }
            fx.enable(eff);
        }
    }
    const io::Json* p = j.find("params");
    if (p && p->type == io::Json::Type::Object) {
        EffectParams& q = fx.params;
        q.exposure = static_cast<float>(p->at("exposure").asNumber(q.exposure));
        q.taaBlend = static_cast<float>(p->at("taaBlend").asNumber(q.taaBlend));
        q.upscaleSharpness = static_cast<float>(p->at("upscaleSharpness").asNumber(q.upscaleSharpness));
        q.shadowStrength = static_cast<float>(p->at("shadowStrength").asNumber(q.shadowStrength));
        q.shadowCenter = vec3Of(p->at("shadowCenter"), q.shadowCenter);
        q.shadowRadius = static_cast<float>(p->at("shadowRadius").asNumber(q.shadowRadius));
        q.shadowZNear = static_cast<float>(p->at("shadowZNear").asNumber(q.shadowZNear));
        q.shadowZFar = static_cast<float>(p->at("shadowZFar").asNumber(q.shadowZFar));
        q.shadowFilter = static_cast<fuhgl::render::ShadowFilter>(p->at("shadowFilter").asInt(static_cast<int>(q.shadowFilter)));
        q.shadowSnapToTexels = p->at("shadowSnapToTexels").asBool(q.shadowSnapToTexels);
        q.shadowCascadeBlend = static_cast<float>(p->at("shadowCascadeBlend").asNumber(q.shadowCascadeBlend));
        q.shadowLightSize = static_cast<float>(p->at("shadowLightSize").asNumber(q.shadowLightSize));
        q.shadowMinVariance = static_cast<float>(p->at("shadowMinVariance").asNumber(q.shadowMinVariance));
        q.shadowNormalOffset = static_cast<float>(p->at("shadowNormalOffset").asNumber(q.shadowNormalOffset));
        q.shadowPcfRadius = static_cast<float>(p->at("shadowPcfRadius").asNumber(q.shadowPcfRadius));
        q.renderScale = static_cast<float>(p->at("renderScale").asNumber(q.renderScale));
        q.msaaSamples = p->at("msaaSamples").asInt(q.msaaSamples);
        q.shadowMapSize = p->at("shadowMapSize").asInt(q.shadowMapSize);
        q.csmCascades = p->at("csmCascades").asInt(q.csmCascades);
        q.lightProbeStrength = static_cast<float>(p->at("lightProbeStrength").asNumber(q.lightProbeStrength));
        q.dofFocusDist = static_cast<float>(p->at("dofFocusDist").asNumber(q.dofFocusDist));
        q.dofStrength = static_cast<float>(p->at("dofStrength").asNumber(q.dofStrength));
        q.dofMaxCoC = static_cast<float>(p->at("dofMaxCoC").asNumber(q.dofMaxCoC));
        q.ssaoRadius = static_cast<float>(p->at("ssaoRadius").asNumber(q.ssaoRadius));
        q.ssaoIntensity = static_cast<float>(p->at("ssaoIntensity").asNumber(q.ssaoIntensity));
        q.ssaoBias = static_cast<float>(p->at("ssaoBias").asNumber(q.ssaoBias));
        q.ssaoSamples = p->at("ssaoSamples").asInt(q.ssaoSamples);
        q.ssrMaxSteps = p->at("ssrMaxSteps").asInt(q.ssrMaxSteps);
        q.ssrThickness = static_cast<float>(p->at("ssrThickness").asNumber(q.ssrThickness));
        q.ssrMaxDist = static_cast<float>(p->at("ssrMaxDist").asNumber(q.ssrMaxDist));
        q.ssrStrength = static_cast<float>(p->at("ssrStrength").asNumber(q.ssrStrength));
        q.bloomThreshold = static_cast<float>(p->at("bloomThreshold").asNumber(q.bloomThreshold));
        q.bloomStrength = static_cast<float>(p->at("bloomStrength").asNumber(q.bloomStrength));
        q.bloomRadius = static_cast<float>(p->at("bloomRadius").asNumber(q.bloomRadius));
        q.volumetricDensity = static_cast<float>(p->at("volumetricDensity").asNumber(q.volumetricDensity));
        q.volumetricCloudiness = static_cast<float>(p->at("volumetricCloudiness").asNumber(q.volumetricCloudiness));
        q.volumetricHeightFalloff = static_cast<float>(p->at("volumetricHeightFalloff").asNumber(q.volumetricHeightFalloff));
        q.volumetricNoiseScale = static_cast<float>(p->at("volumetricNoiseScale").asNumber(q.volumetricNoiseScale));
        q.volumetricAnisotropy = static_cast<float>(p->at("volumetricAnisotropy").asNumber(q.volumetricAnisotropy));
        q.volumetricMaxDist = static_cast<float>(p->at("volumetricMaxDist").asNumber(q.volumetricMaxDist));
        q.volumetricStrength = static_cast<float>(p->at("volumetricStrength").asNumber(q.volumetricStrength));
        q.volumetricSteps = p->at("volumetricSteps").asInt(q.volumetricSteps);
        q.wetnessAmount = static_cast<float>(p->at("wetnessAmount").asNumber(q.wetnessAmount));
        q.wetnessPuddle = static_cast<float>(p->at("wetnessPuddle").asNumber(q.wetnessPuddle));
        q.wetnessNoiseScale = static_cast<float>(p->at("wetnessNoiseScale").asNumber(q.wetnessNoiseScale));
    }
    return true;
}

io::Json lightToJson(const fuhgl::render::Light& l) {
    io::Json j;
    j.type = io::Json::Type::Object;
    j.obj["position"] = vec3Json(l.position);
    j.obj["color"] = vec3Json(l.color);
    j.obj["intensity"] = numJson(l.intensity);
    j.obj["type"] = numJson(static_cast<int>(l.type));
    j.obj["direction"] = vec3Json(l.direction);
    j.obj["attenuationConstant"] = numJson(l.attenuationConstant);
    j.obj["attenuationLinear"] = numJson(l.attenuationLinear);
    j.obj["attenuationQuadratic"] = numJson(l.attenuationQuadratic);
    j.obj["range"] = numJson(l.range);
    j.obj["coneInner"] = numJson(l.coneInner);
    j.obj["coneOuter"] = numJson(l.coneOuter);
    return j;
}

fuhgl::render::Light lightFromJson(const io::Json& j) {
    fuhgl::render::Light l;
    l.position = vec3Of(j.at("position"), l.position);
    l.color = vec3Of(j.at("color"), l.color);
    l.intensity = static_cast<float>(j.at("intensity").asNumber(l.intensity));
    l.type = static_cast<fuhgl::render::LightType>(j.at("type").asInt(static_cast<int>(l.type)));
    l.direction = vec3Of(j.at("direction"), l.direction);
    l.attenuationConstant = static_cast<float>(j.at("attenuationConstant").asNumber(l.attenuationConstant));
    l.attenuationLinear = static_cast<float>(j.at("attenuationLinear").asNumber(l.attenuationLinear));
    l.attenuationQuadratic = static_cast<float>(j.at("attenuationQuadratic").asNumber(l.attenuationQuadratic));
    l.range = static_cast<float>(j.at("range").asNumber(l.range));
    l.coneInner = static_cast<float>(j.at("coneInner").asNumber(l.coneInner));
    l.coneOuter = static_cast<float>(j.at("coneOuter").asNumber(l.coneOuter));
    return l;
}

io::Json decalToJson(const render::Decal& d) {
    io::Json j;
    j.type = io::Json::Type::Object;
    j.obj["halfExtents"] = vec3Json(d.halfExtents);
    j.obj["albedo"] = vec3Json(d.albedo);
    j.obj["opacity"] = numJson(d.opacity);
    j.obj["materialIdMask"] = numJson(d.materialIdMask);
    return j;
}

io::Json clipToJson(const anim::AnimationClip& c) {
    io::Json j;
    j.type = io::Json::Type::Object;
    j.obj["name"] = strJson(c.name);
    j.obj["duration"] = numJson(c.duration);
    io::Json chs;
    chs.type = io::Json::Type::Array;
    for (const anim::TrsChannel& ch : c.channels) {
        io::Json cj;
        cj.type = io::Json::Type::Object;
        cj.obj["nodeName"] = strJson(ch.nodeName);
        auto arrOf = [](const std::vector<float>& v) {
            io::Json a;
            a.type = io::Json::Type::Array;
            for (float f : v) a.arr.push_back(numJson(f));
            return a;
        };
        cj.obj["times"] = arrOf(ch.times);
        io::Json ts, rs, ss;
        ts.type = rs.type = ss.type = io::Json::Type::Array;
        for (const auto& t : ch.translations) ts.arr.push_back(vec3Json(t));
        for (const auto& r : ch.rotations) {
            io::Json q;
            q.type = io::Json::Type::Array;
            for (float f : {r.x, r.y, r.z, r.w}) q.arr.push_back(numJson(f));
            rs.arr.push_back(q);
        }
        for (const auto& s : ch.scales) ss.arr.push_back(vec3Json(s));
        cj.obj["translations"] = ts;
        cj.obj["rotations"] = rs;
        cj.obj["scales"] = ss;
        chs.arr.push_back(cj);
    }
    j.obj["channels"] = chs;
    return j;
}

io::Json nodeToJson(const Node& n) {
    io::Json j;
    j.type = io::Json::Type::Object;
    j.obj["name"] = strJson(n.name());
    j.obj["translation"] = vec3Json(n.translation);
    {
        io::Json q;
        q.type = io::Json::Type::Array;
        for (float f : {n.rotation.x, n.rotation.y, n.rotation.z, n.rotation.w}) q.arr.push_back(numJson(f));
        j.obj["rotation"] = q;
    }
    j.obj["scale"] = vec3Json(n.scale);
    j.obj["effects"] = effectsToJson(n.effects);
    {
        io::Json in;
        in.type = io::Json::Type::Object;
        in.obj["enabled"] = boolJson(n.instancing.enabled);
        in.obj["count"] = numJson(n.instancing.count);
        in.obj["layout"] = strJson(n.instancing.layout == InstanceLayout::Grid ? "grid" : "random");
        in.obj["extent"] = vec3Json(n.instancing.extent);
        in.obj["seed"] = numJson(n.instancing.seed);
        j.obj["instancing"] = in;
    }
    io::Json comps;
    comps.type = io::Json::Type::Array;
    for (const auto& c : n.components()) {
        if (const MeshComponent* mc = dynamic_cast<const MeshComponent*>(c.get())) {
            io::Json m;
            m.type = io::Json::Type::Object;
            m.obj["type"] = strJson("mesh");
            io::Json entries;
            entries.type = io::Json::Type::Array;
            for (const MeshEntry& e : mc->meshes) {
                io::Json ej;
                ej.type = io::Json::Type::Object;
                // mesh spec: procedural shapes only (GL meshes are re-created by the resolver);
                // the glTF loader keeps its own path — here we store the entry's data + a
                // shape hint when the mesh is a known primitive (box/quad) is not recoverable,
                // so mesh-less entries serialize with "mesh": null unless the app provides a spec
                ej.obj["mesh"] = io::Json{}; // null — filled by the resolver on load
                ej.obj["localOffset"] = mat4Json(e.localOffset);
                ej.obj["castShadow"] = boolJson(e.castShadow);
                ej.obj["materialId"] = numJson(e.materialId);
                ej.obj["albedo"] = vec3Json(e.albedo);
                ej.obj["emissive"] = vec3Json(e.emissive);
                ej.obj["metallic"] = numJson(e.metallic);
                ej.obj["roughness"] = numJson(e.roughness);
                ej.obj["ao"] = numJson(e.ao);
                entries.arr.push_back(ej);
            }
            m.obj["entries"] = entries;
            comps.arr.push_back(m);
        } else if (const LightComponent* lc = dynamic_cast<const LightComponent*>(c.get())) {
            io::Json m;
            m.type = io::Json::Type::Object;
            m.obj["type"] = strJson("light");
            m.obj["light"] = lightToJson(lc->light);
            comps.arr.push_back(m);
        } else if (const DecalComponent* dc = dynamic_cast<const DecalComponent*>(c.get())) {
            io::Json m;
            m.type = io::Json::Type::Object;
            m.obj["type"] = strJson("decal");
            m.obj["decal"] = decalToJson(dc->decal);
            comps.arr.push_back(m);
        } else if (const AnimationComponent* ac = dynamic_cast<const AnimationComponent*>(c.get())) {
            io::Json m;
            m.type = io::Json::Type::Object;
            m.obj["type"] = strJson("animation");
            io::Json clips;
            clips.type = io::Json::Type::Array;
            for (const anim::AnimationClip& cl : ac->player.clips()) clips.arr.push_back(clipToJson(cl));
            m.obj["clips"] = clips;
            comps.arr.push_back(m);
        }
        // ParticleEmitterComponent / LightProbeComponent / WaterComponent: GL resources, skipped
    }
    j.obj["components"] = comps;
    io::Json children;
    children.type = io::Json::Type::Array;
    for (const auto& ch : n.children()) children.arr.push_back(nodeToJson(*ch));
    j.obj["children"] = children;
    return j;
}

bool nodeFromJson(Node& n, const io::Json& j, const MeshResolver& resolveMesh,
                  std::vector<std::unique_ptr<fuhgl::render::Mesh>>& arena, std::string* err,
                  int depth) {
    if (depth > 64) {
        if (err) *err = "node hierarchy too deep";
        return false;
    }
    n.setName(j.at("name").asString(n.name()));
    n.translation = vec3Of(j.at("translation"), n.translation);
    {
        const io::Json& q = j.at("rotation");
        if (q.type == io::Json::Type::Array && q.arr.size() >= 4)
            n.rotation = fuhgl::math::Quat{static_cast<float>(q.arr[0].asNumber(0)),
                                            static_cast<float>(q.arr[1].asNumber(0)),
                                            static_cast<float>(q.arr[2].asNumber(0)),
                                            static_cast<float>(q.arr[3].asNumber(1))};
    }
    n.scale = vec3Of(j.at("scale"), n.scale);
    {
        const io::Json& fx = j.at("effects");
        if (fx.type == io::Json::Type::Object && !effectsFromJson(fx, n.effects, err)) return false;
    }
    {
        const io::Json& in = j.at("instancing");
        if (in.type == io::Json::Type::Object) {
            n.instancing.enabled = in.at("enabled").asBool(false);
            n.instancing.count = in.at("count").asInt(0);
            n.instancing.layout = in.at("layout").asString() == "random" ? InstanceLayout::Random
                                                                          : InstanceLayout::Grid;
            n.instancing.extent = vec3Of(in.at("extent"), n.instancing.extent);
            n.instancing.seed = static_cast<unsigned>(in.at("seed").asNumber(1));
        }
    }
    const io::Json* comps = j.find("components");
    if (comps && comps->type == io::Json::Type::Array) {
        for (const io::Json& c : comps->arr) {
            const std::string type = c.at("type").asString();
            if (type == "mesh") {
                MeshComponent& mc = n.attach<MeshComponent>();
                const io::Json* entries = c.find("entries");
                if (entries && entries->type == io::Json::Type::Array) {
                    for (const io::Json& ej : entries->arr) {
                        MeshEntry e;
                        const io::Json* ms = ej.find("mesh");
                        if (ms && ms->type != io::Json::Type::Null && resolveMesh) {
                            if (auto m = resolveMesh(*ms)) {
                                arena.push_back(std::move(m));
                                e.mesh = arena.back().get();
                            }
                        }
                        const io::Json& lo = ej.at("localOffset");
                        if (lo.type == io::Json::Type::Array && lo.arr.size() >= 16) {
                            for (int i = 0; i < 16; ++i)
                                e.localOffset.data()[i] = static_cast<float>(lo.arr[static_cast<std::size_t>(i)].asNumber(0));
                        }
                        e.castShadow = ej.at("castShadow").asBool(true);
                        e.materialId = static_cast<std::uint32_t>(ej.at("materialId").asNumber(0));
                        e.albedo = vec3Of(ej.at("albedo"), e.albedo);
                        e.emissive = vec3Of(ej.at("emissive"), e.emissive);
                        e.metallic = static_cast<float>(ej.at("metallic").asNumber(e.metallic));
                        e.roughness = static_cast<float>(ej.at("roughness").asNumber(e.roughness));
                        e.ao = static_cast<float>(ej.at("ao").asNumber(e.ao));
                        mc.meshes.push_back(e);
                    }
                }
            } else if (type == "light") {
                n.attach<LightComponent>().light = lightFromJson(c.at("light"));
            } else if (type == "decal") {
                const io::Json& d = c.at("decal");
                DecalComponent& dc = n.attach<DecalComponent>();
                dc.decal.halfExtents = vec3Of(d.at("halfExtents"), dc.decal.halfExtents);
                dc.decal.albedo = vec3Of(d.at("albedo"), dc.decal.albedo);
                dc.decal.opacity = static_cast<float>(d.at("opacity").asNumber(dc.decal.opacity));
                dc.decal.materialIdMask = static_cast<std::uint32_t>(d.at("materialIdMask").asNumber(static_cast<double>(dc.decal.materialIdMask)));
            } else if (type == "animation") {
                AnimationComponent& ac = n.attach<AnimationComponent>();
                const io::Json* clips = c.find("clips");
                if (clips && clips->type == io::Json::Type::Array) {
                    for (const io::Json& cj : clips->arr) {
                        anim::AnimationClip clip;
                        clip.name = cj.at("name").asString();
                        clip.duration = static_cast<float>(cj.at("duration").asNumber(0));
                        const io::Json* chs = cj.find("channels");
                        if (chs && chs->type == io::Json::Type::Array) {
                            for (const io::Json& chj : chs->arr) {
                                anim::TrsChannel ch;
                                ch.nodeName = chj.at("nodeName").asString();
                                auto floatsOf = [](const io::Json& a) {
                                    std::vector<float> v;
                                    if (a.type == io::Json::Type::Array)
                                        for (const io::Json& x : a.arr)
                                            v.push_back(static_cast<float>(x.asNumber(0)));
                                    return v;
                                };
                                ch.times = floatsOf(chj.at("times"));
                                const io::Json& ts = chj.at("translations");
                                if (ts.type == io::Json::Type::Array)
                                    for (const io::Json& t : ts.arr)
                                        ch.translations.push_back(vec3Of(t, {0, 0, 0}));
                                const io::Json& rs = chj.at("rotations");
                                if (rs.type == io::Json::Type::Array)
                                    for (const io::Json& q : rs.arr)
                                        if (q.type == io::Json::Type::Array && q.arr.size() >= 4)
                                            ch.rotations.push_back(fuhgl::math::Quat{
                                                static_cast<float>(q.arr[0].asNumber(0)),
                                                static_cast<float>(q.arr[1].asNumber(0)),
                                                static_cast<float>(q.arr[2].asNumber(0)),
                                                static_cast<float>(q.arr[3].asNumber(1))});
                                const io::Json& ss = chj.at("scales");
                                if (ss.type == io::Json::Type::Array)
                                    for (const io::Json& sv : ss.arr)
                                        ch.scales.push_back(vec3Of(sv, {1, 1, 1}));
                                clip.channels.push_back(std::move(ch));
                            }
                        }
                        ac.player.addClip(clip);
                    }
                }
            }
        }
    }
    const io::Json* children = j.find("children");
    if (children && children->type == io::Json::Type::Array) {
        for (const io::Json& chj : children->arr) {
            Node& child = n.createChild();
            if (!nodeFromJson(child, chj, resolveMesh, arena, err, depth + 1)) return false;
        }
    }
    return true;
}

} // namespace

std::unique_ptr<fuhgl::render::Mesh> defaultShapeResolver(const io::Json& meshSpec) {
    if (meshSpec.type != io::Json::Type::Object) return nullptr;
    const std::string shape = meshSpec.at("shape").asString();
    const fuhgl::math::Vec3 color = vec3Of(meshSpec.at("color"), {1.f, 1.f, 1.f});
    auto mesh = std::make_unique<fuhgl::render::Mesh>();
    if (!mesh->create()) return nullptr;
    if (shape == "box") {
        const fuhgl::math::Vec3 he = vec3Of(meshSpec.at("halfExtents"), {0.5f, 0.5f, 0.5f});
        mesh->setMeshData(fuhgl::render::shapes::boxData(he, color));
        return mesh;
    }
    if (shape == "quad") {
        const float hs = static_cast<float>(meshSpec.at("halfSize").asNumber(0.5));
        mesh->setMeshData(fuhgl::render::shapes::quadData(hs, color));
        return mesh;
    }
    return nullptr;
}

io::Json sceneToJson(const Scene& scene) {
    io::Json j;
    j.type = io::Json::Type::Object;
    j.obj["format"] = strJson("fuhglgraphics-scene");
    j.obj["version"] = numJson(1);
    j.obj["root"] = nodeToJson(scene.root());
    return j;
}

bool sceneFromJson(Scene& scene, const io::Json& j, const MeshResolver& resolveMesh,
                   std::vector<std::unique_ptr<fuhgl::render::Mesh>>& meshArena,
                   std::string* err) {
    if (j.type != io::Json::Type::Object) {
        if (err) *err = "scene JSON must be an object";
        return false;
    }
    const io::Json* root = j.find("root");
    if (!root || root->type != io::Json::Type::Object) {
        if (err) *err = "scene JSON has no root object";
        return false;
    }
    // rebuild the root in place: drop all children + components first
    Node& r = scene.root();
    r.clearChildren();
    while (r.components().size() > 0) r.detach(r.components().front().get());
    return nodeFromJson(r, *root, resolveMesh, meshArena, err, 0);
}

bool saveScene(const Scene& scene, const std::string& path, std::string* err) {
    return io::writeJsonFile(path, sceneToJson(scene), err);
}

bool loadScene(Scene& scene, const std::string& path, const MeshResolver& resolveMesh,
               std::vector<std::unique_ptr<fuhgl::render::Mesh>>& meshArena, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    io::Json j;
    if (!io::parseJson(ss.str(), j, err)) return false;
    return sceneFromJson(scene, j, resolveMesh, meshArena, err);
}

} // namespace fuhglgraphics::scene
