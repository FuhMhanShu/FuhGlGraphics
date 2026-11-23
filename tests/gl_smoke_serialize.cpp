// Headless G19 smoke test (label "gl_context"): builds a small scene (box + ground + sun),
// saves it to a .json file, loads it back with the defaultShapeResolver (real GL box mesh),
// renders it through the G3 DeferredRenderer and verifies the frame is non-trivial and that
// the loaded scene has the expected items/lights. Skips cleanly with exit code 77 without GL.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

constexpr int kSkip = 77;
constexpr int W = 256, H = 256;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_serialize", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_serialize: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_serialize: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- build a scene programmatically (box mesh with a shape spec in mind) ---
    scene::Scene scene;
    fuhgl::render::Mesh box;
    if (!box.create()) return 1;
    box.setMeshData(fuhgl::render::shapes::boxData({0.8f, 0.8f, 0.8f}, {0.8f, 0.4f, 0.2f}));
    scene::Node& boxNode = scene.root().createChild("crate");
    boxNode.translation = {0.f, 0.8f, -1.f};
    scene::MeshEntry& be = boxNode.attach<scene::MeshComponent>().addMesh(&box);
    be.albedo = {0.8f, 0.4f, 0.2f};
    be.roughness = 0.5f;
    be.materialId = 2;
    // NOTE: mesh specs are app-provided; for this test we inject a box spec by re-saving with a
    // custom serializer hook: simplest is to write the JSON, patch the "mesh" field, reload.
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.4f, -0.9f, -0.3f}, {1.f, 0.95f, 0.9f}, 2.5f);
    scene.root().effects.enable(render::Effect::Tonemap);

    const auto dir = std::filesystem::temp_directory_path() / "fgg-gl-smoke-serialize";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "scene.json").string();
    std::string err;
    if (!scene::saveScene(scene, path, &err)) {
        std::fprintf(stderr, "gl_smoke_serialize: saveScene failed: %s\n", err.c_str());
        return 1;
    }

    // patch the saved JSON: give the crate's mesh entry a procedural box spec
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    io::Json j;
    if (!io::parseJson(text, j, &err)) {
        std::fprintf(stderr, "gl_smoke_serialize: parse failed: %s\n", err.c_str());
        return 1;
    }
    // find root.children[].components[].entries[0].mesh and set the spec
    io::Json* root = const_cast<io::Json*>(j.find("root"));
    io::Json* children = root ? const_cast<io::Json*>(root->find("children")) : nullptr;
    bool patched = false;
    if (children)
        for (io::Json& ch : children->arr) {
            io::Json* comps = const_cast<io::Json*>(ch.find("components"));
            if (!comps) continue;
            for (io::Json& c : comps->arr) {
                io::Json* entries = const_cast<io::Json*>(c.find("entries"));
                if (!entries) continue;
                for (io::Json& e : entries->arr) {
                    io::Json spec;
                    spec.type = io::Json::Type::Object;
                    spec.obj["shape"].type = io::Json::Type::String;
                    spec.obj["shape"].str = "box";
                    io::Json he;
                    he.type = io::Json::Type::Array;
                    for (double v : {0.8, 0.8, 0.8}) {
                        io::Json n;
                        n.type = io::Json::Type::Number;
                        n.number = v;
                        he.arr.push_back(n);
                    }
                    spec.obj["halfExtents"] = he;
                    e.obj["mesh"] = spec;
                    patched = true;
                }
            }
        }
    if (!patched) {
        std::fprintf(stderr, "gl_smoke_serialize: could not patch a mesh spec into the JSON\n");
        return 1;
    }
    if (!io::writeJsonFile(path, j, &err)) {
        std::fprintf(stderr, "gl_smoke_serialize: rewrite failed: %s\n", err.c_str());
        return 1;
    }

    // --- load back with the default shape resolver (creates a real GL box) ---
    scene::Scene loaded;
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> arena;
    if (!scene::loadScene(loaded, path, scene::defaultShapeResolver, arena, &err)) {
        std::fprintf(stderr, "gl_smoke_serialize: loadScene failed: %s\n", err.c_str());
        return 1;
    }
    if (arena.size() != 1) {
        std::fprintf(stderr, "gl_smoke_serialize: expected 1 resolved mesh, got %zu\n", arena.size());
        return 1;
    }

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.f, 5.f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(fuhgl::math::Vec3{0.f, 0.8f, -1.f} - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) return 1;
    renderer.setEffectEnabled(render::Effect::Taa, false);

    scene::FrameData frame;
    loaded.fillFrame(frame, &cam, W, H);
    if (frame.view.items.size() != 1 || frame.view.lights.size() != 1) {
        std::fprintf(stderr, "gl_smoke_serialize: loaded frame has %zu items / %zu lights\n",
                     frame.view.items.size(), frame.view.lights.size());
        return 1;
    }
    frame.effects.applyTo(renderer);
    renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                    frame.lightProbes, &frame.decals, frame.water, &frame.skins);

    renderer.output().bind();
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
    fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    fuhgl::gl::flush();
    long sum = 0;
    for (std::size_t i = 0; i < px.size(); i += 4) sum += px[i] + px[i + 1] + px[i + 2];
    std::fprintf(stderr, "gl_smoke_serialize: rendered sum=%ld (items=%zu lights=%zu)\n", sum,
                 frame.view.items.size(), frame.view.lights.size());
    if (sum < 1000) {
        std::fprintf(stderr, "gl_smoke_serialize: rendered frame too dark — loaded scene broken?\n");
        return 1;
    }
    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_serialize: GL error left over\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_serialize: OK (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
