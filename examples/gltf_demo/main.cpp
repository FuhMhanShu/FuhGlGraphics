// G4 glTF demo: loads a glTF 2.0 model (.gltf/.glb) with io::loadGltf, instantiates its
// default scene into a G9 scene graph via io::instantiateGltf (GL meshes + materials +
// textures, node TRS preserved), centers/scales the model onto a ground plaza, adds a sun,
// fill lights and a gradient sky cubemap (+ IBL environment), renders headless through the
// G3 DeferredRenderer and writes a PNG — used for the G4 tutorial images in docs/images/.
//
// Usage: fuhglgraphics_gltf_demo --model model.glb --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--msaa 0] [--size 4] [--azimuth 35] [--elevation 14]
//          [--distance 9] [--sun-azimuth 160] [--sun-elevation 30] [--no-shadows] [--no-ibl]
//          [--no-rays] [--no-sky]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string model, out = "gltf_demo.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f, size = 4.f, distance = 0.f; // distance 0 -> auto from size
    float azimuth = 35.f, elevation = 14.f;
    float sunAzimuth = 160.f, sunElevation = 30.f;
    bool shadows = true, ibl = true, rays = true, sky = true, textures = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc) a.model = argv[++i];
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) a.msaa = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) a.size = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--distance") == 0 && i + 1 < argc) a.distance = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--azimuth") == 0 && i + 1 < argc) a.azimuth = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--elevation") == 0 && i + 1 < argc) a.elevation = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--sun-azimuth") == 0 && i + 1 < argc) a.sunAzimuth = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--sun-elevation") == 0 && i + 1 < argc) a.sunElevation = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-ibl") == 0) a.ibl = false;
        else if (std::strcmp(argv[i], "--no-rays") == 0) a.rays = false;
        else if (std::strcmp(argv[i], "--no-sky") == 0) a.sky = false;
        else if (std::strcmp(argv[i], "--no-tex") == 0) a.textures = false;
    }
    return a;
}

fuhgl::math::Vec3 dirFromAzEl(float azimuthDeg, float elevationDeg) {
    const float az = fuhgl::math::radians(azimuthDeg), el = fuhgl::math::radians(elevationDeg);
    return {std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)};
}

} // namespace

int main(int argc, char** argv) {
    using namespace fuhglgraphics;
    const Args args = parseArgs(argc, argv);
    if (args.model.empty()) {
        std::fprintf(stderr, "gltf_demo: --model <file.gltf|file.glb> is required\n");
        return 2;
    }
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_gltf_demo", 64, 64)) {
        std::fprintf(stderr, "gltf_demo: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gltf_demo: context below GL 3.3\n");
        return 2;
    }

    // --- G4: parse + instantiate ---
    io::GltfAsset asset;
    std::string err;
    if (!io::loadGltf(args.model, asset, &err)) {
        std::fprintf(stderr, "gltf_demo: loadGltf failed: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "gltf_demo: %s — meshes %zu, materials %zu, images %zu (%zu decoded), nodes %zu\n",
                 args.model.c_str(), asset.meshes.size(), asset.materials.size(), asset.images.size(),
                 [&] { std::size_t d = 0; for (const auto& im : asset.images) d += im.decoded ? 1 : 0; return d; }(),
                 asset.nodes.size());
    for (std::size_t i = 0; i < asset.images.size(); ++i)
        std::fprintf(stderr, "  image[%zu] uri=%s decoded=%d %dx%d\n", i, asset.images[i].uri.c_str(),
                     asset.images[i].decoded ? 1 : 0, asset.images[i].width, asset.images[i].height);
    for (std::size_t i = 0; i < asset.materials.size(); ++i)
        std::fprintf(stderr, "  material[%zu] name=%s baseColorTex=%d baseColor=(%.2f,%.2f,%.2f)\n", i,
                     asset.materials[i].name.c_str(), asset.materials[i].baseColorTexture,
                     asset.materials[i].baseColorFactor.x, asset.materials[i].baseColorFactor.y,
                     asset.materials[i].baseColorFactor.z);
    if (!asset.meshes.empty() && !asset.meshes[0].primitives.empty() &&
        asset.meshes[0].primitives[0].vertices.size() >= 11)
        std::fprintf(stderr, "  mesh[0].prim[0] v0 uv=(%.3f,%.3f) verts=%zu idx=%zu\n",
                     asset.meshes[0].primitives[0].vertices[8], asset.meshes[0].primitives[0].vertices[9],
                     asset.meshes[0].primitives[0].vertices.size() / 11, asset.meshes[0].primitives[0].indices.size());

    scene::Scene scene;
    scene::Node& modelNode = scene.root().createChild("model");
    io::GltfGpuResources gpu;
    if (!io::instantiateGltf(asset, scene, gpu, &modelNode, &err)) {
        std::fprintf(stderr, "gltf_demo: instantiateGltf failed: %s\n", err.c_str());
        return 1;
    }
    for (std::size_t i = 0; i < gpu.materials.size(); ++i)
        std::fprintf(stderr, "  gpu material[%zu]: albedoTexture id=%u normalMap=%p\n", i,
                     gpu.materials[i].albedoTexture ? gpu.materials[i].albedoTexture->id() : 0u,
                     static_cast<const void*>(gpu.materials[i].normalMap));
    std::fprintf(stderr, "  gpu textures=%zu meshes=%zu\n", gpu.textures.size(), gpu.meshes.size());
    if (!args.textures)
        for (auto& m : gpu.materials) m.albedoTexture = nullptr; // fall back to baseColorFactor

    // normalize the model: uniform scale so the longest side == args.size, then move the
    // AABB center to the origin and rest the bottom on the ground plane y = 0
    scene.updateWorldTransforms();
    if (modelNode.hasAabb()) {
        fuhgl::math::Vec3 mn, mx;
        modelNode.worldAabb(mn, mx);
        const fuhgl::math::Vec3 extents = mx - mn;
        const float longest = std::max(extents.x, std::max(extents.y, extents.z));
        const float s = longest > 1e-6f ? args.size / longest : 1.f;
        const fuhgl::math::Vec3 center = (mn + mx) * 0.5f;
        modelNode.scale = {s, s, s};
        modelNode.translation = {-center.x * s, -mn.y * s, -center.z * s};
        scene.updateWorldTransforms();
    }

    // --- stage: ground plaza, sun + fill lights, camera component ---
    const float plaza = std::max(12.f, args.size * 4.f);
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, plaza, {0.32f, 0.33f, 0.36f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.32f, 0.33f, 0.36f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    const fuhgl::math::Vec3 sunDir = dirFromAzEl(args.sunAzimuth, args.sunElevation); // toward the sun
    scene::Node& sunNode = scene.root().createChild("sun");
    sunNode.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.82f, 0.62f}, 3.0f);
    scene::Node& fillNode = scene.root().createChild("fill");
    fillNode.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(dirFromAzEl(args.sunAzimuth + 140.f, 55.f), {0.45f, 0.55f, 0.75f}, 0.7f);

    const float camDist = args.distance > 0.f ? args.distance : args.size * 2.4f;
    fuhgl::FirstPersonCamera cam;
    cam.position = dirFromAzEl(args.azimuth, args.elevation) * camDist + fuhgl::math::Vec3{0.f, args.size * 0.35f, 0.f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 target{0.f, args.size * 0.45f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // per-frame global effects: the union of all node effect lists (G9)
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowStrength = 1.f;
    scene.root().effects.params.shadowCenter = {0.f, args.size * 0.5f, 0.f};
    scene.root().effects.params.shadowRadius = args.size * 3.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = camDist * 4.f;

    // --- sky: gradient cubemap, shown directly and baked into an IBL environment ---
    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.66f}, skyHorizon{0.85f, 0.78f, 0.66f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "gltf_demo: skybox create failed, no sky cubemap\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);
    fuhgl::render::Environment env;
    if (args.ibl && skybox.texture().id() != 0 && !env.createFromCubemap(skybox.texture(), 16, 32, 64))
        std::fprintf(stderr, "gltf_demo: IBL bake failed, continuing without env\n");
    env.intensity = 0.6f;

    // --- renderer (G3) ---
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "gltf_demo: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(1024, 4);
        renderer.setShadowSceneBounds({0.f, args.size * 0.5f, 0.f}, args.size * 3.f);
        renderer.setShadowZRange(0.5f, camDist * 4.f);
    }
    if (args.sky && skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.godRays.enabled = args.rays;
    renderer.godRays.strength = 0.30f;
    renderer.godRays.decay = 0.94f;
    renderer.godRays.samples = 48;

    scene::FrameData frame;
    scene.fillFrame(frame, nullptr, args.width, args.height);
    if (!args.shadows) {
        frame.effects.disable(render::Effect::Shadows);
        frame.effects.disable(render::Effect::CsmShadows);
    }
    if (!args.ibl) frame.effects.disable(render::Effect::Ibl);
    frame.effects.applyTo(renderer);
    frame.view.env = args.ibl ? &env : nullptr;
    frame.view.fogColor = skyHorizon;
    frame.view.fogDensity = 0.0012f;

    for (int i = 0; i < args.frames; ++i)
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "gltf_demo: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "gltf_demo: wrote %s (%dx%d, %d frames, scale %.2f, items %zu, GL %d.%d %s)\n", args.out.c_str(),
                 args.width, args.height, args.frames, renderer.renderScale(), frame.view.items.size(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
