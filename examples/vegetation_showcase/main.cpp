// G37 vegetation-showcase screenshot tool: six ecophysiological plants (G37
// VegetationGenerator — Space Colonization + climate-to-geometry rules + Da Vinci pipe model
// + baked leaf/bark tints) grown from different EcoEnvironments and standing side by side on
// a G24-noise terrain: open-meadow broad crown, dense-forest columnar pruned trunk, tropical
// drip-tip tree, desert succulent/thorns, wind-flagged coastal tree, autumn-colored tree.
// Leaves render through a separate MeshEntry with `subsurface` set, so the G33 SSS pass
// backlights the thin leaves. Noon sun with CSM shadows, sky cubemap + IBL, SSAO, TAA + ACES.
// Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_vegetation_showcase --prefix g37_vegetation [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhglgraphics/procedural/noise.hpp>
#include <fuhglgraphics/procedural/vegetation.hpp>

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

using fuhgl::math::Vec3;

struct Args {
    std::string prefix = "g37_vegetation";
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

void aimAt(fuhgl::FirstPersonCamera& cam, const Vec3& target) {
    const Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

float smoothstep01(float e0, float e1, float x) {
    const float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
    return t * t * (3.f - 2.f * t);
}

Vec3 cubeDir(int face, int i, int j, int size) {
    const float u = 2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(size) - 1.f;
    const float v = 2.f * (static_cast<float>(j) + 0.5f) / static_cast<float>(size) - 1.f;
    Vec3 d;
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
                          const Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const Vec3 zenith{0.10f, 0.32f, 0.78f}, horizon{0.66f, 0.78f, 0.95f};
    const Vec3 ground{0.42f, 0.44f, 0.40f}, sunTint{1.f, 0.95f, 0.8f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const Vec3 dir = cubeDir(face, i, j, size);
                const float n = noise.turbulence3(dir.x * 3.4f + 2.2f, dir.y * 3.4f, dir.z * 3.4f + 6.1f, 5,
                                                  fuhglgraphics::procedural::Noise::Base::Simplex);
                const float cl = smoothstep01(0.42f, 0.75f, n + 0.12f * dir.y);
                Vec3 col = fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
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
                        frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface);
    }
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    fuhgl::gl::readPixels(0, 0, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    return fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), width, height);
}

// one G37 plant: branch mesh + leaf mesh as two entries of one node (leaves get the G33 SSS
// subsurface factor from the material)
struct PlantActor {
    fuhgl::render::Mesh branches, leaves;
    float height = 0.f;
};

PlantActor buildPlant(const fuhglgraphics::procedural::EcoEnvironment& eco, unsigned seed,
                      float scale, const char* name) {
    fuhglgraphics::procedural::VegetationGenerator gen;
    fuhglgraphics::procedural::VegetationParams p =
        fuhglgraphics::procedural::VegetationGenerator::randomParams(eco, seed);
    p.scale = scale;
    const fuhglgraphics::procedural::VegetationMesh vm = gen.generate(p);
    PlantActor a;
    a.height = vm.height;
    a.branches.setMeshData(vm.branches);
    a.leaves.setMeshData(vm.leaves);
    std::fprintf(stderr, "vegetation_showcase: %s h=%.2f branches=%zu tris leaves=%zu tris leaf=%d\n",
                 name, vm.height, vm.branches.indices.size() / 3, vm.leaves.indices.size() / 3,
                 static_cast<int>(vm.form.leaf));
    return a;
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
    if (!window.create("fuhglgraphics_vegetation_showcase", 64, 64)) {
        std::fprintf(stderr, "vegetation_showcase: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "vegetation_showcase: context below GL 3.3\n");
        return 2;
    }

    const procedural::Noise noise(424242u);
    // low sun from -Z, slightly off-axis: the crowns are backlit -> G33 SSS glows through leaves
    const Vec3 sunDir = fuhgl::math::normalize(Vec3{0.30f, 0.42f, -0.85f});

    // --- terrain (G24 noise heightmap) ---
    scene::Scene scene;
    scene::Node& tn = scene.root().createChild("terrain");
    scene::TerrainComponent& tc = tn.attach<scene::TerrainComponent>();
    tc.levels = 4;
    tc.fovYDeg = 55.f;
    tc.albedo = {0.30f, 0.36f, 0.20f}; // grass
    tc.customDistances = {60.f, 150.f, 300.f};
    if (!tc.create(160, 160, 160.f, 160.f, 10.f)) {
        std::fprintf(stderr, "vegetation_showcase: terrain create failed\n");
        return 1;
    }
    for (int j = 0; j < tc.gridH; ++j)
        for (int i = 0; i < tc.gridW; ++i) {
            const float x = (static_cast<float>(i) / (tc.gridW - 1) - 0.5f) * tc.sizeX;
            const float z = (static_cast<float>(j) / (tc.gridH - 1) - 0.5f) * tc.sizeZ;
            float h = noise.height2(x * 0.02f, z * 0.02f, 5) * 0.35f;
            h *= smoothstep01(75.f, 40.f, std::hypot(x, z));
            tc.heights[static_cast<std::size_t>(j) * tc.gridW + i] = h;
        }
    tc.rebuildLod();

    // --- six plants from six EcoEnvironments (same params as the G31 terrain climate maps) ---
    struct Slot {
        const char* name;
        procedural::EcoEnvironment eco;
        float x;
    };
    const float windA = 0.6f; // prevailing wind from -X... windDir is where the wind blows to
    const Slot slots[6] = {
        {"meadow_oak", {0.80f, 0.50f, 0.0f, {0.f, 0.f, 0.f}, 0.00f, 0.f, 101u}, -30.f}, // open: broad crown
        {"forest_column", {0.80f, 0.55f, 0.1f, {std::cos(windA), 0.f, std::sin(windA)}, 0.95f, 0.f, 102u}, -18.f}, // dense: tall, pruned
        {"tropical_driptip", {0.95f, 0.95f, 0.0f, {0.f, 0.f, 0.f}, 0.20f, 0.f, 103u}, -6.f}, // wet: drip tips
        {"desert_succulent", {0.90f, 0.06f, 0.2f, {1.f, 0.f, 0.f}, 0.00f, 0.f, 104u}, 6.f}, // dry: thorns/succulent
        {"coastal_flagged", {0.55f, 0.45f, 1.0f, {1.f, 0.f, 0.f}, 0.10f, 0.f, 105u}, 18.f}, // wind: leaning
        {"autumn_beech", {0.60f, 0.55f, 0.0f, {0.f, 0.f, 0.f}, 0.30f, 1.f, 106u}, 30.f}, // season: red-brown
    };
    std::vector<std::unique_ptr<PlantActor>> plants;
    for (int i = 0; i < 6; ++i) {
        plants.push_back(std::make_unique<PlantActor>(
            buildPlant(slots[i].eco, slots[i].eco.seed, 1.0f, slots[i].name)));
        scene::Node& n = scene.root().createChild(std::string("plant_") + slots[i].name);
        n.translation = {slots[i].x, tc.heightAt(slots[i].x, 0.f), 0.f};
        scene::MeshEntry& be = n.attach<scene::MeshComponent>().addMesh(&plants.back()->branches);
        be.albedo = {1.f, 1.f, 1.f}; // vertex colors carry the baked bark tint
        be.roughness = 0.9f;
        be.subsurface = 0.f;
        scene::MeshEntry& le = n.attach<scene::MeshComponent>().addMesh(&plants.back()->leaves);
        le.albedo = {1.f, 1.f, 1.f};
        le.roughness = 0.75f;
        le.subsurface = 0.7f; // G33: backlit thin leaves glow (thickness rides in pbr.a)
        le.castShadow = true;
    }

    // --- sun + sky/IBL ---
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.93f, 0.80f}, 3.2f);

    fuhgl::Texture skyCube = makeDaySky(256, noise, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "vegetation_showcase: IBL bake failed, continuing without env\n");
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
    scene.root().effects.enable(render::Effect::Subsurface); // G33 backlighting through the leaves
    scene.root().effects.params.exposure = 1.0f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowCenter = {0.f, 6.f, 0.f};
    scene.root().effects.params.shadowRadius = 90.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 260.f;
    scene.root().effects.params.ssaoRadius = 2.0f;
    scene.root().effects.params.ssaoIntensity = 1.0f;
    scene.root().effects.params.sssDistortion = 0.5f;
    scene.root().effects.params.sssPower = 2.5f;
    scene.root().effects.params.sssScale = 2.0f;
    scene.root().effects.params.sssThickness = 0.6f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.10f, 0.32f, 0.78f};
    renderer.skyBottom = {0.66f, 0.78f, 0.95f};

    // shot 1: overview of the whole row (meadow oak .. autumn beech)
    cam.position = {0.f, tc.heightAt(0.f, 26.f) + 9.f, 26.f};
    aimAt(cam, {0.f, tc.heightAt(0.f, 0.f) + 4.f, 0.f});
    const std::string widePath = args.prefix + "_wide.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, widePath)) {
        std::fprintf(stderr, "vegetation_showcase: failed to write %s\n", widePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "vegetation_showcase: wrote %s\n", widePath.c_str());

    // shot 2: closeup of the tropical drip-tip tree, sun behind it (SSS through the big leaves)
    const float tx = slots[2].x;
    cam.position = {tx + 7.5f, tc.heightAt(tx, 0.f) + 4.5f, 7.5f};
    aimAt(cam, {tx, tc.heightAt(tx, 0.f) + plants[2]->height * 0.62f, 0.f});
    const std::string closePath = args.prefix + "_driptip_closeup.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, closePath)) {
        std::fprintf(stderr, "vegetation_showcase: failed to write %s\n", closePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "vegetation_showcase: wrote %s (GL %d.%d %s)\n", closePath.c_str(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
