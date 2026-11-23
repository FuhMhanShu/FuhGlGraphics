// G19 serialization screenshot tool: builds a small scene (ground quad + three crates + sun),
// renders it (<prefix>_original.png), saves it to a .json file, injects procedural mesh specs
// into the mesh entries (what an editor would store), loads the file back with
// scene::defaultShapeResolver and renders the loaded scene (<prefix>_loaded.png). Both images
// must match (round-trip). Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_serialization_screenshot --prefix g19 [--scene scene.json]
//          [--width 1280] [--height 720] [--frames 16]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "../screenshot/png_writer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string prefix = "g19_serialization";
    std::string scene;
    int width = 1280, height = 720, frames = 16;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) a.scene = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
    }
    return a;
}

fuhglgraphics::io::Json strJson(const std::string& s) {
    fuhglgraphics::io::Json j;
    j.type = fuhglgraphics::io::Json::Type::String;
    j.str = s;
    return j;
}

fuhglgraphics::io::Json numJson(double v) {
    fuhglgraphics::io::Json j;
    j.type = fuhglgraphics::io::Json::Type::Number;
    j.number = v;
    return j;
}

fuhglgraphics::io::Json vec3Json(const fuhgl::math::Vec3& v) {
    fuhglgraphics::io::Json j;
    j.type = fuhglgraphics::io::Json::Type::Array;
    j.arr.push_back(numJson(v.x));
    j.arr.push_back(numJson(v.y));
    j.arr.push_back(numJson(v.z));
    return j;
}

// Sets the "mesh" spec of every mesh entry of the node (recursively) — this is what an editor
// stores so the scene file is self-contained (GL meshes are re-created by the resolver on load).
void injectMeshSpecs(fuhglgraphics::io::Json& nodeJson, const std::string& nodeName) {
    using fuhglgraphics::io::Json;
    auto it = nodeJson.obj.find("components");
    if (it != nodeJson.obj.end())
        for (Json& c : it->second.arr) {
            if (c.at("type").asString() != "mesh") continue;
            auto eit = c.obj.find("entries");
            if (eit == c.obj.end()) continue;
            Json spec;
            spec.type = Json::Type::Object;
            if (nodeName == "ground") {
                spec.obj["shape"] = strJson("quad");
                spec.obj["halfSize"] = numJson(30.0);
                spec.obj["color"] = vec3Json({0.35f, 0.34f, 0.31f});
            } else {
                spec.obj["shape"] = strJson("box");
                spec.obj["halfExtents"] = vec3Json({0.8f, 0.8f, 0.8f});
                spec.obj["color"] = vec3Json({1.f, 1.f, 1.f});
            }
            for (Json& e : eit->second.arr) e.obj["mesh"] = spec;
        }
    auto cit = nodeJson.obj.find("children");
    if (cit != nodeJson.obj.end())
        for (Json& ch : cit->second.arr) injectMeshSpecs(ch, ch.at("name").asString(""));
}

// Renders `scn` with `renderer`/`cam` and writes a PNG; returns false on write failure.
bool renderToPng(fuhglgraphics::scene::Scene& scn, fuhgl::FirstPersonCamera& cam,
                 fuhglgraphics::render::DeferredRenderer& renderer, int frames, int width, int height,
                 const std::string& outPath) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = {0.88f, 0.84f, 0.74f};
        frame.view.fogDensity = 0.0015f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
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
    if (!window.create("fuhglgraphics_serialization_screenshot", 64, 64)) {
        std::fprintf(stderr, "serialization_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "serialization_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- build the scene programmatically ---
    scene::Scene scene;
    fuhgl::render::Mesh ground, box;
    fuhgl::render::shapes::quad(ground, 30.f, {0.35f, 0.34f, 0.31f});
    fuhgl::render::shapes::box(box, {0.8f, 0.8f, 0.8f}, {1, 1, 1});

    scene::Node& g = scene.root().createChild("ground");
    scene::MeshEntry& ge = g.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.35f, 0.34f, 0.31f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir{0.5f, 0.7f, 0.4f};
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.92f, 0.78f}, 3.0f);

    const fuhgl::math::Vec3 crateColors[3] = {{0.8f, 0.45f, 0.2f}, {0.25f, 0.55f, 0.8f}, {0.3f, 0.75f, 0.35f}};
    const char* names[3] = {"crateA", "crateB", "crateC"};
    const fuhgl::math::Vec3 pos[3] = {{-2.2f, 0.8f, -1.0f}, {0.4f, 0.8f, -2.8f}, {2.4f, 0.8f, -0.6f}};
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild(names[i]);
        n.translation = pos[i];
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&box);
        e.albedo = crateColors[i];
        e.roughness = 0.6f;
        e.materialId = static_cast<std::uint32_t>(10 + i);
    }

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.5f, 3.2f, 7.5f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.6f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);

    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.64f}, skyHorizon{0.88f, 0.84f, 0.74f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "serialization_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.enableCsmShadows(1024, 4);
    renderer.setShadowSceneBounds({0.f, 1.f, 0.f}, 12.f);
    renderer.setShadowZRange(0.5f, 40.f);
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    // --- 1) render the original scene ---
    const std::string originalPath = args.prefix + "_original.png";
    if (!renderToPng(scene, cam, renderer, args.frames, args.width, args.height, originalPath)) {
        std::fprintf(stderr, "serialization_screenshot: failed to write %s\n", originalPath.c_str());
        return 1;
    }
    std::fprintf(stderr, "serialization_screenshot: wrote %s\n", originalPath.c_str());

    // --- 2) save to JSON, inject mesh specs, reload ---
    const std::string scenePath =
        args.scene.empty() ? (std::filesystem::temp_directory_path() / "fgg-g19-scene.json").string()
                           : args.scene;
    std::string err;
    if (!scene::saveScene(scene, scenePath, &err)) {
        std::fprintf(stderr, "serialization_screenshot: saveScene failed: %s\n", err.c_str());
        return 1;
    }
    io::Json j = scene::sceneToJson(scene);
    injectMeshSpecs(j.obj["root"], "root");
    if (!io::writeJsonFile(scenePath, j, &err)) {
        std::fprintf(stderr, "serialization_screenshot: writeJsonFile failed: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "serialization_screenshot: wrote scene file %s (%zu bytes)\n",
                 scenePath.c_str(), std::filesystem::file_size(scenePath));

    scene::Scene loaded;
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> meshArena;
    if (!scene::loadScene(loaded, scenePath, scene::defaultShapeResolver, meshArena, &err)) {
        std::fprintf(stderr, "serialization_screenshot: loadScene failed: %s\n", err.c_str());
        return 1;
    }
    const bool roundTripEqual = io::writeJson(scene::sceneToJson(scene), 0) ==
                                io::writeJson(scene::sceneToJson(loaded), 0);
    std::fprintf(stderr,
                 "serialization_screenshot: loaded %s (mesh arena=%zu, round-trip JSON equal=%d)\n",
                 scenePath.c_str(), meshArena.size(), (int)roundTripEqual);

    // --- 3) render the loaded scene ---
    const std::string loadedPath = args.prefix + "_loaded.png";
    if (!renderToPng(loaded, cam, renderer, args.frames, args.width, args.height, loadedPath)) {
        std::fprintf(stderr, "serialization_screenshot: failed to write %s\n", loadedPath.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "serialization_screenshot: wrote %s (%dx%d, GL %d.%d %s)\n", loadedPath.c_str(),
                 args.width, args.height, fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
