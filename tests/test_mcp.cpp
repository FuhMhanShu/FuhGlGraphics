// Unit tests for the G12 MCP layer (label "unit", no GL, no Qt):
// - mcp/jsonrpc: message build/parse/serialize round-trips, framing, id matching
// - mcp/tool_catalog: tools/list, scene tools, render-setting tools (Effect + EffectParams),
//   G13 water tools, the procedural tools G24-G31 (data + attach path with a GL-free resolver)
// - mcp/mcp_server: initialize / tools/list / tools/call / notification / unknown method
// - io::AssetCache (G11): content-hash put/get round-trip
// - scene::effectsToJson/FromJson/setEffectParam (public JSON mapping used by MCP + config)

#include "test_framework.hpp"
#include "tests.hpp"

#include <fuhglgraphics/io/asset_cache.hpp>
#include <fuhglgraphics/mcp/jsonrpc.hpp>
#include <fuhglgraphics/mcp/mcp_server.hpp>
#include <fuhglgraphics/mcp/tool_catalog.hpp>
#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/serialize.hpp>
#include <fuhglgraphics/water/water_surface.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace fuhglgraphics;
using fuhglgraphics::mcp::jsonArray;
using fuhglgraphics::mcp::jsonBool;
using fuhglgraphics::mcp::jsonNull;
using fuhglgraphics::mcp::jsonNumber;
using fuhglgraphics::mcp::jsonObject;
using fuhglgraphics::mcp::jsonString;

namespace {

io::Json obj(std::initializer_list<std::pair<const std::string, io::Json>> items) {
    return jsonObject(std::map<std::string, io::Json>(items));
}

void testJsonRpc() {
    namespace mcp = fuhglgraphics::mcp;
    // request round-trip
    io::Json req = mcp::makeRequest(jsonNumber(1), "tools/call",
                                    obj({{"name", jsonString("ping")}, {"arguments", jsonObject({})}}));
    const std::string line = mcp::serializeMessage(req);
    CHECK(line.find('\n') == std::string::npos); // one line
    mcp::Message m;
    CHECK(mcp::parseMessage(line, m));
    CHECK(m.kind == mcp::Message::Kind::Request);
    CHECK(mcp::idEquals(m.id, jsonNumber(1)));
    CHECK(m.method == "tools/call");
    CHECK(m.params.at("name").asString() == "ping");
    // notification has no id
    io::Json note = mcp::makeNotification("notifications/initialized", jsonObject({}));
    CHECK(mcp::parseMessage(mcp::serializeMessage(note), m));
    CHECK(m.kind == mcp::Message::Kind::Notification);
    CHECK(m.id.isNull());
    // error response
    io::Json errResp = mcp::makeErrorResponse(jsonString("abc"), mcp::kMethodNotFound, "nope");
    CHECK(mcp::parseMessage(mcp::serializeMessage(errResp), m));
    CHECK(m.kind == mcp::Message::Kind::Response);
    CHECK(m.errorCode == -32601);
    CHECK(m.errorMessage == "nope");
    CHECK(mcp::idEquals(m.id, jsonString("abc")));
    // invalid input
    CHECK(!mcp::parseMessage("{not json", m, nullptr));
    CHECK(!mcp::parseMessage("{\"jsonrpc\":\"1.0\"}", m, nullptr));
    CHECK(!mcp::parseMessage("{\"jsonrpc\":\"2.0\"}", m, nullptr));
    // framing
    std::string buf = "line1\nline2\npartial";
    std::string l;
    CHECK(mcp::extractLine(buf, l) && l == "line1");
    CHECK(mcp::extractLine(buf, l) && l == "line2");
    CHECK(!mcp::extractLine(buf, l));
    CHECK(buf == "partial");
    // id equality
    CHECK(mcp::idEquals(jsonNull(), jsonNull()));
    CHECK(!mcp::idEquals(jsonNumber(1), jsonNumber(2)));
    CHECK(mcp::idEquals(jsonString("x"), jsonString("x")));
}

mcp::ToolHost makeHost(scene::Scene& scene) {
    mcp::ToolHost host;
    host.scene = &scene;
    host.effects = &scene.root().effects;
    // GL-free resolvers: shapes/meshes are not uploaded, but the attach bookkeeping runs
    host.meshResolver = [](const io::Json&) { return std::unique_ptr<fuhgl::render::Mesh>(); };
    host.meshDataResolver = [](const fuhgl::render::MeshData&) {
        return std::make_unique<fuhgl::render::Mesh>(); // ctor is GL-free; upload happens app-side
    };
    return host;
}

void testToolCatalogBasics() {
    io::Json list = mcp::toolsListJson();
    CHECK(list.isObject());
    const io::Json* tools = list.find("tools");
    CHECK(tools && tools->isArray());
    CHECK(tools->arr.size() >= 20);
    bool hasRender = false, hasProc = false, hasWater = false;
    for (const io::Json& t : tools->arr) {
        const std::string n = t.at("name").asString();
        if (n == "render_set_effect") hasRender = true;
        if (n == "procedural_worldgen") hasProc = true;
        if (n == "water_set_params") hasWater = true;
        CHECK(t.contains("description") && t.contains("inputSchema"));
    }
    CHECK(hasRender && hasProc && hasWater);
    CHECK(mcp::toolExists("render_set_param"));
    CHECK(!mcp::toolExists("does_not_exist"));
    scene::Scene scene;
    mcp::ToolHost host = makeHost(scene);
    std::string err;
    CHECK(mcp::callTool("nope", jsonObject({}), host, &err).isNull());
    CHECK(err.find("unknown tool") != std::string::npos);
}

void testSceneTools() {
    scene::Scene scene;
    mcp::ToolHost host = makeHost(scene);
    host.meshArena = nullptr; // set below
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> arena;
    std::vector<std::unique_ptr<fuhgl::render::MeshData>> dataArena;
    host.meshArena = &arena;
    host.meshDataArena = &dataArena;
    std::string err;

    io::Json r = mcp::callTool("scene_add_node", obj({{"name", jsonString("hero")},
                                                       {"translation", jsonArray({jsonNumber(1), jsonNumber(2), jsonNumber(3)})}}),
                               host, &err);
    CHECK(r.isObject() && r.at("node").asString() == "hero");
    scene::Node* hero = nullptr;
    for (const auto& c : scene.root().children()) if (c->name() == "hero") hero = c.get();
    CHECK(hero != nullptr);
    CHECK_NEAR(hero->translation.x, 1.f, 1e-6f);

    r = mcp::callTool("scene_set_trs", obj({{"name", jsonString("hero")},
                                            {"scale", jsonArray({jsonNumber(2), jsonNumber(2), jsonNumber(2)})}}),
                      host, &err);
    CHECK(r.isObject());
    CHECK_NEAR(hero->scale.x, 2.f, 1e-6f);
    CHECK(mcp::callTool("scene_set_trs", obj({{"name", jsonString("ghost")}}), host, &err).isNull());

    // shape without GL resolver -> data-only answer with meshSpec
    host.meshResolver = scene::MeshResolver{};
    r = mcp::callTool("scene_add_shape", obj({{"shape", jsonString("box")}, {"name", jsonString("crate")}}),
                      host, &err);
    CHECK(r.at("attached").asBool() == false);
    CHECK(r.contains("meshSpec"));
    // shape with (GL-free) resolver -> attached node with a MeshEntry
    host.meshResolver = [](const io::Json&) { return std::make_unique<fuhgl::render::Mesh>(); };
    r = mcp::callTool("scene_add_shape", obj({{"shape", jsonString("quad")}, {"name", jsonString("floor")}}),
                      host, &err);
    CHECK(r.at("attached").asBool() == true);
    CHECK(arena.size() == 1);

    // light
    r = mcp::callTool("scene_add_light", obj({{"type", jsonString("directional")},
                                              {"name", jsonString("sun")},
                                              {"direction", jsonArray({jsonNumber(0), jsonNumber(-1), jsonNumber(0)})},
                                              {"intensity", jsonNumber(3.5)}}),
                      host, &err);
    CHECK(r.at("node").asString() == "sun");

    // list + remove
    r = mcp::callTool("scene_list_nodes", jsonObject({}), host, &err);
    CHECK(r.contains("root"));
    r = mcp::callTool("scene_remove_node", obj({{"name", jsonString("crate")}}), host, &err);
    CHECK(r.at("removed").asString() == "crate");
    CHECK(mcp::callTool("scene_remove_node", obj({{"name", jsonString("root")}}), host, &err).isNull());
}

void testRenderTools() {
    scene::Scene scene;
    mcp::ToolHost host = makeHost(scene);
    host.glMajor = 4; host.glMinor = 3;
    std::string err;

    io::Json r = mcp::callTool("render_list_effects", jsonObject({}), host, &err);
    CHECK(r.at("effects").isArray() && r.at("effects").arr.size() == static_cast<std::size_t>(render::kEffectCount));
    // every entry: supported on GL 4.3, including GpuSkinning
    bool gpuSkinOk = false;
    for (const io::Json& e : r.at("effects").arr)
        if (e.at("name").asString() == "GPU skinning") gpuSkinOk = e.at("supported").asBool();
    CHECK(gpuSkinOk);

    r = mcp::callTool("render_set_effect", obj({{"effect", jsonString("bloom")}, {"enabled", jsonBool(true)}}),
                      host, &err);
    CHECK(r.at("enabled").asBool() == true);
    CHECK(scene.root().effects.enabled(render::Effect::Bloom));
    r = mcp::callTool("render_set_effect", obj({{"effect", jsonString("bloom")}, {"enabled", jsonBool(false)}}),
                      host, &err);
    CHECK(!scene.root().effects.enabled(render::Effect::Bloom));
    CHECK(mcp::callTool("render_set_effect", obj({{"effect", jsonString("nope")}}), host, &err).isNull());

    // single param + operator name + vec3
    r = mcp::callTool("render_set_param", obj({{"name", jsonString("bloomThreshold")}, {"value", jsonNumber(2.5)}}),
                      host, &err);
    CHECK(r.at("value").asNumber() == 2.5);
    CHECK_NEAR(scene.root().effects.params.bloomThreshold, 2.5f, 1e-6f);
    r = mcp::callTool("render_set_param", obj({{"name", jsonString("tonemapOperator")}, {"value", jsonString("reinhard")}}),
                      host, &err);
    CHECK(scene.root().effects.params.tonemapOperator == 1);
    r = mcp::callTool("render_set_param", obj({{"name", jsonString("shadowCenter")},
                                               {"value", jsonArray({jsonNumber(1), jsonNumber(2), jsonNumber(3)})}}),
                      host, &err);
    CHECK_NEAR(scene.root().effects.params.shadowCenter.y, 2.f, 1e-6f);
    CHECK(mcp::callTool("render_set_param", obj({{"name", jsonString("bogus")}, {"value", jsonNumber(1)}}),
                        host, &err).isNull());

    // multi-param + get_settings round-trip through the public JSON mapping
    r = mcp::callTool("render_set_params", obj({{"params", obj({{"exposure", jsonNumber(1.25)},
                                                               {"volumetricDensity", jsonNumber(0.05)}})}}),
                      host, &err);
    CHECK(r.at("applied").asInt() == 2);
    io::Json settings = mcp::callTool("render_get_settings", jsonObject({}), host, &err);
    CHECK_NEAR(settings.at("params").at("exposure").asNumber(), 1.25, 1e-6);
    scene::Effects round;
    CHECK(scene::effectsFromJson(settings, round, &err));
    CHECK_NEAR(round.params.exposure, 1.25f, 1e-6f);
    CHECK(round.enabled(render::Effect::Bloom) == false);
    // unknown effect name in a list fails
    io::Json bad = obj({{"list", jsonArray({jsonString("bogus-effect")})}});
    CHECK(!scene::effectsFromJson(bad, round, &err));
    // effectParamNames covers the set fields
    CHECK(scene::effectParamNames().size() >= 40);
}

void testWaterTools() {
    scene::Scene scene;
    mcp::ToolHost host = makeHost(scene);
    std::string err;
    // no water yet
    CHECK(mcp::callTool("water_get_params", jsonObject({}), host, &err).isNull());
    // add to scene without GL create -> ready=false, but surface becomes the active target
    io::Json r = mcp::callTool("water_add_to_scene", obj({{"name", jsonString("lake")}, {"size", jsonNumber(80)}}),
                               host, &err);
    CHECK(r.at("ready").asBool() == false);
    CHECK(host.water != nullptr);
    CHECK(scene.root().effects.enabled(render::Effect::Water));
    // G13 params: replace waves + set foam
    host.water->setDefaultWaves();
    const std::size_t defaultWaves = host.water->waves.size();
    r = mcp::callTool("water_set_params",
                      obj({{"foamStrength", jsonNumber(0.9)},
                           {"waves", jsonArray({obj({{"direction", jsonArray({jsonNumber(1), jsonNumber(0)})},
                                                     {"amplitude", jsonNumber(0.5)},
                                                     {"wavelength", jsonNumber(8)},
                                                     {"speed", jsonNumber(2)},
                                                     {"steepness", jsonNumber(0.5)}})})}}),
                      host, &err);
    CHECK(r.isObject());
    CHECK(host.water->waves.size() == 1);
    CHECK_NEAR(host.water->waves[0].amplitude, 0.5f, 1e-6f);
    CHECK_NEAR(host.water->foamStrength, 0.9f, 1e-6f);
    r = mcp::callTool("water_get_params", jsonObject({}), host, &err);
    CHECK(r.at("waves").arr.size() == 1);
    CHECK(r.at("waves").arr[0].at("wavelength").asNumber() == 8.0);
    (void)defaultWaves;
}

void testProceduralTools() {
    scene::Scene scene;
    mcp::ToolHost host = makeHost(scene);
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> arena;
    std::vector<std::unique_ptr<fuhgl::render::MeshData>> dataArena;
    host.meshArena = &arena;
    host.meshDataArena = &dataArena;
    std::string err;

    // G24 noise: deterministic per seed, fractals in range
    io::Json a = obj({{"seed", jsonNumber(42)}, {"kind", jsonString("simplex3")},
                      {"x", jsonNumber(1.5)}, {"y", jsonNumber(2.5)}, {"z", jsonNumber(3.5)}});
    io::Json r1 = mcp::callTool("procedural_noise", a, host, &err);
    io::Json r2 = mcp::callTool("procedural_noise", a, host, &err);
    CHECK(r1.at("values").arr.size() == 1);
    CHECK(r1.at("values").arr[0].asNumber() == r2.at("values").arr[0].asNumber());
    io::Json f = mcp::callTool("procedural_noise", obj({{"seed", jsonNumber(7)}, {"kind", jsonString("fbm3")},
                                                        {"x", jsonNumber(0.3)}, {"y", jsonNumber(0.1)}, {"z", jsonNumber(0.9)},
                                                        {"octaves", jsonNumber(4)}}), host, &err);
    const double fv = f.at("values").arr[0].asNumber();
    CHECK(fv >= -1.0001 && fv <= 1.0001);
    // multi-point sampling
    r1 = mcp::callTool("procedural_noise", obj({{"seed", jsonNumber(1)}, {"kind", jsonString("perlin2")},
                                                {"points", jsonArray({jsonArray({jsonNumber(0), jsonNumber(0)}),
                                                                      jsonArray({jsonNumber(1), jsonNumber(1)})})}}), host, &err);
    CHECK(r1.at("values").arr.size() == 2);
    CHECK(mcp::callTool("procedural_noise", obj({{"kind", jsonString("bogus")}}), host, &err).isNull());

    // G25 marching cubes on a sphere field, data-only
    r1 = mcp::callTool("procedural_marching_cubes", obj({{"field", jsonString("sphere")},
                                                          {"resolution", jsonNumber(24)}, {"size", jsonNumber(4)}}), host, &err);
    CHECK(r1.at("vertices").asNumber() > 0);
    CHECK(r1.at("triangles").asNumber() > 0);
    CHECK(!r1.contains("attached")); // attach not requested
    // attach path (GL-free resolver still records the node + meshData)
    const std::size_t nodesBefore = scene.root().childCount();
    r1 = mcp::callTool("procedural_marching_cubes", obj({{"field", jsonString("sphere")},
                                                          {"resolution", jsonNumber(16)}, {"size", jsonNumber(4)},
                                                          {"attach", jsonBool(true)}, {"name", jsonString("ball")}}), host, &err);
    CHECK(r1.at("attached").asBool() == true);
    CHECK(scene.root().childCount() == nodesBefore + 1);
    CHECK(arena.size() == 1 && dataArena.size() == 1);

    // G26 L-system
    r1 = mcp::callTool("procedural_lsystem", obj({{"preset", jsonString("bush")}, {"iterations", jsonNumber(3)}}), host, &err);
    CHECK(r1.at("vertices").asNumber() > 0);
    CHECK(r1.at("stringLength").asNumber() > 0);

    // G27 WFC dungeon + city roadsConnected flag
    r1 = mcp::callTool("procedural_wfc", obj({{"preset", jsonString("dungeon")}, {"width", jsonNumber(6)},
                                              {"height", jsonNumber(6)}, {"seed", jsonNumber(3)}}), host, &err);
    CHECK(r1.at("cells").arr.size() > 0);
    r1 = mcp::callTool("procedural_wfc", obj({{"preset", jsonString("city")}, {"width", jsonNumber(4)},
                                              {"height", jsonNumber(4)}, {"seed", jsonNumber(5)}}), host, &err);
    CHECK(r1.contains("roadsConnected"));

    // G28 voronoi
    r1 = mcp::callTool("procedural_voronoi", obj({{"count", jsonNumber(20)}, {"y1", jsonNumber(1.5)}}), host, &err);
    CHECK(r1.at("cellCount").asInt() == 20);
    CHECK(r1.at("vertices").asNumber() > 0);

    // G29 SDF
    r1 = mcp::callTool("procedural_sdf", obj({{"primitive", jsonString("sphere")}, {"resolution", jsonNumber(24)}}), host, &err);
    CHECK(r1.at("vertices").asNumber() > 0);
    r1 = mcp::callTool("procedural_sdf", obj({{"primitive", jsonString("mandelbulb")}, {"resolution", jsonNumber(24)},
                                              {"power", jsonNumber(8)}}), host, &err);
    CHECK(r1.at("vertices").asNumber() > 0);

    // G30 creature (small resolution to keep the test fast)
    r1 = mcp::callTool("procedural_creature", obj({{"preset", jsonString("quadruped")}, {"resolution", jsonNumber(24)}}), host, &err);
    CHECK(r1.at("vertices").asNumber() > 0);

    // G31 worldgen (small, no erosion to keep it fast)
    r1 = mcp::callTool("procedural_worldgen", obj({{"mode", jsonString("landscape")}, {"seed", jsonNumber(9)},
                                                    {"worldSize", jsonNumber(60)}, {"terrainGrid", jsonNumber(33)},
                                                    {"treeCount", jsonNumber(5)}, {"bushCount", jsonNumber(4)},
                                                    {"rockCount", jsonNumber(2)}}), host, &err);
    CHECK(r1.at("gridW").asInt() == 33);
    CHECK(r1.at("trees").asNumber() > 0);
    CHECK(r1.at("attachedNodes").asInt() == 0); // attach not requested
    const std::size_t nodesBefore2 = scene.root().childCount();
    r1 = mcp::callTool("procedural_worldgen", obj({{"mode", jsonString("landscape")}, {"seed", jsonNumber(9)},
                                                    {"worldSize", jsonNumber(60)}, {"terrainGrid", jsonNumber(33)},
                                                    {"treeCount", jsonNumber(3)}, {"bushCount", jsonNumber(0)},
                                                    {"rockCount", jsonNumber(0)}, {"attach", jsonBool(true)}}), host, &err);
    CHECK(r1.at("attachedNodes").asNumber() >= 4); // terrain + trees
    CHECK(scene.root().childCount() == nodesBefore2 + 1); // one world root node
}

void testMcpServer() {
    scene::Scene scene;
    mcp::ToolHost host = makeHost(scene);
    // initialize
    io::Json initReq = mcp::makeRequest(jsonNumber(1), "initialize", jsonObject({}));
    std::string resp = mcp::handleLine(mcp::serializeMessage(initReq), host);
    mcp::Message m;
    CHECK(mcp::parseMessage(resp, m));
    CHECK(m.kind == mcp::Message::Kind::Response);
    CHECK(m.result.at("protocolVersion").asString() == mcp::kProtocolVersion);
    CHECK(m.result.at("serverInfo").at("name").asString() == "fuhglgraphics");
    // notification -> no response
    CHECK(mcp::handleLine(mcp::serializeMessage(
                              mcp::makeNotification("notifications/initialized", jsonObject({}))), host).empty());
    // ping
    resp = mcp::handleLine(mcp::serializeMessage(mcp::makeRequest(jsonNumber(2), "ping", jsonObject({}))), host);
    CHECK(mcp::parseMessage(resp, m) && m.result.isObject());
    // tools/list over the wire
    resp = mcp::handleLine(mcp::serializeMessage(mcp::makeRequest(jsonNumber(3), "tools/list", jsonObject({}))), host);
    CHECK(mcp::parseMessage(resp, m) && m.result.at("tools").isArray());
    // tools/call render_set_effect over the wire
    io::Json callReq = mcp::makeRequest(jsonNumber(4), "tools/call",
                                        obj({{"name", jsonString("render_set_effect")},
                                             {"arguments", obj({{"effect", jsonString("SSAO")}, {"enabled", jsonBool(true)}})}}));
    resp = mcp::handleLine(mcp::serializeMessage(callReq), host);
    CHECK(mcp::parseMessage(resp, m));
    CHECK(m.result.at("content").arr[0].at("text").asString().find("SSAO") != std::string::npos);
    CHECK(m.result.at("isError").asBool() == false);
    CHECK(scene.root().effects.enabled(render::Effect::Ssao));
    // tools/call with a bad tool -> isError result
    callReq = mcp::makeRequest(jsonNumber(5), "tools/call", obj({{"name", jsonString("bogus")}}));
    resp = mcp::handleLine(mcp::serializeMessage(callReq), host);
    CHECK(mcp::parseMessage(resp, m) && m.result.at("isError").asBool() == true);
    // unknown method -> -32601
    resp = mcp::handleLine(mcp::serializeMessage(mcp::makeRequest(jsonNumber(6), "bogus/method", jsonObject({}))), host);
    CHECK(mcp::parseMessage(resp, m) && m.errorCode == mcp::kMethodNotFound);
    // malformed line -> -32700
    resp = mcp::handleLine("{oops", host);
    CHECK(mcp::parseMessage(resp, m) && m.errorCode == mcp::kParseError);
    // runServer end-to-end over stringstreams
    std::istringstream in;
    std::ostringstream out;
    in.str(mcp::serializeMessage(mcp::makeRequest(jsonNumber(10), "initialize", jsonObject({}))) + "\n" +
           mcp::serializeMessage(mcp::makeNotification("notifications/initialized", jsonObject({}))) + "\n" +
           mcp::serializeMessage(mcp::makeRequest(jsonNumber(11), "tools/call",
                                                  obj({{"name", jsonString("procedural_noise")},
                                                       {"arguments", obj({{"seed", jsonNumber(3)}, {"kind", jsonString("value2")},
                                                                           {"x", jsonNumber(0.5)}, {"y", jsonNumber(0.5)}})}}))) + "\n");
    CHECK(mcp::runServer(host, in, out));
    std::istringstream outIn(out.str());
    std::string l1, l2, extra;
    CHECK(std::getline(outIn, l1) && std::getline(outIn, l2));
    CHECK(!std::getline(outIn, extra)); // exactly two responses (the notification got none)
    CHECK(mcp::parseMessage(l1, m) && mcp::idEquals(m.id, jsonNumber(10)));
    CHECK(mcp::parseMessage(l2, m) && mcp::idEquals(m.id, jsonNumber(11)) && m.result.at("content").isArray());
}

void testAssetCache() {
    const std::string dir = "/tmp/fuhglgraphics_test_asset_cache";
    std::filesystem::remove_all(dir);
    io::AssetCache cache;
    std::string err;
    CHECK(cache.open(dir, &err));
    CHECK(cache.isOpen());
    // write a source file
    const std::string src = dir + "_src.bin";
    {
        std::ofstream f(src, std::ios::binary);
        const char data[] = "hello asset cache";
        f.write(data, sizeof(data));
    }
    std::string cached;
    CHECK(cache.put(src, cached, &err));
    CHECK(!cached.empty());
    const std::string hash = cache.hashOfFile(src, &err);
    CHECK(hash.size() == 16);
    // identical content -> same hash -> same cache path, no duplicate
    const std::string src2 = dir + "_src2.bin";
    {
        std::ofstream f(src2, std::ios::binary);
        const char data[] = "hello asset cache";
        f.write(data, sizeof(data));
    }
    std::string cached2;
    CHECK(cache.put(src2, cached2, &err));
    CHECK(cached2 == cached);
    CHECK(cache.hashOfFile(src2, &err) == hash);
    // get
    std::string got;
    CHECK(cache.get(hash, "bin", got, &err));
    CHECK(got == cached);
    CHECK(!cache.get("0000000000000000", "bin", got, &err));
    // hashBytes known vector: FNV-1a64("") = cbf29ce484222325
    CHECK(io::AssetCache::hashBytes(nullptr, 0) == "cbf29ce484222325");
    std::filesystem::remove_all(dir);
}

} // namespace

void runMcpTests() {
    testJsonRpc();
    testToolCatalogBasics();
    testSceneTools();
    testRenderTools();
    testWaterTools();
    testProceduralTools();
    testMcpServer();
    testAssetCache();
}
