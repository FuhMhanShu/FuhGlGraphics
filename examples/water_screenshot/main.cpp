// Water screenshot tool (G13): a G9 scene-graph lake scene — heightmap terrain with a
// flooded basin (shore foam), a Gerstner-wave water surface (scene::WaterComponent ->
// FrameData::water -> DeferredRenderer with Effect::Water: planar reflection, refraction,
// fresnel, wave-crest foam + sun glint), rocks sticking out of the water, shore trees, a
// procedural cloud sky cubemap (+ IBL env baked from it), a low sun and distance fog.
// Renders headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_water_screenshot --out file.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--msaa 0] [--time 4.0] [--cam 0|1|2]
//          [--refl-scale 0.75] [--no-ibl] [--no-shadows] [--no-fog]
//          [--no-reflection] [--no-foam] [--water-off]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/math/noise.hpp>
#include <fuhgl/render.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out = "water.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f, time = 4.f, reflScale = 0.75f;
    int cam = 0;
    bool ibl = true, shadows = true, fog = true, reflection = true, foam = true, water = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) a.msaa = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--time") == 0 && i + 1 < argc) a.time = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--refl-scale") == 0 && i + 1 < argc) a.reflScale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--cam") == 0 && i + 1 < argc) a.cam = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--no-ibl") == 0) a.ibl = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-fog") == 0) a.fog = false;
        else if (std::strcmp(argv[i], "--no-reflection") == 0) a.reflection = false;
        else if (std::strcmp(argv[i], "--no-foam") == 0) a.foam = false;
        else if (std::strcmp(argv[i], "--water-off") == 0) a.water = false;
    }
    return a;
}

float smoothstep01(float e0, float e1, float x) {
    const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
    return t * t * (3.f - 2.f * t);
}

// world direction of cubemap face texel (i, j) — conventions only need to be consistent
fuhgl::math::Vec3 cubeDir(int face, int i, int j, int size) {
    const float u = 2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(size) - 1.f;
    const float v = 2.f * (static_cast<float>(j) + 0.5f) / static_cast<float>(size) - 1.f;
    fuhgl::math::Vec3 d;
    switch (face) {
        case 0: d = {1.f, -v, -u}; break;  // +X
        case 1: d = {-1.f, -v, u}; break;  // -X
        case 2: d = {u, 1.f, v}; break;    // +Y
        case 3: d = {u, -1.f, -v}; break;  // -Y
        case 4: d = {u, -v, 1.f}; break;   // +Z
        default: d = {-u, -v, -1.f}; break; // -Z
    }
    return fuhgl::math::normalize(d);
}

// Procedural cloud sky cubemap: vertical sky gradient + fBm clouds + warm glow around the sun.
fuhgl::Texture makeCloudSky(int size, unsigned seed, const fuhgl::math::Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.05f, 0.22f, 0.58f}, horizon{0.42f, 0.58f, 0.82f};
    const fuhgl::math::Vec3 ground{0.35f, 0.38f, 0.42f}, cloudTint{1.f, 1.f, 1.f}, sunTint{1.f, 0.75f, 0.45f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float n = fuhgl::math::fbm3(dir.x * 3.5f + 7.3f, dir.y * 3.5f, dir.z * 3.5f, 5, seed);
                const float cl = smoothstep01(0.15f, 0.40f, n + 0.15f * dir.y);
                fuhgl::math::Vec3 col = fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                col = fuhgl::math::lerp(col, cloudTint, cl * 0.95f);
                const float sg = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 128.f);
                col += sunTint * sg * 2.5f;
                unsigned char* p = px.data() + (static_cast<std::size_t>(j) * size + i) * 4;
                p[0] = static_cast<unsigned char>(std::min(255.f, col.x * 255.f));
                p[1] = static_cast<unsigned char>(std::min(255.f, col.y * 255.f));
                p[2] = static_cast<unsigned char>(std::min(255.f, col.z * 255.f));
                p[3] = 255;
            }
        }
        cube.setImageCubeFace(order[face], 0, fuhgl::gl::GL_RGBA8, size, fuhgl::gl::GL_RGBA,
                              fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    }
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_R, fuhgl::gl::GL_CLAMP_TO_EDGE);
    return cube;
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
    if (!window.create("fuhglgraphics_water_screenshot", 64, 64)) {
        std::fprintf(stderr, "water_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "water_screenshot: context below GL 3.3\n");
        return 2;
    }

    // low sun toward -Z (drives the sun disk, the reflection glint and the shadows)
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.35f, 0.20f, -0.90f});
    const float waterLevel = 2.2f;

    // terrain: 220 x 220 world units, hills up to ~9 — the basin around y < waterLevel floods
    fuhgl::render::HeightmapTerrain terrain;
    const fuhgl::render::procedural::Image heightImg =
        fuhgl::render::procedural::heightmap(256, 256, 11u, 6, 0.05f);
    if (!terrain.create(heightImg, 220.f, 220.f, 4.5f)) {
        std::fprintf(stderr, "water_screenshot: terrain create failed\n");
        return 1;
    }

    // cloud sky cubemap + IBL environment baked from the same cube
    fuhgl::Texture cloudSky = makeCloudSky(256, 5u, sunDir);
    fuhgl::render::Environment env;
    if (cloudSky.id() != 0 && !env.createFromCubemap(cloudSky, 16, 32, 64))
        std::fprintf(stderr, "water_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.8f;

    // shared meshes: rock spheres, tree trunk + foliage
    fuhgl::render::Mesh trunk, foliage;
    fuhgl::render::shapes::box(trunk, {0.14f, 1.0f, 0.14f}, {0.32f, 0.22f, 0.15f});
    foliage = fuhgl::render::makeSphere(1.15f)->buildMesh(10, {0.16f, 0.42f, 0.16f},
                                                          fuhgl::render::SolidUv::Spherical);
    std::vector<fuhgl::render::Mesh> rocks;
    for (int i = 0; i < 4; ++i)
        rocks.push_back(fuhgl::render::makeSphere(1.f + 0.35f * i)->buildMesh(
            20, {0.42f, 0.40f, 0.38f}, fuhgl::render::SolidUv::Spherical));

    // camera presets: 0 = across the lake toward the sun, 1 = close over the waves, 2 = high
    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(55.f);
    fuhgl::math::Vec3 target;
    switch (args.cam) {
        case 1: cam.position = {6.f, waterLevel + 1.4f, 18.f}; target = {-5.f, waterLevel, -8.f}; break;
        case 2: cam.position = {0.f, 26.f, 58.f}; target = {0.f, 0.f, -12.f}; break;
        default: cam.position = {10.f, waterLevel + 7.f, 55.f}; target = {-6.f, waterLevel - 1.2f, -40.f}; break;
    }
    {
        const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
        cam.yaw = std::atan2(d.x, -d.z);
        cam.pitch = std::asin(d.y);
    }

    // --- G9 scene graph ---
    scene::Scene scene;
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::Water);
    scene.root().effects.params.exposure = 1.1f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowCenter = {0.f, 4.f, -10.f};
    scene.root().effects.params.shadowRadius = 110.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 300.f;
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene::Node& terrainNode = scene.root().createChild("terrain");
    scene::MeshEntry& te = terrainNode.attach<scene::MeshComponent>().addMesh(&terrain.mesh());
    te.albedo = {0.30f, 0.42f, 0.22f};
    te.roughness = 1.0f;
    te.castShadow = false;
    te.materialId = 1;

    // the water surface: node transform places the plane at the water level
    scene::Node& waterNode = scene.root().createChild("water");
    waterNode.translation = {0.f, waterLevel, 0.f};
    scene::WaterComponent& wc = waterNode.attach<scene::WaterComponent>();
    if (!wc.surface.create(160, 220.f)) {
        std::fprintf(stderr, "water_screenshot: water create failed\n");
        return 1;
    }
    wc.surface.waves = {
        {{1.f, 0.15f}, 0.32f, 16.f, 2.0f, 0.65f},
        {{0.7f, 0.7f}, 0.20f, 8.f, 1.4f, 0.55f},
        {{-0.5f, 0.86f}, 0.11f, 4.5f, 1.0f, 0.45f},
        {{0.2f, -1.f}, 0.07f, 2.8f, 0.8f, 0.35f},
    };
    wc.surface.time = 0.f;
    wc.surface.reflectionStrength = args.reflection ? 1.f : 0.f;
    if (!args.foam) wc.surface.foamStrength = 0.f; // otherwise keep the tuned default
    wc.surface.deepColor = {0.004f, 0.05f, 0.11f};
    wc.surface.shallowColor = {0.06f, 0.30f, 0.38f};

    // rocks half-submerged in the lake (reflection + foam ring around them)
    std::mt19937 rng(7u);
    std::uniform_real_distribution<float> pos(-70.f, 70.f);
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    int placedRocks = 0;
    for (int i = 0; i < 400 && placedRocks < 14; ++i) {
        const float x = pos(rng), z = pos(rng);
        if (z > 15.f) continue; // keep the area near the camera clear
        const float dxCam = cam.position.x - x, dzCam = cam.position.z - z;
        if (dxCam * dxCam + dzCam * dzCam < 15.f * 15.f) continue;
        const float h = terrain.heightAt(x, z);
        if (h < waterLevel - 1.6f || h > waterLevel + 0.9f) continue; // want partially submerged
        const float s = 0.7f + uni(rng) * 1.6f;
        scene::Node& rock = scene.root().createChild("rock" + std::to_string(placedRocks));
        rock.translation = {x, h + 0.15f * s, z};
        rock.scale = {s, s * (0.7f + 0.6f * uni(rng)), s};
        scene::MeshEntry& re = rock.attach<scene::MeshComponent>().addMesh(&rocks[placedRocks % 4]);
        re.albedo = {0.42f, 0.40f, 0.38f};
        re.roughness = 0.85f;
        re.castShadow = true;
        re.materialId = 20;
        ++placedRocks;
    }

    // trees on the shore (terrain above the water line)
    int placedTrees = 0;
    for (int i = 0; i < 600 && placedTrees < 60; ++i) {
        const float x = pos(rng), z = pos(rng);
        const float h = terrain.heightAt(x, z);
        if (h < waterLevel + 0.8f) continue;
        if ((x - cam.position.x) * (x - cam.position.x) + (z - cam.position.z) * (z - cam.position.z) < 25.f) continue;
        const float s = 0.9f + uni(rng) * 1.2f;
        scene::Node& tree = scene.root().createChild("tree" + std::to_string(placedTrees));
        tree.translation = {x, h + 0.2f, z};
        tree.scale = {s, s, s};
        tree.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
        scene::MeshComponent& mc = tree.attach<scene::MeshComponent>();
        scene::MeshEntry& tr = mc.addMesh(&trunk);
        tr.albedo = {0.32f, 0.22f, 0.15f};
        tr.roughness = 1.f;
        tr.castShadow = true;
        tr.materialId = 30;
        scene::MeshEntry& fo = mc.addMesh(&foliage);
        fo.localOffset = fuhgl::math::translate({0.f, 1.7f, 0.f});
        const float g = 0.75f + uni(rng) * 0.5f;
        fo.albedo = {0.10f * g, 0.32f * g, 0.12f * g};
        fo.roughness = 0.9f;
        fo.castShadow = true;
        fo.materialId = 31;
        ++placedTrees;
    }
    std::fprintf(stderr, "water_screenshot: placed %d rocks, %d trees\n", placedRocks, placedTrees);

    scene::Node& sunNode = scene.root().createChild("sun");
    sunNode.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.72f, 0.48f}, 3.4f);

    // --- renderer ---
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "water_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    renderer.setWaterReflectionScale(args.reflScale);
    if (args.shadows) renderer.enableCsmShadows(1024, 4);
    if (cloudSky.id() != 0) renderer.setSkyCubemap(&cloudSky);
    renderer.skyTop = {0.20f, 0.40f, 0.78f};
    renderer.skyBottom = {0.72f, 0.80f, 0.90f};

    scene::FrameData frame;
    scene.fillFrame(frame, nullptr, args.width, args.height);
    if (!args.shadows) {
        frame.effects.disable(render::Effect::Shadows);
        frame.effects.disable(render::Effect::CsmShadows);
    }
    if (!args.ibl) frame.effects.disable(render::Effect::Ibl);
    if (!args.water) frame.effects.disable(render::Effect::Water);
    frame.effects.applyTo(renderer);
    frame.view.env = args.ibl ? &env : nullptr;
    frame.view.fogColor = {0.62f, 0.72f, 0.84f};
    frame.view.fogDensity = args.fog ? 0.0018f : 0.f;

    const float dt = args.time / static_cast<float>(std::max(args.frames, 1));
    for (int i = 0; i < args.frames; ++i) {
        wc.surface.update(dt); // advance the waves
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals, frame.water);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "water_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "water_screenshot: wrote %s (%dx%d, %d frames, scale %.2f, cam %d, time %.2f, "
                 "refl=%d foam=%d water=%d, items %zu, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, renderer.renderScale(), args.cam,
                 args.time, args.reflection ? 1 : 0, args.foam ? 1 : 0, args.water ? 1 : 0,
                 frame.view.items.size(), fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
