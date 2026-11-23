// G26 L-system screenshot tool: a forest of G26 L-system trees (3 variants) + ferns on a
// G24-noise terrain, instanced grass snapped via Instancing::heightFn, noon sun with CSM
// shadows + god rays, sky cubemap + IBL, SSAO, volumetric fog, TAA + upscale + ACES.
// Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_lsystem_screenshot --prefix g26_lsystem [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0]
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
#include <memory>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string prefix = "g26_lsystem";
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
    }
    return a;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

float smoothstep01(float e0, float e1, float x) {
    const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
    return t * t * (3.f - 2.f * t);
}

fuhgl::math::Vec3 cubeDir(int face, int i, int j, int size) {
    const float u = 2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(size) - 1.f;
    const float v = 2.f * (static_cast<float>(j) + 0.5f) / static_cast<float>(size) - 1.f;
    fuhgl::math::Vec3 d;
    switch (face) {
        case 0: d = {1.f, -v, -u}; break;
        case 1: d = {-1.f, -v, u}; break;
        case 2: d = {u, 1.f, v}; break;
        case 3: d = {u, -1.f, -v}; break;
        case 4: d = {u, -v, 1.f}; break;
        default: d = {-u, -v, -1.f}; break;
    }
    return fuhgl::math::normalize(d);
}

// midday sky cubemap (G24 turbulence clouds)
fuhgl::Texture makeDaySky(int size, const fuhglgraphics::procedural::Noise& noise,
                          const fuhgl::math::Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.10f, 0.32f, 0.78f}, horizon{0.66f, 0.78f, 0.95f};
    const fuhgl::math::Vec3 ground{0.42f, 0.44f, 0.40f}, sunTint{1.f, 0.95f, 0.8f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float n = noise.turbulence3(dir.x * 3.4f + 2.2f, dir.y * 3.4f, dir.z * 3.4f + 6.1f, 5,
                                                  fuhglgraphics::procedural::Noise::Base::Simplex);
                const float cl = smoothstep01(0.42f, 0.75f, n + 0.12f * dir.y);
                fuhgl::math::Vec3 col =
                    fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                col = fuhgl::math::lerp(col, {1.f, 1.f, 1.f}, cl * 0.95f);
                const float sg = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 128.f);
                col += sunTint * sg * 2.6f;
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

bool renderToPng(fuhglgraphics::scene::Scene& scn, fuhgl::FirstPersonCamera& cam,
                 fuhglgraphics::render::DeferredRenderer& renderer, const fuhgl::render::Environment* env,
                 int frames, int width, int height, const std::string& outPath) {
    using namespace fuhglgraphics;
    for (int i = 0; i < frames; ++i) {
        scene::FrameData frame;
        scn.fillFrame(frame, &cam, width, height);
        frame.effects.applyTo(renderer);
        frame.view.env = env;
        frame.view.fogColor = {0.66f, 0.78f, 0.95f};
        frame.view.fogDensity = 0.0012f;
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
    if (!window.create("fuhglgraphics_lsystem_screenshot", 64, 64)) {
        std::fprintf(stderr, "lsystem_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "lsystem_screenshot: context below GL 3.3\n");
        return 2;
    }

    const procedural::Noise noise(424242u);
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.35f, 0.55f, -0.75f});

    // --- terrain (G24 noise heightmap) ---
    scene::Scene scene;
    scene::Node& tn = scene.root().createChild("terrain");
    scene::TerrainComponent& tc = tn.attach<scene::TerrainComponent>();
    tc.levels = 4;
    tc.fovYDeg = 55.f;
    tc.albedo = {0.30f, 0.36f, 0.20f}; // grass-ish
    tc.customDistances = {60.f, 150.f, 300.f};
    if (!tc.create(160, 160, 220.f, 220.f, 16.f)) {
        std::fprintf(stderr, "lsystem_screenshot: terrain create failed\n");
        return 1;
    }
    for (int j = 0; j < tc.gridH; ++j)
        for (int i = 0; i < tc.gridW; ++i) {
            const float x = (static_cast<float>(i) / (tc.gridW - 1) - 0.5f) * tc.sizeX;
            const float z = (static_cast<float>(j) / (tc.gridH - 1) - 0.5f) * tc.sizeZ;
            float h = noise.height2(x * 0.02f, z * 0.02f, 5);
            h *= smoothstep01(130.f, 70.f, std::hypot(x, z));
            tc.heights[static_cast<std::size_t>(j) * tc.gridW + i] = h;
        }
    tc.rebuildLod();

    // --- G26 L-system trees: 3 variants, several instances with rotation/scale ---
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> treeMeshes;
    const fuhgl::math::Vec2 treeSpots[9] = {{-18.f, -10.f}, {8.f, -22.f}, {24.f, 2.f}, {-30.f, 12.f},
                                             {2.f, 8.f}, {-8.f, 26.f}, {34.f, -14.f}, {-40.f, -18.f},
                                             {16.f, 24.f}};
    for (int i = 0; i < 9; ++i) {
        const procedural::LSystem ls = procedural::LSystem::makeTree(i % 3, static_cast<unsigned>(i * 7 + 1));
        const fuhgl::render::MeshData md = ls.generate({0.30f, 0.20f, 0.12f}, {0.14f, 0.45f, 0.16f});
        treeMeshes.push_back(std::make_unique<fuhgl::render::Mesh>());
        treeMeshes.back()->setMeshData(md);
        scene::Node& n = scene.root().createChild("tree" + std::to_string(i));
        const float s = 0.8f + 0.35f * ((i * 37u) % 10u) / 10.f;
        n.translation = {treeSpots[i].x, tc.heightAt(treeSpots[i].x, treeSpots[i].y), treeSpots[i].y};
        n.rotation = fuhgl::math::fromAxisAngle(fuhgl::math::radians(static_cast<float>((i * 53u) % 360u)),
                                            {0.f, 1.f, 0.f});
        n.scale = {s, s, s};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(treeMeshes.back().get());
        e.albedo = {1.f, 1.f, 1.f};
        e.roughness = 0.9f;
        std::fprintf(stderr, "lsystem_screenshot: tree%d variant=%d %zu tris\n", i, i % 3,
                     md.indices.size() / 3);
    }

    // --- ferns at a few spots ---
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> fernMeshes;
    const fuhgl::math::Vec2 fernSpots[5] = {{-12.f, 4.f}, {14.f, -8.f}, {28.f, 14.f}, {-24.f, -2.f}, {6.f, 18.f}};
    for (int i = 0; i < 5; ++i) {
        const fuhgl::render::MeshData md = procedural::LSystem::makeFern().generate(
            {0.20f, 0.32f, 0.14f}, {0.18f, 0.50f, 0.20f});
        fernMeshes.push_back(std::make_unique<fuhgl::render::Mesh>());
        fernMeshes.back()->setMeshData(md);
        scene::Node& n = scene.root().createChild("fern" + std::to_string(i));
        const float s = 0.7f + 0.2f * (i % 3);
        n.translation = {fernSpots[i].x, tc.heightAt(fernSpots[i].x, fernSpots[i].y), fernSpots[i].y};
        n.scale = {s, s, s};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(fernMeshes.back().get());
        e.albedo = {1.f, 1.f, 1.f};
        e.roughness = 0.9f;
        e.castShadow = false;
    }

    // --- instanced grass snapped to the terrain via Instancing::heightFn ---
    fuhgl::render::Mesh blade;
    fuhgl::render::shapes::box(blade, {0.015f, 0.22f, 0.015f}, {0.25f, 0.45f, 0.18f});
    scene::Node& grass = scene.root().createChild("grass");
    grass.instancing.enabled = true;
    grass.instancing.count = 3000;
    grass.instancing.layout = scene::InstanceLayout::Random;
    grass.instancing.extent = {170.f, 3.f, 170.f};
    grass.instancing.seed = 7u;
    grass.instancing.heightFn = [&tc](float x, float z) { return tc.heightAt(x, z) + 0.3f; };
    scene::MeshEntry& ge = grass.attach<scene::MeshComponent>().addMesh(&blade);
    ge.albedo = {1.f, 1.f, 1.f};
    ge.roughness = 1.f;
    ge.castShadow = false;

    // --- sun + sky/IBL ---
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.93f, 0.80f}, 3.0f);

    fuhgl::Texture skyCube = makeDaySky(256, noise, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "lsystem_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.7f;

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(55.f);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::Ssao);
    scene.root().effects.enable(render::Effect::VolumetricFog);
    scene.root().effects.params.exposure = 1.0f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowCenter = {0.f, 8.f, 0.f};
    scene.root().effects.params.shadowRadius = 100.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 320.f;
    scene.root().effects.params.ssaoRadius = 2.0f;
    scene.root().effects.params.ssaoIntensity = 1.2f;
    scene.root().effects.params.volumetricDensity = 0.005f;
    scene.root().effects.params.volumetricCloudiness = 0.8f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.10f, 0.32f, 0.78f};
    renderer.skyBottom = {0.66f, 0.78f, 0.95f};
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.28f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 48;

    // shot 1: forest overview from a hill, sun through the trees (god rays)
    cam.position = {-38.f, tc.heightAt(-38.f, 46.f) + 15.f, 46.f};
    aimAt(cam, {2.f, tc.heightAt(2.f, -8.f) + 3.f, -8.f});
    const std::string forestPath = args.prefix + "_forest.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, forestPath)) {
        std::fprintf(stderr, "lsystem_screenshot: failed to write %s\n", forestPath.c_str());
        return 1;
    }
    std::fprintf(stderr, "lsystem_screenshot: wrote %s\n", forestPath.c_str());

    // shot 2: closeup of tree0 (variant 0) — camera pulled back and raised to frame the crown
    cam.position = {treeSpots[0].x + 11.f, tc.heightAt(treeSpots[0].x, treeSpots[0].y) + 14.f,
                    treeSpots[0].y + 11.f};
    aimAt(cam, {treeSpots[0].x, tc.heightAt(treeSpots[0].x, treeSpots[0].y) + 13.f, treeSpots[0].y});
    const std::string treePath = args.prefix + "_tree.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, treePath)) {
        std::fprintf(stderr, "lsystem_screenshot: failed to write %s\n", treePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "lsystem_screenshot: wrote %s (GL %d.%d %s)\n", treePath.c_str(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
