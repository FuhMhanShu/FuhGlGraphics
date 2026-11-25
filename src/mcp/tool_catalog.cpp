#include "tool_catalog_internal.hpp"

#include <fuhgl/render/light.hpp>

namespace fuhglgraphics::mcp {
namespace tc {

// --- scene tools -----------------------------------------------------------------------

io::Json tSceneList(const io::Json& /*a*/, ToolHost& host, std::string* err) {
    if (!host.scene) { if (err) *err = "no scene in host"; return jsonNull(); }
    return payload({{"root", nodeToJson(host.scene->root())}});
}

io::Json tSceneAddNode(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.scene) { if (err) *err = "no scene in host"; return jsonNull(); }
    scene::Node* parent = findNode(*host.scene, argStr(a, "parent", "root"));
    if (!parent) { if (err) *err = "parent not found: " + argStr(a, "parent"); return jsonNull(); }
    std::string name = argStr(a, "name");
    if (name.empty()) name = "node" + std::to_string(parent->childCount() + 1);
    scene::Node& n = parent->createChild(name);
    if (a.contains("translation")) n.translation = argVec3(a, "translation", n.translation);
    if (a.contains("scale")) n.scale = argVec3(a, "scale", n.scale);
    if (a.contains("rotation")) {
        const io::Json& r = a.at("rotation");
        if (r.isArray() && r.arr.size() >= 4)
            n.rotation = fuhgl::math::Quat{static_cast<float>(r.arr[0].asNumber(0)),
                                           static_cast<float>(r.arr[1].asNumber(0)),
                                           static_cast<float>(r.arr[2].asNumber(0)),
                                           static_cast<float>(r.arr[3].asNumber(1))};
    }
    return payload({{"node", jsonString(n.name())}, {"parent", jsonString(parent->name())}});
}

io::Json tSceneRemoveNode(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.scene) { if (err) *err = "no scene in host"; return jsonNull(); }
    const std::string name = argStr(a, "name");
    if (name.empty() || name == "root") { if (err) *err = "cannot remove root"; return jsonNull(); }
    scene::Node* child = nullptr;
    scene::Node* parent = findParentOf(*host.scene, name, &child);
    if (!parent) { if (err) *err = "node not found: " + name; return jsonNull(); }
    parent->removeChild(child);
    return payload({{"removed", jsonString(name)}});
}

io::Json tSceneSetTrs(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.scene) { if (err) *err = "no scene in host"; return jsonNull(); }
    scene::Node* n = findNode(*host.scene, argStr(a, "name"));
    if (!n) { if (err) *err = "node not found: " + argStr(a, "name"); return jsonNull(); }
    if (a.contains("translation")) n->translation = argVec3(a, "translation", n->translation);
    if (a.contains("scale")) n->scale = argVec3(a, "scale", n->scale);
    if (a.contains("rotation")) {
        const io::Json& r = a.at("rotation");
        if (r.isArray() && r.arr.size() >= 4)
            n->rotation = fuhgl::math::Quat{static_cast<float>(r.arr[0].asNumber(0)),
                                            static_cast<float>(r.arr[1].asNumber(0)),
                                            static_cast<float>(r.arr[2].asNumber(0)),
                                            static_cast<float>(r.arr[3].asNumber(1))};
    }
    const io::Json j = nodeToJson(*n);
    return payload({{"node", jsonString(n->name())},
                    {"translation", j.at("translation")},
                    {"rotation", j.at("rotation")},
                    {"scale", j.at("scale")}});
}

io::Json tSceneAddShape(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.scene) { if (err) *err = "no scene in host"; return jsonNull(); }
    const std::string shape = argStr(a, "shape", "box");
    if (shape != "box" && shape != "quad" && shape != "sphere") {
        if (err) *err = "unknown shape: " + shape + " (box|quad|sphere)";
        return jsonNull();
    }
    scene::Node* parent = findNode(*host.scene, argStr(a, "parent", "root"));
    if (!parent) { if (err) *err = "parent not found: " + argStr(a, "parent"); return jsonNull(); }
    const Vec3 color = argVec3(a, "color", Vec3{0.8f, 0.8f, 0.85f});
    io::Json spec = jsonObject({{"shape", jsonString(shape)},
                                {"color", jsonArray({jsonNumber(color.x), jsonNumber(color.y), jsonNumber(color.z)})}});
    if (shape == "box") {
        const Vec3 he = argVec3(a, "halfExtents", Vec3{0.5f, 0.5f, 0.5f});
        spec.obj["halfExtents"] = jsonArray({jsonNumber(he.x), jsonNumber(he.y), jsonNumber(he.z)});
    } else if (shape == "quad") {
        spec.obj["halfSize"] = jsonNumber(argNum(a, "halfSize", 1.0));
    } else {
        spec.obj["radius"] = jsonNumber(argNum(a, "radius", 1.0));
    }
    std::string name = argStr(a, "name");
    if (name.empty()) name = shape + std::to_string(parent->childCount() + 1);
    scene::Node& node = parent->createChild(name);
    if (a.contains("translation")) node.translation = argVec3(a, "translation", node.translation);
    io::Json r = payload({{"node", jsonString(name)}});
    if (host.meshResolver && host.meshArena) {
        auto mesh = host.meshResolver(spec);
        if (mesh) {
            scene::MeshEntry& e = node.attach<scene::MeshComponent>().addMesh(mesh.get());
            e.albedo = color;
            host.meshArena->push_back(std::move(mesh));
            r.obj["attached"] = jsonBool(true);
            return r;
        }
    }
    r.obj["attached"] = jsonBool(false);
    r.obj["meshSpec"] = spec;
    r.obj["reason"] = jsonString("no GL mesh resolver in host");
    return r;
}

io::Json tSceneAddLight(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.scene) { if (err) *err = "no scene in host"; return jsonNull(); }
    const std::string type = argStr(a, "type", "point");
    if (type != "directional" && type != "point" && type != "spot") {
        if (err) *err = "unknown light type: " + type + " (directional|point|spot)";
        return jsonNull();
    }
    const Vec3 color = argVec3(a, "color", Vec3{1.f, 0.9f, 0.7f});
    const float intensity = static_cast<float>(argNum(a, "intensity", 20.0));
    const Vec3 dir = argVec3(a, "direction", Vec3{0.f, -1.f, 0.f});
    scene::Node* parent = findNode(*host.scene, argStr(a, "parent", "root"));
    if (!parent) { if (err) *err = "parent not found: " + argStr(a, "parent"); return jsonNull(); }
    std::string name = argStr(a, "name");
    if (name.empty()) name = "light" + std::to_string(parent->childCount() + 1);
    scene::Node& node = parent->createChild(name);
    if (a.contains("translation")) node.translation = argVec3(a, "translation", node.translation);
    scene::LightComponent& lc = node.attach<scene::LightComponent>();
    if (type == "directional")
        lc.light = fuhgl::render::Light::directional(dir, color, intensity);
    else if (type == "spot") {
        const float outer = static_cast<float>(argNum(a, "coneOuterDeg", 45.0)) * fuhgl::math::kPi / 180.f;
        lc.light = fuhgl::render::Light::spot(Vec3{0.f, 0.f, 0.f}, dir, color, intensity, outer * 0.8f, outer);
    }
    else
        lc.light = fuhgl::render::Light::point(Vec3{0.f, 0.f, 0.f}, color, intensity,
                                               static_cast<float>(argNum(a, "range", 30.0)));
    return payload({{"node", jsonString(name)}, {"type", jsonString(type)}});
}

// --- deferred-renderer settings tools ----------------------------------------------------

io::Json tRenderListEffects(const io::Json& /*a*/, ToolHost& host, std::string* /*err*/) {
    std::vector<io::Json> fx;
    for (int i = 0; i < render::kEffectCount; ++i) {
        const render::Effect e = static_cast<render::Effect>(i);
        const bool on = host.effects && host.effects->enabled(e);
        fx.push_back(payload({{"name", jsonString(render::effectName(e))},
                              {"index", jsonNumber(i)},
                              {"enabled", jsonBool(on)},
                              {"supported", jsonBool(render::effectSupported(e, host.glMajor, host.glMinor))}}));
    }
    return payload({{"effects", jsonArray(std::move(fx))}});
}

io::Json tRenderSetEffect(const io::Json& a, ToolHost& host, std::string* err) {
    render::Effect e;
    if (!render::effectFromName(argStr(a, "effect"), e)) {
        if (err) *err = "unknown effect: " + argStr(a, "effect") + " (see render_list_effects)";
        return jsonNull();
    }
    const bool on = argBool(a, "enabled", true);
    if (host.effects) { if (on) host.effects->enable(e); else host.effects->disable(e); }
    return payload({{"effect", jsonString(render::effectName(e))},
                    {"enabled", jsonBool(on)},
                    {"supported", jsonBool(render::effectSupported(e, host.glMajor, host.glMinor))}});
}

io::Json tRenderGetSettings(const io::Json& /*a*/, ToolHost& host, std::string* err) {
    if (!host.effects) { if (err) *err = "no effects target in host"; return jsonNull(); }
    return scene::effectsToJson(*host.effects);
}

io::Json tRenderSetParam(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.effects) { if (err) *err = "no effects target in host"; return jsonNull(); }
    const std::string name = argStr(a, "name");
    if (!a.contains("value")) { if (err) *err = "missing value"; return jsonNull(); }
    if (!scene::setEffectParam(host.effects->params, name, a.at("value"), err)) return jsonNull();
    return payload({{"name", jsonString(name)},
                    {"value", scene::effectParamToJson(host.effects->params, name)}});
}

io::Json tRenderSetParams(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.effects) { if (err) *err = "no effects target in host"; return jsonNull(); }
    const io::Json& p = a.at("params");
    if (p.type != io::Json::Type::Object) { if (err) *err = "params must be an object"; return jsonNull(); }
    int applied = 0;
    for (const auto& kv : p.obj)
        if (scene::setEffectParam(host.effects->params, kv.first, kv.second, err)) ++applied;
    return payload({{"applied", jsonNumber(applied)}, {"settings", scene::effectsToJson(*host.effects)}});
}

// --- G13 water tools ---------------------------------------------------------------------

io::Json waterToJson(const water::WaterSurface& w) {
    std::vector<io::Json> waves;
    for (const water::GerstnerWave& g : w.waves) {
        waves.push_back(payload({{"direction", jsonArray({jsonNumber(g.direction.x), jsonNumber(g.direction.y)})},
                                 {"amplitude", jsonNumber(g.amplitude)},
                                 {"wavelength", jsonNumber(g.wavelength)},
                                 {"speed", jsonNumber(g.speed)},
                                 {"steepness", jsonNumber(g.steepness)}}));
    }
    auto c3 = [](const Vec3& c) { return jsonArray({jsonNumber(c.x), jsonNumber(c.y), jsonNumber(c.z)}); };
    return payload({{"waves", jsonArray(std::move(waves))},
                    {"time", jsonNumber(w.time)},
                    {"deepColor", c3(w.deepColor)}, {"shallowColor", c3(w.shallowColor)},
                    {"foamColor", c3(w.foamColor)},
                    {"fresnelF0", jsonNumber(w.fresnelF0)},
                    {"reflectionStrength", jsonNumber(w.reflectionStrength)},
                    {"distortion", jsonNumber(w.distortion)},
                    {"refractionDistortion", jsonNumber(w.refractionDistortion)},
                    {"foamCrestStart", jsonNumber(w.foamCrestStart)},
                    {"foamCrestSoftness", jsonNumber(w.foamCrestSoftness)},
                    {"shoreFoamWidth", jsonNumber(w.shoreFoamWidth)},
                    {"foamStrength", jsonNumber(w.foamStrength)},
                    {"glintPower", jsonNumber(w.glintPower)},
                    {"detailNormalScale", jsonNumber(w.detailNormalScale)},
                    {"maxWaveHeight", jsonNumber(w.maxWaveHeight())}});
}

io::Json tWaterGetParams(const io::Json& /*a*/, ToolHost& host, std::string* err) {
    if (!host.water) { if (err) *err = "no water surface in host (use water_add_to_scene first)"; return jsonNull(); }
    return waterToJson(*host.water);
}

io::Json tWaterSetParams(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.water) { if (err) *err = "no water surface in host (use water_add_to_scene first)"; return jsonNull(); }
    water::WaterSurface& w = *host.water;
    auto num = [&](const char* k, float& dst) { if (a.contains(k)) dst = static_cast<float>(a.at(k).asNumber(dst)); };
    auto col = [&](const char* k, Vec3& dst) { if (a.contains(k)) dst = argVec3(a, k, dst); };
    col("deepColor", w.deepColor); col("shallowColor", w.shallowColor); col("foamColor", w.foamColor);
    num("fresnelF0", w.fresnelF0); num("reflectionStrength", w.reflectionStrength);
    num("distortion", w.distortion); num("refractionDistortion", w.refractionDistortion);
    num("foamCrestStart", w.foamCrestStart); num("foamCrestSoftness", w.foamCrestSoftness);
    num("shoreFoamWidth", w.shoreFoamWidth); num("foamStrength", w.foamStrength);
    num("glintPower", w.glintPower); num("detailNormalScale", w.detailNormalScale);
    num("time", w.time);
    if (a.contains("waves")) {
        const io::Json& ws = a.at("waves");
        if (ws.isArray()) {
            w.waves.clear();
            for (const io::Json& wj : ws.arr) {
                if (static_cast<int>(w.waves.size()) >= water::WaterSurface::kMaxWaves) break;
                water::GerstnerWave g;
                g.direction = argVec2(wj, "direction", g.direction);
                g.amplitude = static_cast<float>(wj.at("amplitude").asNumber(g.amplitude));
                g.wavelength = static_cast<float>(wj.at("wavelength").asNumber(g.wavelength));
                g.speed = static_cast<float>(wj.at("speed").asNumber(g.speed));
                g.steepness = clampFloat(static_cast<float>(wj.at("steepness").asNumber(g.steepness)), 0.f, 1.f);
                w.waves.push_back(g);
            }
        }
    }
    return waterToJson(w);
}

io::Json tWaterAddToScene(const io::Json& a, ToolHost& host, std::string* err) {
    if (!host.scene) { if (err) *err = "no scene in host"; return jsonNull(); }
    scene::Node* parent = findNode(*host.scene, argStr(a, "parent", "root"));
    if (!parent) { if (err) *err = "parent not found: " + argStr(a, "parent"); return jsonNull(); }
    const int res = clampInt(argInt(a, "resolution", 96), 16, 256);
    const float size = clampFloat(static_cast<float>(argNum(a, "size", 120.0)), 1.f, 4000.f);
    std::string name = argStr(a, "name");
    if (name.empty()) name = "water" + std::to_string(parent->childCount() + 1);
    scene::Node& node = parent->createChild(name);
    if (a.contains("translation")) node.translation = argVec3(a, "translation", node.translation);
    scene::WaterComponent& wc = node.attach<scene::WaterComponent>();
    bool ready = false;
    if (host.waterCreate) ready = host.waterCreate(wc.surface, res, size);
    host.water = &wc.surface; // subsequent water_* param tools target this surface
    if (host.effects) host.effects->enable(render::Effect::Water);
    return payload({{"node", jsonString(name)},
                    {"resolution", jsonNumber(res)},
                    {"size", jsonNumber(size)},
                    {"ready", jsonBool(ready)},
                    {"note", jsonString(ready ? "GL surface created" : "no waterCreate in host — surface not uploaded yet")}});
}

// --- catalog / dispatch --------------------------------------------------------------------

struct ToolDef {
    const char* name;
    const char* description;
    io::Json (*run)(const io::Json&, ToolHost&, std::string*);
    io::Json inputSchema;
};

const std::vector<ToolDef>& catalog() {
    static const std::vector<ToolDef> tools = {
        {"scene_list_nodes", "List the scene-graph node tree (names, TRS, children).",
         tSceneList, schema({})},
        {"scene_add_node", "Add a node (optional parent/translation/rotation[quat xyzw]/scale).",
         tSceneAddNode, schema({{"name", prop("string", "node name (auto when empty)")},
                                {"parent", prop("string", "parent node name (default root)")},
                                {"translation", arrProp("number", "[x,y,z]")},
                                {"rotation", arrProp("number", "quaternion [x,y,z,w]")},
                                {"scale", arrProp("number", "[x,y,z]")}})},
        {"scene_remove_node", "Remove a node (and its subtree) by name.",
         tSceneRemoveNode, schema({{"name", prop("string", "node name")}} , {"name"})},
        {"scene_set_trs", "Set a node's translation/rotation(quat xyzw)/scale (any subset).",
         tSceneSetTrs, schema({{"name", prop("string", "node name")},
                               {"translation", arrProp("number", "[x,y,z]")},
                               {"rotation", arrProp("number", "quaternion [x,y,z,w]")},
                               {"scale", arrProp("number", "[x,y,z]")}}, {"name"})},
        {"scene_add_shape", "Add a primitive shape node (box|quad|sphere) with color; attaches a GL mesh when the host has a resolver.",
         tSceneAddShape, schema({{"shape", prop("string", "box|quad|sphere", {"box", "quad", "sphere"})},
                                 {"name", prop("string", "node name")},
                                 {"parent", prop("string", "parent node")},
                                 {"translation", arrProp("number", "[x,y,z]")},
                                 {"halfExtents", arrProp("number", "box half extents [x,y,z]")},
                                 {"halfSize", prop("number", "quad half size")},
                                 {"radius", prop("number", "sphere radius")},
                                 {"color", arrProp("number", "[r,g,b] 0..1")}}, {"shape"})},
        {"scene_add_light", "Add a light node (directional|point|spot).",
         tSceneAddLight, schema({{"type", prop("string", "directional|point|spot", {"directional", "point", "spot"})},
                                 {"name", prop("string", "node name")},
                                 {"parent", prop("string", "parent node")},
                                 {"translation", arrProp("number", "light position [x,y,z]")},
                                 {"direction", arrProp("number", "direction [x,y,z]")},
                                 {"color", arrProp("number", "[r,g,b]")},
                                 {"intensity", prop("number", "light intensity")},
                                 {"range", prop("number", "point light range")},
                                 {"coneOuterDeg", prop("number", "spot outer cone, degrees")}})},
        {"render_list_effects", "List all deferred-renderer effects (render::Effect) with enabled/supported state.",
         tRenderListEffects, schema({})},
        {"render_set_effect", "Enable/disable one deferred-renderer effect by name (e.g. \"bloom\", \"water\", \"SSAO\").",
         tRenderSetEffect, schema({{"effect", prop("string", "effect name (see render_list_effects)")},
                                   {"enabled", prop("boolean", "on/off, default true")}}, {"effect"})},
        {"render_get_settings", "Get the current renderer settings (effect list + all EffectParams).",
         tRenderGetSettings, schema({})},
        {"render_set_param", "Set one deferred-render parameter by name (e.g. bloomThreshold, exposure, tonemapOperator, volumetricDensity).",
         tRenderSetParam, schema({{"name", prop("string", "parameter name (see render_get_settings params)")},
                                  {"value", prop("number", "number, bool, [x,y,z] array or operator name")}}, {"name", "value"})},
        {"render_set_params", "Set several renderer parameters at once ({params:{name:value,...}}).",
         tRenderSetParams, schema({{"params", prop("object", "name -> value map")}}, {"params"})},
        {"water_get_params", "G13: get all water-surface parameters (Gerstner waves, colors, foam, reflection).",
         tWaterGetParams, schema({})},
        {"water_set_params", "G13: set water-surface parameters; `waves` replaces the Gerstner wave list (max 8).",
         tWaterSetParams, schema({{"waves", arrProp("object", "[{direction:[x,z],amplitude,wavelength,speed,steepness}]")},
                                  {"deepColor", arrProp("number", "[r,g,b]")},
                                  {"shallowColor", arrProp("number", "[r,g,b]")},
                                  {"foamColor", arrProp("number", "[r,g,b]")},
                                  {"fresnelF0", prop("number", "fresnel F0")},
                                  {"reflectionStrength", prop("number", "planar reflection intensity")},
                                  {"distortion", prop("number", "reflection normal distortion")},
                                  {"refractionDistortion", prop("number", "refraction distortion")},
                                  {"foamCrestStart", prop("number", "crest foam start (fraction of max height)")},
                                  {"foamCrestSoftness", prop("number", "crest foam ramp width")},
                                  {"shoreFoamWidth", prop("number", "shore foam width in world units")},
                                  {"foamStrength", prop("number", "overall foam intensity")},
                                  {"glintPower", prop("number", "sun glint exponent")},
                                  {"detailNormalScale", prop("number", "ripple detail normal strength")},
                                  {"time", prop("number", "wave animation time, seconds")}})},
        {"water_add_to_scene", "G13: add a water surface node (WaterComponent) to the scene and make it the active water target; needs a GL waterCreate in the host.",
         tWaterAddToScene, schema({{"name", prop("string", "node name")},
                                   {"parent", prop("string", "parent node")},
                                   {"translation", arrProp("number", "water plane position [x,y,z]")},
                                   {"resolution", prop("integer", "grid resolution (16..256)")},
                                   {"size", prop("number", "world size of the water plane")}})},
        {"procedural_noise", "G24: sample seeded noise (value/perlin/simplex/fbm/ridged/turbulence, 2D/3D) at one point or a list of `points`.",
         tProceduralNoise, schema({{"seed", prop("integer", "noise seed")},
                                   {"kind", prop("string", "value2|value3|perlin2|perlin3|simplex2|simplex3|fbm2|fbm3|ridged2|ridged3|turbulence2|turbulence3",
                                                 {"value2", "value3", "perlin2", "perlin3", "simplex2", "simplex3", "fbm2", "fbm3", "ridged2", "ridged3", "turbulence2", "turbulence3"})},
                                   {"base", prop("string", "base for fractals: value|perlin|simplex", {"value", "perlin", "simplex"})},
                                   {"x", prop("number", "x coordinate")}, {"y", prop("number", "y coordinate")}, {"z", prop("number", "z coordinate")},
                                   {"scale", prop("number", "coordinate scale")},
                                   {"octaves", prop("integer", "fractal octaves")},
                                   {"lacunarity", prop("number", "fractal lacunarity")},
                                   {"gain", prop("number", "fractal gain")},
                                   {"points", arrProp("array", "[[x,y,z],...] sample list")}})},
        {"procedural_marching_cubes", "G25: mesh a scalar field (mandelbulb|sphere|box|blob) via marching cubes; attach:true adds it as a scene node.",
         tProceduralMarchingCubes, schema({{"field", prop("string", "mandelbulb|sphere|box|blob", {"mandelbulb", "sphere", "box", "blob"})},
                                            {"resolution", prop("integer", "grid resolution (8..96)")},
                                            {"size", prop("number", "world size of the sampled box")},
                                            {"isolevel", prop("number", "iso level")},
                                            {"power", prop("number", "mandelbulb power")},
                                            {"attach", prop("boolean", "attach the mesh as a scene node")},
                                            {"name", prop("string", "node name")},
                                            {"parent", prop("string", "parent node")},
                                            {"translation", arrProp("number", "[x,y,z]")}}, {"field"})},
        {"procedural_lsystem", "G26: generate an L-system mesh (tree|fern|bush presets); attach:true adds it as a scene node.",
         tProceduralLsystem, schema({{"preset", prop("string", "tree|fern|bush", {"tree", "fern", "bush"})},
                                     {"variant", prop("integer", "tree variant 0..2")},
                                     {"seedJitter", prop("integer", "preset seed jitter")},
                                     {"iterations", prop("integer", "override rule iterations (1..8)")},
                                     {"step", prop("number", "segment length")},
                                     {"angleDeg", prop("number", "turn angle")},
                                     {"attach", prop("boolean", "attach as scene node")},
                                     {"name", prop("string", "node name")},
                                     {"parent", prop("string", "parent node")},
                                     {"translation", arrProp("number", "[x,y,z]")}}, {"preset"})},
        {"procedural_wfc", "G27: solve a tiled WFC grid (dungeon|city presets); attach:true adds one box node per collapsed cell.",
         tProceduralWfc, schema({{"preset", prop("string", "dungeon|city", {"dungeon", "city"})},
                                 {"width", prop("integer", "grid width")}, {"height", prop("integer", "grid height")},
                                 {"seed", prop("integer", "solver seed")},
                                 {"cellSize", prop("number", "world size of one cell")},
                                 {"attach", prop("boolean", "attach cells as box nodes")},
                                 {"parent", prop("string", "parent node")}}, {"preset"})},
        {"procedural_voronoi", "G28: seeded 2D voronoi cells, extruded to prisms; attach:true adds the mesh as a scene node.",
         tProceduralVoronoi, schema({{"count", prop("integer", "seed point count")},
                                     {"min", arrProp("number", "[x,z] min")}, {"max", arrProp("number", "[x,z] max")},
                                     {"seed", prop("integer", "seed")},
                                     {"y0", prop("number", "prism bottom")}, {"y1", prop("number", "prism top")},
                                     {"gap", prop("number", "crack gap between cells")},
                                     {"heightJitter", prop("number", "per-cell top height jitter 0..1")},
                                     {"attach", prop("boolean", "attach as scene node")},
                                     {"name", prop("string", "node name")}, {"parent", prop("string", "parent node")},
                                     {"translation", arrProp("number", "[x,y,z]")}})},
        {"procedural_sdf", "G29: mesh an SDF primitive (mandelbulb|roundedBox|sphere|box|cylinder) via marching cubes; attach:true adds a node.",
         tProceduralSdf, schema({{"primitive", prop("string", "mandelbulb|roundedBox|sphere|box|cylinder",
                                                    {"mandelbulb", "roundedBox", "sphere", "box", "cylinder"})},
                                 {"resolution", prop("integer", "grid resolution (8..96)")},
                                 {"size", prop("number", "world size")},
                                 {"power", prop("number", "mandelbulb power")},
                                 {"attach", prop("boolean", "attach as scene node")},
                                 {"name", prop("string", "node name")}, {"parent", prop("string", "parent node")},
                                 {"translation", arrProp("number", "[x,y,z]")}}, {"primitive"})},
        {"procedural_creature", "G30: generate a procedural creature (quadruped|reptile preset, SDF skeleton -> marching cubes); attach:true adds a node.",
         tProceduralCreature, schema({{"preset", prop("string", "quadruped|reptile", {"quadruped", "reptile"})},
                                      {"resolution", prop("integer", "grid resolution (16..96)")},
                                      {"detailAmp", prop("number", "geometry noise detail amplitude")},
                                      {"attach", prop("boolean", "attach as scene node")},
                                      {"name", prop("string", "node name")}, {"parent", prop("string", "parent node")},
                                      {"translation", arrProp("number", "[x,y,z]")}}, {"preset"})},
        {"procedural_worldgen", "G31: generate a whole world (landscape|city) from one params struct; attach:true adds terrain + placements as nodes.",
         tProceduralWorldgen, schema({{"mode", prop("string", "landscape|city", {"landscape", "city"})},
                                      {"seed", prop("integer", "world seed")},
                                      {"worldSize", prop("number", "world extent X/Z")},
                                      {"terrainGrid", prop("integer", "heightmap resolution")},
                                      {"terrainHeight", prop("number", "height scale")},
                                      {"treeCount", prop("integer", "scattered trees")},
                                      {"bushCount", prop("integer", "scattered bushes")},
                                      {"rockCount", prop("integer", "scattered rocks")},
                                      {"erosionDroplets", prop("integer", "hydraulic erosion droplets")},
                                      {"thermalPasses", prop("integer", "thermal erosion passes")},
                                      {"riverCount", prop("integer", "rivers")},
                                      {"cityGrid", prop("integer", "city WFC cells per side")},
                                      {"attach", prop("boolean", "attach terrain + placements as nodes")},
                                      {"name", prop("string", "root node name")}, {"parent", prop("string", "parent node")}}, {"mode"})},
    };
    return tools;
}

} // namespace tc

io::Json toolsListJson() {
    std::vector<io::Json> tools;
    for (const tc::ToolDef& t : tc::catalog()) {
        tools.push_back(jsonObject({{"name", jsonString(t.name)},
                                    {"description", jsonString(t.description)},
                                    {"inputSchema", t.inputSchema}}));
    }
    return jsonObject({{"tools", jsonArray(std::move(tools))}});
}

bool toolExists(const std::string& name) {
    for (const tc::ToolDef& t : tc::catalog())
        if (name == t.name) return true;
    return false;
}

io::Json callTool(const std::string& name, const io::Json& arguments, ToolHost& host,
                  std::string* err) {
    for (const tc::ToolDef& t : tc::catalog()) {
        if (name == t.name) {
            io::Json args = arguments;
            if (args.isNull()) args = jsonObject({});
            if (!args.isObject()) {
                if (err) *err = "arguments must be an object";
                return jsonNull();
            }
            return t.run(args, host, err);
        }
    }
    if (err) *err = "unknown tool: " + name;
    return jsonNull();
}

io::Json makeToolResult(const io::Json& payloadJson, bool isError) {
    return jsonObject({{"content", jsonArray({jsonObject({{"type", jsonString("text")},
                                                          {"text", jsonString(io::writeJson(payloadJson, 2))}})})},
                       {"isError", jsonBool(isError)}});
}

} // namespace fuhglgraphics::mcp
