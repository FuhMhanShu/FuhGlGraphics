// G28 Voronoi screenshot tool: a shattered plaza — G28 voronoi cells extruded as stone plates
// with gaps over an emissive underlay (glowing cracks + bloom), plus a crystal-shard cluster
// (per-cell height jitter). Sun + CSM shadows, sky + IBL, SSAO, bloom, volumetric fog,
// TAA + upscale + ACES. Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_voronoi_screenshot --prefix g28_voronoi [--width 1280] [--height 720]
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
#include <string>
#include <vector>

namespace {

struct Args {
    std::string prefix = "g28_voronoi";
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

// sunset sky cubemap (G24 turbulence clouds + warm sun glow)
fuhgl::Texture makeSunsetSky(int size, const fuhglgraphics::procedural::Noise& noise,
                             const fuhgl::math::Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.08f, 0.16f, 0.42f}, horizon{0.95f, 0.55f, 0.28f};
    const fuhgl::math::Vec3 ground{0.20f, 0.16f, 0.14f}, sunTint{1.f, 0.5f, 0.2f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float n = noise.turbulence3(dir.x * 3.0f + 9.1f, dir.y * 3.0f, dir.z * 3.0f + 1.7f, 5,
                                                  fuhglgraphics::procedural::Noise::Base::Simplex);
                const float cl = smoothstep01(0.40f, 0.72f, n + 0.18f * dir.y);
                fuhgl::math::Vec3 col =
                    fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                col = fuhgl::math::lerp(col, {0.95f, 0.75f, 0.6f}, cl * 0.85f);
                const float sg = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 48.f);
                col += sunTint * sg * 3.5f;
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
        frame.view.fogColor = {0.55f, 0.38f, 0.30f};
        frame.view.fogDensity = 0.0015f;
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
    if (!window.create("fuhglgraphics_voronoi_screenshot", 64, 64)) {
        std::fprintf(stderr, "voronoi_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "voronoi_screenshot: context below GL 3.3\n");
        return 2;
    }

    const procedural::Noise noise(13579u);
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.55f, 0.22f, -0.80f});

    scene::Scene scene;

    // --- dark ground plane under everything ---
    fuhgl::render::Mesh groundMesh;
    fuhgl::render::shapes::quad(groundMesh, 60.f, {0.16f, 0.15f, 0.14f});
    scene::Node& gn = scene.root().createChild("ground");
    gn.translation = {0.f, -0.35f, 0.f};
    {
        scene::MeshEntry& e = gn.attach<scene::MeshComponent>().addMesh(&groundMesh);
        e.albedo = {0.16f, 0.15f, 0.14f};
        e.roughness = 1.f;
    }

    // --- emissive underlay: glows through the cracks between the plates ---
    fuhgl::render::Mesh glowMesh;
    fuhgl::render::shapes::quad(glowMesh, 59.f, {1, 1, 1});
    scene::Node& glow = scene.root().createChild("crack-glow");
    glow.translation = {0.f, -0.18f, 0.f};
    {
        scene::MeshEntry& e = glow.attach<scene::MeshComponent>().addMesh(&glowMesh);
        e.emissive = {2.6f, 0.9f, 0.22f};
        e.albedo = {0.05f, 0.02f, 0.01f};
        e.castShadow = false;
    }

    // --- G28: shattered plaza plates (voronoi cells extruded with gaps) ---
    const procedural::Voronoi2D plaza = procedural::voronoi2d(70, {-30.f, -30.f}, {30.f, 30.f}, 2468u);
    const fuhgl::render::MeshData plates =
        procedural::extrudeCells(plaza, -0.15f, 0.30f, 0.055f, 0.12f, 11u, {0.52f, 0.50f, 0.46f}, 0.18f);
    std::fprintf(stderr, "voronoi_screenshot: plaza %zu cells -> %zu tris\n", plaza.cells.size(),
                 plates.indices.size() / 3);
    fuhgl::render::Mesh platesMesh;
    platesMesh.setMeshData(plates);
    scene::Node& pn = scene.root().createChild("plaza");
    {
        scene::MeshEntry& e = pn.attach<scene::MeshComponent>().addMesh(&platesMesh);
        e.albedo = {1.f, 1.f, 1.f};
        e.roughness = 0.85f;
        e.metallic = 0.05f;
    }

    // --- crystal-shard cluster (voronoi cells with strong height jitter) ---
    const fuhgl::math::Vec2 crystalCenter{16.f, 12.f};
    const procedural::Voronoi2D crystals =
        procedural::voronoi2d(16, {crystalCenter.x - 5.f, crystalCenter.y - 5.f},
                             {crystalCenter.x + 5.f, crystalCenter.y + 5.f}, 1357u);
    const fuhgl::render::MeshData shards =
        procedural::extrudeCells(crystals, 0.25f, 2.2f, 0.10f, 4.2f, 23u, {0.35f, 0.55f, 0.75f}, 0.25f);
    fuhgl::render::Mesh shardsMesh;
    shardsMesh.setMeshData(shards);
    scene::Node& cn = scene.root().createChild("crystals");
    {
        scene::MeshEntry& e = cn.attach<scene::MeshComponent>().addMesh(&shardsMesh);
        e.albedo = {1.f, 1.f, 1.f};
        e.metallic = 0.85f;
        e.roughness = 0.18f;
    }
    scene.root().createChild("crystal-light").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({crystalCenter.x, 5.f, crystalCenter.y}, {0.45f, 0.75f, 1.0f}, 90.f, 22.f);

    // --- sun + sky/IBL ---
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.55f, 0.30f}, 2.8f);

    fuhgl::Texture skyCube = makeSunsetSky(256, noise, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "voronoi_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.6f;

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(55.f);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ibl);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.enable(render::Effect::Ssao);
    scene.root().effects.enable(render::Effect::Bloom);
    scene.root().effects.enable(render::Effect::VolumetricFog);
    scene.root().effects.params.exposure = 1.0f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowCenter = {0.f, 2.f, 0.f};
    scene.root().effects.params.shadowRadius = 45.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 160.f;
    scene.root().effects.params.ssaoRadius = 1.8f;
    scene.root().effects.params.ssaoIntensity = 1.4f;
    scene.root().effects.params.bloomThreshold = 1.0f;
    scene.root().effects.params.bloomStrength = 1.1f;
    scene.root().effects.params.volumetricDensity = 0.005f;
    scene.root().effects.params.volumetricCloudiness = 0.7f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.08f, 0.16f, 0.42f};
    renderer.skyBottom = {0.95f, 0.55f, 0.28f};
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.25f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 48;

    // shot 1: wide over the shattered plaza, glowing cracks, low sun
    cam.position = {-26.f, 14.f, 34.f};
    aimAt(cam, {4.f, 0.f, -2.f});
    const std::string widePath = args.prefix + "_shattered.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, widePath)) {
        std::fprintf(stderr, "voronoi_screenshot: failed to write %s\n", widePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "voronoi_screenshot: wrote %s\n", widePath.c_str());

    // shot 2: crystal cluster closeup (point light + bloom)
    cam.position = {crystalCenter.x + 8.f, 3.5f, crystalCenter.y + 9.f};
    aimAt(cam, {crystalCenter.x, 2.2f, crystalCenter.y});
    const std::string cryPath = args.prefix + "_crystals.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, cryPath)) {
        std::fprintf(stderr, "voronoi_screenshot: failed to write %s\n", cryPath.c_str());
        return 1;
    }
    std::fprintf(stderr, "voronoi_screenshot: wrote %s (GL %d.%d %s)\n", cryPath.c_str(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
