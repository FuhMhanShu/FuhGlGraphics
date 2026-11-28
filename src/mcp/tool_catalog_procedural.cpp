#include "tool_catalog_internal.hpp"

#include "fuhglgraphics/procedural/creature.hpp"
#include "fuhglgraphics/procedural/lsystem.hpp"
#include "fuhglgraphics/procedural/marching_cubes.hpp"
#include "fuhglgraphics/procedural/noise.hpp"
#include "fuhglgraphics/procedural/sdf.hpp"
#include "fuhglgraphics/procedural/voronoi.hpp"
#include "fuhglgraphics/procedural/wfc.hpp"
#include "fuhglgraphics/procedural/worldgen.hpp"

#include <functional>

namespace fuhglgraphics::mcp {
namespace tc {

/// @brief Terrain grid MeshData from a GeneratedWorld heightmap (world-centered, y up,
/// per-vertex color from the RGBA splatmap when present).
MeshData terrainMeshData(const procedural::GeneratedWorld& w) {
    MeshData md;
    const int gw = w.gridW, gh = w.gridH;
    if (gw < 2 || gh < 2 || static_cast<int>(w.heightmap.size()) != gw * gh) return md;
    md.vertices.reserve(static_cast<std::size_t>(gw) * gh * MeshData::kFloatsPerVertex);
    for (int z = 0; z < gh; ++z) {
        for (int x = 0; x < gw; ++x) {
            const float wx = -w.sizeX * 0.5f + w.sizeX * x / (gw - 1);
            const float wz = -w.sizeZ * 0.5f + w.sizeZ * z / (gh - 1);
            const float h = w.heightmap[static_cast<std::size_t>(x) + gw * z] * w.heightScale;
            Vec3 c{0.45f, 0.5f, 0.35f};
            if (!w.splatmap.empty()) {
                const unsigned char* s = &w.splatmap[(static_cast<std::size_t>(x) + gw * z) * 4];
                c = {s[0] / 255.f * 0.3f + 0.25f, s[1] / 255.f * 0.3f + 0.3f, s[2] / 255.f * 0.25f + 0.3f};
            }
            const float v[11] = {wx, h, wz, 0.f, 1.f, 0.f, c.x, c.y, c.z,
                                 x / static_cast<float>(gw - 1), z / static_cast<float>(gh - 1)};
            for (float f : v) md.vertices.push_back(f);
        }
    }
    for (int z = 0; z < gh - 1; ++z) {
        for (int x = 0; x < gw - 1; ++x) {
            const unsigned int a = static_cast<unsigned int>(x + gw * z);
            const unsigned int b = a + 1, c = a + gw, d = c + 1;
            const unsigned int t[6] = {a, c, b, b, c, d};
            md.indices.insert(md.indices.end(), std::begin(t), std::end(t));
        }
    }
    return md;
}

io::Json tProceduralNoise(const io::Json& a, ToolHost& /*host*/, std::string* err) {
    const unsigned seed = static_cast<unsigned>(argInt(a, "seed", 0));
    const std::string kind = argStr(a, "kind", "simplex3");
    const float scale = clampFloat(static_cast<float>(argNum(a, "scale", 1.0)), 1e-4f, 1e4f);
    const int octaves = clampInt(argInt(a, "octaves", 4), 1, 8);
    const float lacunarity = clampFloat(static_cast<float>(argNum(a, "lacunarity", 2.0)), 0.5f, 8.f);
    const float gain = clampFloat(static_cast<float>(argNum(a, "gain", 0.5)), 0.f, 1.f);
    const procedural::Noise noise(seed);
    const procedural::Noise::Base base = [&] {
        const std::string b = argStr(a, "base", "perlin");
        if (b == "value") return procedural::Noise::Base::Value;
        if (b == "simplex") return procedural::Noise::Base::Simplex;
        return procedural::Noise::Base::Perlin;
    }();
    std::vector<io::Json> values;
    auto evalOne = [&](double x, double y, double z) -> bool {
        const float fx = static_cast<float>(x) * scale, fy = static_cast<float>(y) * scale,
                    fz = static_cast<float>(z) * scale;
        float v = 0.f;
        if (kind == "value2") v = noise.value2(fx, fy);
        else if (kind == "value3") v = noise.value3(fx, fy, fz);
        else if (kind == "perlin2") v = noise.perlin2(fx, fy);
        else if (kind == "perlin3") v = noise.perlin3(fx, fy, fz);
        else if (kind == "simplex2") v = noise.simplex2(fx, fy);
        else if (kind == "simplex3") v = noise.simplex3(fx, fy, fz);
        else if (kind == "fbm2") v = noise.fbm2(fx, fy, octaves, base, lacunarity, gain);
        else if (kind == "fbm3") v = noise.fbm3(fx, fy, fz, octaves, base, lacunarity, gain);
        else if (kind == "ridged2") v = noise.ridged2(fx, fy, octaves, base, lacunarity, gain);
        else if (kind == "ridged3") v = noise.ridged3(fx, fy, fz, octaves, base, lacunarity, gain);
        else if (kind == "turbulence2") v = noise.turbulence2(fx, fy, octaves, base, lacunarity, gain);
        else if (kind == "turbulence3") v = noise.turbulence3(fx, fy, fz, octaves, base, lacunarity, gain);
        else { if (err) *err = "unknown noise kind: " + kind; return false; }
        values.push_back(jsonNumber(v));
        return true;
    };
    const io::Json& points = a.at("points");
    if (points.isArray() && !points.arr.empty()) {
        for (const io::Json& p : points.arr) {
            if (p.isArray() && p.arr.size() >= 2) {
                if (!evalOne(p.arr[0].asNumber(), p.arr[1].asNumber(),
                             p.arr.size() >= 3 ? p.arr[2].asNumber() : 0.0))
                    return jsonNull();
            }
        }
    } else {
        if (!evalOne(argNum(a, "x", 0.0), argNum(a, "y", 0.0), argNum(a, "z", 0.0))) return jsonNull();
    }
    return payload({{"kind", jsonString(kind)}, {"seed", jsonNumber(seed)},
                    {"values", jsonArray(std::move(values))}});
}

io::Json tProceduralMarchingCubes(const io::Json& a, ToolHost& host, std::string* err) {
    const std::string field = argStr(a, "field", "mandelbulb");
    const int res = clampInt(argInt(a, "resolution", 32), 8, 96);
    const float size = clampFloat(static_cast<float>(argNum(a, "size", 4.0)), 0.1f, 100.f);
    const float isolevel = static_cast<float>(argNum(a, "isolevel", 0.0));
    const float power = clampFloat(static_cast<float>(argNum(a, "power", 8.0)), 2.f, 16.f);
    const float half = size * 0.5f;
    const Vec3 mn{-half, -half, -half}, mx{half, half, half};
    std::function<float(const Vec3&)> fn;
    if (field == "mandelbulb") {
        fn = [power](const Vec3& p) { return procedural::sdf::mandelbulb(p, power); };
    } else if (field == "sphere") {
        fn = [half](const Vec3& p) { return procedural::sdf::sphere(p, half * 0.6f); };
    } else if (field == "box") {
        fn = [half](const Vec3& p) { return procedural::sdf::box(p, Vec3{half * 0.6f, half * 0.6f, half * 0.6f}); };
    } else if (field == "blob") {
        const procedural::Noise n(7u);
        fn = [&n, half](const Vec3& p) { return procedural::sdf::sphere(p, half * 0.45f) + 0.12f * n.simplex3(p.x, p.y, p.z); };
    } else { if (err) *err = "unknown field: " + field + " (mandelbulb|sphere|box|blob)"; return jsonNull(); }
    procedural::VolumeGrid grid = procedural::sampleField(fn, mn, mx, res, res, res);
    MeshData md = procedural::marchingCubes(grid, isolevel, Vec3{0.8f, 0.75f, 0.95f});
    if (md.vertexCount() == 0) { if (err) *err = "empty mesh (isolevel out of range?)"; return jsonNull(); }
    io::Json r = meshResult(host, a, "mc_" + field, md);
    r.obj["field"] = jsonString(field);
    return r;
}

io::Json tProceduralLsystem(const io::Json& a, ToolHost& host, std::string* err) {
    const std::string preset = argStr(a, "preset", "tree");
    procedural::LSystem ls;
    if (preset == "tree") ls = procedural::LSystem::makeTree(argInt(a, "variant", 0), static_cast<unsigned>(argInt(a, "seedJitter", 0)));
    else if (preset == "fern") ls = procedural::LSystem::makeFern();
    else if (preset == "bush") ls = procedural::LSystem::makeBush();
    else { if (err) *err = "unknown preset: " + preset + " (tree|fern|bush)"; return jsonNull(); }
    const int iters = argInt(a, "iterations", -1);
    if (iters >= 0) ls.iterations = clampInt(iters, 1, 8);
    if (a.contains("step")) ls.step = clampFloat(static_cast<float>(a.at("step").asNumber(ls.step)), 0.01f, 100.f);
    if (a.contains("angleDeg")) ls.angleDeg = clampFloat(static_cast<float>(a.at("angleDeg").asNumber(ls.angleDeg)), 1.f, 179.f);
    MeshData md = ls.generate();
    if (md.vertexCount() == 0) { if (err) *err = "empty L-system mesh"; return jsonNull(); }
    io::Json r = meshResult(host, a, "lsys_" + preset, md);
    r.obj["stringLength"] = jsonNumber(static_cast<double>(ls.derive().size()));
    return r;
}

io::Json tProceduralWfc(const io::Json& a, ToolHost& host, std::string* err) {
    const std::string preset = argStr(a, "preset", "dungeon");
    if (preset != "dungeon" && preset != "city") {
        if (err) *err = "unknown preset: " + preset + " (dungeon|city)";
        return jsonNull();
    }
    const int w = clampInt(argInt(a, "width", 8), 2, 32);
    const int h = clampInt(argInt(a, "height", 8), 2, 32);
    const unsigned seed = static_cast<unsigned>(argInt(a, "seed", 1));
    const float cellSize = clampFloat(static_cast<float>(argNum(a, "cellSize", 4.0)), 0.5f, 100.f);
    std::vector<procedural::WfcTile> tiles =
        preset == "city" ? procedural::makeCityTiles() : procedural::Wfc::makeDungeonTiles();
    procedural::Wfc wfc(w, h, preset == "city" ? "s" : "w");
    for (const procedural::WfcTile& t : tiles) wfc.addTile(t);
    const std::vector<procedural::WfcCell> cells = wfc.solve(seed);
    std::vector<io::Json> cellJson;
    int attached = 0;
    scene::Node* parent = host.scene ? findNode(*host.scene, argStr(a, "parent", "root")) : nullptr;
    const bool doAttach = argBool(a, "attach", false) && parent && host.meshDataResolver && host.meshArena;
    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            const procedural::WfcCell& c = cells[static_cast<std::size_t>(x) + w * z];
            if (c.tile < 0) continue;
            const std::string tname = tiles[static_cast<std::size_t>(c.tile)].name;
            cellJson.push_back(payload({{"x", jsonNumber(x)}, {"z", jsonNumber(z)},
                                        {"tile", jsonString(tname)}, {"rot", jsonNumber(c.rot)}}));
            if (doAttach) {
                const bool wall = tname.find("wall") != std::string::npos;
                const bool road = tname.find("street") != std::string::npos || tname == "cross" ||
                                  tname == "tE" || tname == "endE";
                const float height = wall ? 3.f : (road ? 0.15f : 4.f);
                MeshData box = boxMeshData(Vec3{cellSize * 0.5f, height * 0.5f, cellSize * 0.5f},
                                           wall ? Vec3{0.55f, 0.55f, 0.6f} : Vec3{0.7f, 0.65f, 0.55f});
                const Vec3 pos{(x + 0.5f) * cellSize - w * cellSize * 0.5f, height * 0.5f,
                               (z + 0.5f) * cellSize - h * cellSize * 0.5f};
                if (attachMeshData(host, "wfc_" + std::to_string(x) + "_" + std::to_string(z), box, parent, pos))
                    ++attached;
            }
        }
    }
    io::Json r = payload({{"preset", jsonString(preset)}, {"width", jsonNumber(w)}, {"height", jsonNumber(h)},
                          {"seed", jsonNumber(seed)}, {"cells", jsonArray(std::move(cellJson))},
                          {"attachedNodes", jsonNumber(attached)}});
    if (preset == "city")
        r.obj["roadsConnected"] = jsonBool(procedural::wfcRoadsConnected(cells, tiles, w, h));
    return r;
}

io::Json tProceduralVoronoi(const io::Json& a, ToolHost& host, std::string* err) {
    const int n = clampInt(argInt(a, "count", 24), 4, 400);
    const Vec2 mn = argVec2(a, "min", Vec2{-10.f, -10.f});
    const Vec2 mx = argVec2(a, "max", Vec2{10.f, 10.f});
    const unsigned seed = static_cast<unsigned>(argInt(a, "seed", 1));
    const float y0 = static_cast<float>(argNum(a, "y0", 0.0));
    const float y1 = static_cast<float>(argNum(a, "y1", 1.0));
    const float gap = clampFloat(static_cast<float>(argNum(a, "gap", 0.02)), 0.f, 0.4f);
    const float hj = clampFloat(static_cast<float>(argNum(a, "heightJitter", 0.0)), 0.f, 1.f);
    procedural::Voronoi2D v = procedural::voronoi2d(n, mn, mx, seed);
    if (v.points.empty()) { if (err) *err = "voronoi failed"; return jsonNull(); }
    MeshData md = procedural::extrudeCells(v, y0, y1, gap, hj, seed, Vec3{0.75f, 0.78f, 0.85f}, 0.15f);
    io::Json r = meshResult(host, a, "voronoi", md);
    r.obj["cellCount"] = jsonNumber(static_cast<double>(v.points.size()));
    return r;
}

io::Json tProceduralSdf(const io::Json& a, ToolHost& host, std::string* err) {
    const std::string prim = argStr(a, "primitive", "mandelbulb");
    const int res = clampInt(argInt(a, "resolution", 48), 8, 96);
    const float size = clampFloat(static_cast<float>(argNum(a, "size", 2.5)), 0.1f, 50.f);
    const float power = clampFloat(static_cast<float>(argNum(a, "power", 8.0)), 2.f, 16.f);
    const float half = size * 0.5f;
    const Vec3 mn{-half, -half, -half}, mx{half, half, half};
    std::function<float(const Vec3&)> fn;
    if (prim == "mandelbulb") fn = [power](const Vec3& p) { return procedural::sdf::mandelbulb(p, power); };
    else if (prim == "roundedBox") fn = [half](const Vec3& p) { return procedural::sdf::roundedBox(p, Vec3{half * 0.6f, half * 0.6f, half * 0.6f}, 0.2f); };
    else if (prim == "sphere") fn = [half](const Vec3& p) { return procedural::sdf::sphere(p, half * 0.7f); };
    else if (prim == "box") fn = [half](const Vec3& p) { return procedural::sdf::box(p, Vec3{half * 0.7f, half * 0.7f, half * 0.7f}); };
    else if (prim == "cylinder") fn = [half](const Vec3& p) { return procedural::sdf::cylinder(p - Vec3{0.f, half * 0.5f, 0.f}, half * 0.5f, half); };
    else { if (err) *err = "unknown primitive: " + prim; return jsonNull(); }
    MeshData md = procedural::meshFromSdf(fn, mn, mx, res, Vec3{0.85f, 0.7f, 0.95f});
    if (md.vertexCount() == 0) { if (err) *err = "empty SDF mesh"; return jsonNull(); }
    io::Json r = meshResult(host, a, "sdf_" + prim, md);
    r.obj["primitive"] = jsonString(prim);
    return r;
}

io::Json tProceduralCreature(const io::Json& a, ToolHost& host, std::string* err) {
    const std::string preset = argStr(a, "preset", "quadruped");
    if (preset != "quadruped" && preset != "reptile") {
        if (err) *err = "unknown preset: " + preset + " (quadruped|reptile)";
        return jsonNull();
    }
    const int res = clampInt(argInt(a, "resolution", 48), 16, 96);
    procedural::Skeleton skel = preset == "reptile" ? procedural::makeReptile() : procedural::makeQuadruped();
    skel.finalize();
    procedural::CreatureParams pr;
    if (a.contains("detailAmp"))
        pr.detailAmp = clampFloat(static_cast<float>(a.at("detailAmp").asNumber(pr.detailAmp)), 0.f, 0.2f);
    procedural::CreatureMesh cm = procedural::meshCreature(skel, pr, res, Vec3{0.75f, 0.6f, 0.45f});
    if (cm.mesh.vertexCount() == 0) { if (err) *err = "empty creature mesh"; return jsonNull(); }
    io::Json r = meshResult(host, a, "creature_" + preset, cm.mesh);
    r.obj["preset"] = jsonString(preset);
    return r;
}

io::Json tProceduralWorldgen(const io::Json& a, ToolHost& host, std::string* err) {
    const std::string mode = argStr(a, "mode", "landscape");
    if (mode != "landscape" && mode != "city") {
        if (err) *err = "unknown mode: " + mode + " (landscape|city)";
        return jsonNull();
    }
    procedural::WorldGenParams p;
    p.mode = mode == "city" ? procedural::WorldGenParams::Mode::City : procedural::WorldGenParams::Mode::Landscape;
    p.seed = static_cast<unsigned>(argInt(a, "seed", 1));
    p.worldSize = clampFloat(static_cast<float>(argNum(a, "worldSize", 200.0)), 10.f, 4000.f);
    p.terrainGrid = clampInt(argInt(a, "terrainGrid", 65), 17, 129);
    p.terrainHeight = clampFloat(static_cast<float>(argNum(a, "terrainHeight", 18.0)), 0.5f, 200.f);
    p.treeCount = clampInt(argInt(a, "treeCount", 40), 0, 400);
    p.bushCount = clampInt(argInt(a, "bushCount", 60), 0, 400);
    p.rockCount = clampInt(argInt(a, "rockCount", 20), 0, 200);
    p.erosionDroplets = clampInt(argInt(a, "erosionDroplets", 0), 0, 2000);
    p.thermalPasses = clampInt(argInt(a, "thermalPasses", 0), 0, 16);
    p.cityGrid = clampInt(argInt(a, "cityGrid", 8), 3, 16);
    p.riverCount = clampInt(argInt(a, "riverCount", 0), 0, 20);
    p.settlementCount = clampInt(argInt(a, "settlementCount", 3), 0, 12);
    p.housesPerSettlement = clampInt(argInt(a, "housesPerSettlement", 7), 0, 24);
    p.clutterCount = clampInt(argInt(a, "clutterCount", 1200), 0, 4000); // G39a causal clutter
    procedural::GeneratedWorld w = procedural::generateWorld(p);
    int attached = 0;
    scene::Node* parent = host.scene ? findNode(*host.scene, argStr(a, "parent", "root")) : nullptr;
    const bool doAttach = argBool(a, "attach", false) && parent && host.meshDataResolver && host.meshArena;
    if (doAttach) {
        const std::string base = argStr(a, "name", "world");
        scene::Node* wnode = &parent->createChild(base);
        if (w.mode == procedural::WorldGenParams::Mode::Landscape && w.gridW >= 2) {
            if (attachMeshData(host, base + "_terrain", terrainMeshData(w), wnode, Vec3{0.f, 0.f, 0.f}))
                ++attached;
        }
        auto placeAll = [&](const std::vector<procedural::PlacedMesh>& v, const char* prefix) {
            int cap = 256; // bound the node count per tool call
            for (const procedural::PlacedMesh& pm : v) {
                if (cap-- <= 0) break;
                scene::Node* n = attachMeshData(host, std::string(prefix) + "_" + std::to_string(attached),
                                                pm.mesh, wnode, pm.position);
                if (n) {
                    n->rotation = fuhgl::math::fromEuler(pm.rotY, 0.f, 0.f);
                    n->scale = Vec3{pm.scale, pm.scale, pm.scale};
                    ++attached;
                }
            }
        };
        placeAll(w.trees, "tree");
        placeAll(w.bushes, "bush");
        placeAll(w.rocks, "rock");
        placeAll(w.buildings, "house");
        if (w.mode == procedural::WorldGenParams::Mode::City) {
            if (w.cityGround.vertexCount() && attachMeshData(host, base + "_ground", w.cityGround, wnode, Vec3{0.f, 0.f, 0.f})) ++attached;
            if (w.roads.vertexCount() && attachMeshData(host, base + "_roads", w.roads, wnode, Vec3{0.f, 0.02f, 0.f})) ++attached;
            if (w.pavements.vertexCount() && attachMeshData(host, base + "_pavements", w.pavements, wnode, Vec3{0.f, 0.01f, 0.f})) ++attached;
        } else {
            // G38 landscape: organic road ribbons, bridges, props (hamlet houses are in buildings)
            if (w.roads.vertexCount() && attachMeshData(host, base + "_roads", w.roads, wnode, Vec3{0.f, 0.02f, 0.f})) ++attached;
            if (w.bridges.vertexCount() && attachMeshData(host, base + "_bridges", w.bridges, wnode, Vec3{0.f, 0.f, 0.f}, 10)) ++attached; // matId 10 = bridge waterline in the lighting pass
            placeAll(w.roadProps, "roadprop");
            // G39a landscape: causal clutter baked into one static mesh (vertex colors per kind)
            if (!w.clutter.empty()) {
                fuhgl::render::MeshData baked;
                for (const auto& c : w.clutter) {
                    const fuhgl::math::Mat4 m = fuhgl::math::translate(c.position) *
                                                fuhgl::math::rotateY(c.rotY) *
                                                fuhgl::math::scale({c.scale, c.scale, c.scale});
                    const fuhgl::math::Mat3 m3(m);
                    const std::uint32_t b0 = static_cast<std::uint32_t>(baked.vertexCount());
                    for (std::size_t i = 0; i < c.mesh.vertexCount(); ++i) {
                        const float* v = &c.mesh.vertices[i * 11];
                        const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{v[0], v[1], v[2], 1.f};
                        const fuhgl::math::Vec3 n = fuhgl::math::normalize(m3 * fuhgl::math::Vec3{v[3], v[4], v[5]});
                        const float nv[11] = {p.x, p.y, p.z, n.x, n.y, n.z, v[6], v[7], v[8], v[9], v[10]};
                        baked.vertices.insert(baked.vertices.end(), nv, nv + 11);
                    }
                    for (const std::uint32_t idx : c.mesh.indices) baked.indices.push_back(b0 + idx);
                }
                if (baked.vertexCount() && attachMeshData(host, base + "_clutter", baked, wnode, Vec3{0.f, 0.f, 0.f})) ++attached;
            }
        }
    }
    return payload({{"mode", jsonString(mode)}, {"seed", jsonNumber(p.seed)},
                    {"gridW", jsonNumber(w.gridW)}, {"gridH", jsonNumber(w.gridH)},
                    {"heightScale", jsonNumber(w.heightScale)},
                    {"waterLevelWorld", jsonNumber(w.waterLevelWorld)},
                    {"trees", jsonNumber(static_cast<double>(w.trees.size()))},
                    {"bushes", jsonNumber(static_cast<double>(w.bushes.size()))},
                    {"rocks", jsonNumber(static_cast<double>(w.rocks.size()))},
                    {"buildings", jsonNumber(static_cast<double>(w.buildings.size()))},
                    {"cityCells", jsonNumber(static_cast<double>(w.cells.size()))},
                    {"settlements", jsonNumber(static_cast<double>(w.settlements.size()))},
                    {"roadProps", jsonNumber(static_cast<double>(w.roadProps.size()))},
                    {"clutter", jsonNumber(static_cast<double>(w.clutter.size()))},
                    {"lifeZones", jsonNumber(static_cast<double>(w.lifeZones.size()))},
                    {"attachedNodes", jsonNumber(attached)}});
}

} // namespace tc
} // namespace fuhglgraphics::mcp
