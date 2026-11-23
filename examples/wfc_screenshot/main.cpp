// G27 wave-function-collapse screenshot tool: a dungeon layout solved by procedural::Wfc
// (14x14 grid, edge-tag tiles with 4 rotations + backtracking), instantiated as scene nodes
// (one shared mesh per tile+rotation), with torches (emissive + point lights + bloom),
// wear decals on the floor (G8), dim directional light with CSM shadows, SSAO, volumetric
// fog, TAA + upscale + ACES. Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_wfc_screenshot --prefix g27_wfc [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--seed 777]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string prefix = "g27_wfc";
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f;
    unsigned seed = 777u;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) a.seed = static_cast<unsigned>(std::atoi(argv[++i]));
    }
    return a;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

// append `src` transformed by `m` into `dst` (MeshData, 11 floats/vertex)
void appendTransformed(fuhgl::render::MeshData& dst, const fuhgl::render::MeshData& src,
                       const fuhgl::math::Mat4& m) {
    const fuhgl::math::Mat3 m3(m);
    const std::uint32_t base = static_cast<std::uint32_t>(dst.vertexCount());
    for (std::size_t i = 0; i < src.vertexCount(); ++i) {
        const float* v = &src.vertices[i * 11];
        const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{v[0], v[1], v[2], 1.f};
        const fuhgl::math::Vec3 n = fuhgl::math::normalize(m3 * fuhgl::math::Vec3{v[3], v[4], v[5]});
        dst.vertices.push_back(p.x); dst.vertices.push_back(p.y); dst.vertices.push_back(p.z);
        dst.vertices.push_back(n.x); dst.vertices.push_back(n.y); dst.vertices.push_back(n.z);
        for (int k = 6; k < 11; ++k) dst.vertices.push_back(v[k]);
    }
    for (const std::uint32_t idx : src.indices) dst.indices.push_back(base + idx);
}

bool renderToPng(fuhglgraphics::scene::Scene& scn, fuhgl::FirstPersonCamera& cam,
                 fuhglgraphics::render::DeferredRenderer& renderer, int frames, int width, int height,
                 const std::string& outPath) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = {0.05f, 0.045f, 0.06f};
        frame.view.fogDensity = 0.006f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    fuhgl::gl::readPixels(0, 0, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    return fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), width, height);
}

} // namespace

int main(int argc, char** argv) {
    using namespace fuhglgraphics;
    const Args args = parseArgs(argc, argv);
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_wfc_screenshot", 64, 64)) {
        std::fprintf(stderr, "wfc_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "wfc_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- G27: solve the dungeon layout ---
    const int GW = 14, GH = 14;
    const float CELL = 4.f;
    procedural::Wfc wfc(GW, GH, "w");
    const auto tiles = procedural::Wfc::makeDungeonTiles();
    for (const auto& t : tiles) wfc.addTile(t);
    const auto cells = wfc.solve(args.seed);
    int unsolved = 0, floors = 0, walls = 0, doors = 0;
    for (const auto& c : cells) {
        if (c.tile < 0) { ++unsolved; continue; }
        const std::string n = tiles[c.tile].name;
        if (n == "floor") ++floors;
        else if (n == "doorH" || n == "doorV") ++doors;
        else ++walls;
    }
    std::fprintf(stderr, "wfc_screenshot: %dx%d solved (seed %u): floor=%d wall=%d door=%d unsolved=%d\n",
                 GW, GH, args.seed, floors, walls, doors, unsolved);

    // --- tile meshes (built once per tile name, oriented per rotation at placement) ---
    // base tile geometry, centered at the cell origin (x,z), y up
    fuhgl::render::MeshData floorTile, wallTile, capTile, cornerTile, pillarTile, doorTile;
    {
        fuhgl::render::MeshData b;
        b = fuhgl::render::shapes::boxData({CELL * 0.5f, 0.1f, CELL * 0.5f}, {1, 1, 1});
        floorTile = b; // slab at y=0
        wallTile = fuhgl::render::shapes::boxData({CELL * 0.5f, 1.6f, 0.22f}, {1, 1, 1});
        wallTile = [&] { // shift up so the wall sits on the floor
            auto m = wallTile;
            for (std::size_t i = 0; i < m.vertexCount(); ++i) m.vertices[i * 11 + 1] += 1.7f;
            return m;
        }();
        capTile = wallTile;
        cornerTile = wallTile;
        doorTile = fuhgl::render::shapes::boxData({CELL * 0.5f, 0.55f, 0.22f}, {1, 1, 1});
        for (std::size_t i = 0; i < doorTile.vertexCount(); ++i) doorTile.vertices[i * 11 + 1] += 0.65f;
        pillarTile = fuhgl::render::shapes::boxData({0.28f, 1.6f, 0.28f}, {1, 1, 1});
        for (std::size_t i = 0; i < pillarTile.vertexCount(); ++i) pillarTile.vertices[i * 11 + 1] += 1.7f;
    }

    scene::Scene scene;
    // one node per (tile, rot): all cells of that kind merged into one MeshData
    std::map<std::pair<int, int>, fuhgl::render::MeshData> merged;
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> meshes;
    std::map<std::pair<int, int>, fuhgl::render::Mesh*> meshOf;
    std::vector<fuhgl::math::Vec3> doorSpots, floorSpots;
    for (int z = 0; z < GH; ++z)
        for (int x = 0; x < GW; ++x) {
            const auto& c = cells[x + GW * z];
            if (c.tile < 0) continue;
            const std::string name = tiles[c.tile].name;
            const fuhgl::render::MeshData* base = &floorTile;
            bool isFloor = name == "floor";
            if (name == "wallH" || name == "wallV") base = &wallTile;
            else if (name == "wallCap") base = &capTile;
            else if (name == "corner") base = &cornerTile;
            else if (name == "pillar") base = &pillarTile;
            else if (name == "doorH" || name == "doorV") base = &doorTile;
            const float wx = (x - (GW - 1) * 0.5f) * CELL;
            const float wz = (z - (GH - 1) * 0.5f) * CELL;
            const fuhgl::math::Mat4 m =
                fuhgl::math::translate({wx, 0.f, wz}) * fuhgl::math::rotateY(-c.rot * fuhgl::math::kPi * 0.5f);
            const auto key = std::make_pair(c.tile, c.rot);
            appendTransformed(merged[key], *base, m);
            const fuhgl::math::Vec3 center{wx, 0.f, wz};
            if (name == "doorH" || name == "doorV") doorSpots.push_back(center);
            if (isFloor) floorSpots.push_back(center);
        }
    for (auto& kv : merged) {
        meshes.push_back(std::make_unique<fuhgl::render::Mesh>());
        meshes.back()->setMeshData(kv.second);
        meshOf[kv.first] = meshes.back().get();
    }
    for (auto& kv : merged) {
        scene::Node& n = scene.root().createChild(tiles[kv.first.first].name);
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(meshOf[kv.first]);
        const std::string name = tiles[kv.first.first].name;
        if (name == "floor") {
            e.albedo = {0.36f, 0.34f, 0.30f};
            e.roughness = 0.95f;
            e.materialId = 1; // decal target (mask 1)
        } else if (name == "doorH" || name == "doorV") {
            e.albedo = {0.45f, 0.30f, 0.16f};
            e.roughness = 0.6f;
            e.materialId = 2;
        } else {
            e.albedo = {0.42f, 0.40f, 0.38f};
            e.roughness = 0.9f;
            e.materialId = 4;
        }
    }

    // --- torches: emissive flame + point light at some doors/floors ---
    fuhgl::render::Mesh flame;
    flame = fuhgl::render::makeSphere(0.16f)->buildMesh(8, {1.f, 0.6f, 0.2f}, fuhgl::render::SolidUv::Spherical);
    const int torchCount = 5;
    std::vector<fuhgl::math::Vec3> torchSpots;
    if (!doorSpots.empty()) torchSpots.push_back(doorSpots[args.seed % doorSpots.size()]);
    for (std::size_t i = 0; i < floorSpots.size() && static_cast<int>(torchSpots.size()) < torchCount; i += 17)
        torchSpots.push_back(floorSpots[(i * 7 + args.seed) % floorSpots.size()]);
    for (std::size_t i = 0; i < torchSpots.size(); ++i) {
        scene::Node& t = scene.root().createChild("torch" + std::to_string(i));
        t.translation = {torchSpots[i].x, 2.6f, torchSpots[i].z};
        scene::MeshEntry& e = t.attach<scene::MeshComponent>().addMesh(&flame);
        e.emissive = {3.5f, 1.6f, 0.5f};
        e.castShadow = false;
        scene.root().createChild("torchlight" + std::to_string(i)).attach<scene::LightComponent>().light =
            fuhgl::render::Light::point({torchSpots[i].x, 2.7f, torchSpots[i].z}, {1.0f, 0.5f, 0.22f}, 60.f, 14.f);
    }

    // --- wear decals on the floor (G8) ---
    for (int i = 0; i < 6 && !floorSpots.empty(); ++i) {
        const auto& p = floorSpots[(i * 13 + 5) % floorSpots.size()];
        scene::Node& d = scene.root().createChild("wear" + std::to_string(i));
        d.translation = {p.x, 0.21f, p.z};
        d.rotation = fuhgl::math::fromAxisAngle(fuhgl::math::radians(static_cast<float>((i * 67u) % 360u)), {0, 1, 0});
        render::Decal& dec = d.attach<scene::DecalComponent>().decal;
        dec.halfExtents = {1.1f, 0.05f, 1.1f};
        dec.albedo = {0.10f, 0.09f, 0.08f};
        dec.opacity = 0.55f;
        dec.materialIdMask = 1u; // floor material id = 1
    }

    // --- dim cool directional light (moon through cracks) with CSM shadows ---
    scene.root().createChild("moon").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.3f, -1.f, 0.2f}, {0.35f, 0.45f, 0.65f}, 0.8f);

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(60.f);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::Ssao);
    scene.root().effects.enable(render::Effect::Bloom);
    scene.root().effects.enable(render::Effect::VolumetricFog);
    scene.root().effects.enable(render::Effect::Decals);
    scene.root().effects.params.exposure = 1.15f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowCenter = {0.f, 1.f, 0.f};
    scene.root().effects.params.shadowRadius = 40.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 120.f;
    scene.root().effects.params.ssaoRadius = 1.6f;
    scene.root().effects.params.ssaoIntensity = 1.6f;
    scene.root().effects.params.bloomThreshold = 1.2f;
    scene.root().effects.params.bloomStrength = 1.0f;
    scene.root().effects.params.volumetricDensity = 0.02f;
    scene.root().effects.params.volumetricCloudiness = 0.3f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    renderer.skyTop = {0.02f, 0.02f, 0.04f};
    renderer.skyBottom = {0.05f, 0.045f, 0.06f};

    // shot 1: interior view from a floor cell toward a torch-lit door
    fuhgl::math::Vec3 eye = floorSpots.empty() ? fuhgl::math::Vec3{0, 1.7f, 20} : floorSpots[floorSpots.size() / 2];
    cam.position = {eye.x, 1.7f, eye.z};
    fuhgl::math::Vec3 tgt = doorSpots.empty() ? fuhgl::math::Vec3{eye.x, 1.5f, eye.z - 10}
                                              : doorSpots[0];
    aimAt(cam, {tgt.x, 1.6f, tgt.z});
    const std::string inPath = args.prefix + "_dungeon.png";
    if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, inPath)) {
        std::fprintf(stderr, "wfc_screenshot: failed to write %s\n", inPath.c_str());
        return 1;
    }
    std::fprintf(stderr, "wfc_screenshot: wrote %s\n", inPath.c_str());

    // shot 2: top-down overview of the whole solved layout
    cam.position = {0.f, 46.f, 0.01f};
    aimAt(cam, {0.f, 0.f, 0.f});
    const std::string ovPath = args.prefix + "_overview.png";
    if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, ovPath)) {
        std::fprintf(stderr, "wfc_screenshot: failed to write %s\n", ovPath.c_str());
        return 1;
    }
    std::fprintf(stderr, "wfc_screenshot: wrote %s (GL %d.%d %s)\n", ovPath.c_str(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
