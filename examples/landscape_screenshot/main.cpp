// Landscape screenshot tool: a G9 scene-graph landscape — heightmap terrain with many
// trees (each tree node holds TWO mesh entries: trunk + foliage), a procedural cloud
// sky cubemap (renderer.setSkyCubemap + IBL env baked from the same cube), a low warm
// sun (sun disk via the directional light), distance fog and god rays
// (renderer.godRays). Renders headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_landscape_screenshot --out file.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--msaa 0] [--no-rays] [--no-fog]
//          [--no-clouds] [--no-shadows] [--no-ibl]
//          [--shadow-filter pcf3x3|poisson|pcss|vsm] [--cascade-blend 0.25]
//          [--light-size 0.6] [--point-shadows] [--contact-shadows] [--no-snap]
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
    std::string out = "landscape.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f;
    bool rays = true, fog = true, clouds = true, shadows = true, ibl = true;
    std::string shadowFilter = "pcss"; // pcf3x3 | poisson | pcss | vsm (F45 shadow kit)
    float cascadeBlend = 0.25f, lightSize = 0.6f;
    bool pointShadows = false, contactShadows = false, snap = true;
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
        else if (std::strcmp(argv[i], "--no-rays") == 0) a.rays = false;
        else if (std::strcmp(argv[i], "--no-fog") == 0) a.fog = false;
        else if (std::strcmp(argv[i], "--no-clouds") == 0) a.clouds = false;
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-ibl") == 0) a.ibl = false;
        else if (std::strcmp(argv[i], "--shadow-filter") == 0 && i + 1 < argc) a.shadowFilter = argv[++i];
        else if (std::strcmp(argv[i], "--cascade-blend") == 0 && i + 1 < argc) a.cascadeBlend = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--light-size") == 0 && i + 1 < argc) a.lightSize = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--point-shadows") == 0) a.pointShadows = true;
        else if (std::strcmp(argv[i], "--contact-shadows") == 0) a.contactShadows = true;
        else if (std::strcmp(argv[i], "--no-snap") == 0) a.snap = false;
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
    cube.bind(); // setImageCubeFace/setParameteri upload to the currently bound texture
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.06f, 0.24f, 0.62f}, horizon{0.40f, 0.55f, 0.80f};
    const fuhgl::math::Vec3 ground{0.42f, 0.45f, 0.48f}, cloudTint{1.f, 1.f, 1.f}, sunTint{1.f, 0.78f, 0.5f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float n = fuhgl::math::fbm3(dir.x * 3.5f + 7.3f, dir.y * 3.5f, dir.z * 3.5f, 5, seed);
                // fbm3 is ~N(0, 0.25) (p90 ~ 0.29): thresholds 0.15..0.40 -> ~28% coverage, white cores
                const float cl = smoothstep01(0.15f, 0.40f, n + 0.15f * dir.y);
                fuhgl::math::Vec3 col = fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                col = fuhgl::math::lerp(col, cloudTint, cl * 0.95f);
                const float sg = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 128.f);
                col += sunTint * sg * 2.0f;
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
    if (!window.create("fuhglgraphics_landscape_screenshot", 64, 64)) {
        std::fprintf(stderr, "landscape_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "landscape_screenshot: context below GL 3.3\n");
        return 2;
    }

    // low warm sun, in view toward -Z (also drives the sun disk: sunColor = color*intensity*4)
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.30f, 0.17f, -0.93f}); // low sun ~10 deg

    // terrain
    fuhgl::render::HeightmapTerrain terrain;
    const fuhgl::render::procedural::Image heightImg =
        fuhgl::render::procedural::heightmap(256, 256, 11u, 6, 0.045f);
    if (!terrain.create(heightImg, 160.f, 160.f, 7.f)) {
        std::fprintf(stderr, "landscape_screenshot: terrain create failed\n");
        return 1;
    }

    // cloud sky cubemap + IBL environment baked from the same cube
    fuhgl::Texture cloudSky = makeCloudSky(256, 5u, sunDir);
    fuhgl::render::Environment env;
    if (cloudSky.id() != 0 && !env.createFromCubemap(cloudSky, 16, 32, 64))
        std::fprintf(stderr, "landscape_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.7f;

    // shared tree meshes (trunk + foliage); every tree node references both
    fuhgl::render::Mesh trunk, foliage;
    fuhgl::render::shapes::box(trunk, {0.14f, 1.0f, 0.14f}, {0.32f, 0.22f, 0.15f});
    foliage = fuhgl::render::makeSphere(1.15f)->buildMesh(10, {0.16f, 0.42f, 0.16f},
                                                          fuhgl::render::SolidUv::Spherical);

    // camera: on the highest terrain point of the strip in front of the view corridor,
    // so the forest + sun are actually visible (not hidden behind a hill)
    float bestH = -1e9f;
    float bestX = 0.f, bestZ = 30.f;
    for (int gz = 20; gz <= 40; gz += 2)
        for (int gx = -12; gx <= 12; gx += 2) {
            const float hh = terrain.heightAt(static_cast<float>(gx), static_cast<float>(gz));
            if (hh > bestH) {
                bestH = hh;
                bestX = static_cast<float>(gx);
                bestZ = static_cast<float>(gz);
            }
        }
    fuhgl::FirstPersonCamera cam;
    cam.position = {bestX, bestH + 3.5f, bestZ};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{-10.f, bestH - 2.f, -45.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);

    // --- G9 scene graph ---
    scene::Scene scene;
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.exposure = 1.0f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowStrength = 1.f;
    scene.root().effects.params.shadowCenter = {0.f, 8.f, -10.f};
    scene.root().effects.params.shadowRadius = 90.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 300.f;
    // F45 shadow-kit effect toggles (the effect LIST is a union over all nodes, so root is fine)
    if (args.contactShadows) scene.root().effects.enable(render::Effect::ContactShadows);
    if (args.pointShadows) {
        scene.root().effects.enable(render::Effect::PointShadows);
        // a warm lamp on the terrain near the view corridor — the first point light gets
        // the cube shadow map (F46)
        const float lampX = 4.f, lampZ = -8.f;
        const float lampY = terrain.heightAt(lampX, lampZ) + 2.2f;
        scene::Node& lamp = scene.root().createChild("lamp");
        lamp.translation = {lampX, lampY, lampZ};
        lamp.attach<scene::LightComponent>().light =
            fuhgl::render::Light::point({lampX, lampY, lampZ}, {1.0f, 0.62f, 0.30f}, 260.f, 40.f);
    }

    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene::Node& terrainNode = scene.root().createChild("terrain");
    scene::MeshEntry& te = terrainNode.attach<scene::MeshComponent>().addMesh(&terrain.mesh());
    te.albedo = {0.25f, 0.38f, 0.17f};
    te.roughness = 1.0f;
    te.castShadow = false;
    te.materialId = 1;

    // vegetation: many tree nodes, two mesh entries each (trunk + foliage), on the terrain
    std::mt19937 rng(42u);
    std::uniform_real_distribution<float> wedge(-1.6f, 1.6f); // around -Z (the view direction)
    std::uniform_real_distribution<float> radius(6.f, 55.f);
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    int placed = 0;
    for (int i = 0; i < 260 && placed < 170; ++i) {
        const float a = fuhgl::math::kPi + wedge(rng), r = radius(rng); // z = sin(a) < 0: in front
        const float x = std::cos(a) * r, z = std::sin(a) * r;
        const float dxCam = cam.position.x - x, dzCam = cam.position.z - z;
        if (dxCam * dxCam + dzCam * dzCam < 36.f) continue; // keep the camera spot clear
        const float s = 0.9f + uni(rng) * 1.3f;
        const float y = terrain.heightAt(x, z) + 0.2f;
        scene::Node& tree = scene.root().createChild("tree" + std::to_string(placed));
        tree.translation = {x, y, z};
        tree.scale = {s, s, s};
        tree.rotation = fuhgl::math::fromAxisAngle(uni(rng) * 2.f * fuhgl::math::kPi, {0.f, 1.f, 0.f});
        scene::MeshComponent& mc = tree.attach<scene::MeshComponent>();
        scene::MeshEntry& tr = mc.addMesh(&trunk); // mesh #1: trunk
        tr.albedo = {0.32f, 0.22f, 0.15f};
        tr.roughness = 1.f;
        tr.castShadow = true;
        tr.materialId = 30;
        scene::MeshEntry& fo = mc.addMesh(&foliage); // mesh #2: foliage, offset up in node space
        fo.localOffset = fuhgl::math::translate({0.f, 1.7f, 0.f});
        const float g = 0.75f + uni(rng) * 0.5f;
        fo.albedo = {0.10f * g, 0.32f * g, 0.12f * g};
        fo.roughness = 0.9f;
        fo.castShadow = true;
        fo.materialId = 31;
        ++placed;
    }
    std::fprintf(stderr, "landscape_screenshot: placed %d trees\n", placed);

    scene::Node& sunNode = scene.root().createChild("sun");
    sunNode.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.65f, 0.40f}, 3.2f); // warm low sun

    // --- renderer ---
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "landscape_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) renderer.enableCsmShadows(1024, 4);
    if (args.clouds && cloudSky.id() != 0) renderer.setSkyCubemap(&cloudSky);
    renderer.godRays.enabled = args.rays;
    renderer.godRays.strength = 0.35f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 48;
    renderer.skyTop = {0.22f, 0.42f, 0.82f};
    renderer.skyBottom = {0.78f, 0.84f, 0.92f};

    scene::FrameData frame;
    scene.fillFrame(frame, nullptr, args.width, args.height);
    if (!args.shadows) {
        frame.effects.disable(render::Effect::Shadows);
        frame.effects.disable(render::Effect::CsmShadows);
        frame.effects.disable(render::Effect::PointShadows);
        frame.effects.disable(render::Effect::ContactShadows);
    }
    if (!args.ibl) frame.effects.disable(render::Effect::Ibl);
    // F45 shadow-kit params: set on the MERGED frame params (per-node merge would let the
    // last-visited child overwrite root params, so set them after fillFrame)
    if (args.shadowFilter == "poisson") frame.effects.params.shadowFilter = fuhgl::render::ShadowFilter::PoissonPcf;
    else if (args.shadowFilter == "pcss") frame.effects.params.shadowFilter = fuhgl::render::ShadowFilter::Pcss;
    else if (args.shadowFilter == "vsm") frame.effects.params.shadowFilter = fuhgl::render::ShadowFilter::Vsm;
    else frame.effects.params.shadowFilter = fuhgl::render::ShadowFilter::Pcf3x3;
    frame.effects.params.shadowCascadeBlend = args.cascadeBlend;
    frame.effects.params.shadowLightSize = args.lightSize;
    frame.effects.params.shadowSnapToTexels = args.snap;
    std::fprintf(stderr, "landscape_screenshot: lights=%zu items=%zu pointShadowFx=%d\n",
                 frame.view.lights.size(), frame.view.items.size(),
                 frame.effects.enabled(render::Effect::PointShadows) ? 1 : 0);
    frame.effects.applyTo(renderer);
    frame.view.env = args.ibl ? &env : nullptr;
    frame.view.fogColor = {0.95f, 0.80f, 0.62f}; // warm haze
    frame.view.fogDensity = args.fog ? 0.004f : 0.f;

    for (int i = 0; i < args.frames; ++i)
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "landscape_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "landscape_screenshot: wrote %s (%dx%d, %d frames, scale %.2f, items %zu, rays=%d fog=%d, "
                 "GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, renderer.renderScale(),
                 frame.view.items.size(), args.rays ? 1 : 0, args.fog ? 1 : 0, fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
