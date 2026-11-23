// G25 marching-cubes screenshot tool: a cave terrain extracted from a G24-noise volume with
// chunked G25 marching cubes (one MeshData per chunk, all chunks as MeshEntries of one node),
// plus a floating emissive metaball cluster (bloom). Sun + CSM shadows + god rays, sky cubemap
// + IBL, SSAO, volumetric fog, bloom, TAA + upscale + ACES. Headless (surfaceless EGL) to PNG
// for docs/images/.
//
// Usage: fuhglgraphics_marching_cubes_screenshot --prefix g25_marching_cubes
//          [--width 1280] [--height 720] [--frames 16] [--scale 1.0]
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
    std::string prefix = "g25_marching_cubes";
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

// dusk sky cubemap (G24 turbulence clouds + warm sun glow)
fuhgl::Texture makeDuskSky(int size, const fuhglgraphics::procedural::Noise& noise,
                           const fuhgl::math::Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.05f, 0.10f, 0.28f}, horizon{0.55f, 0.38f, 0.30f};
    const fuhgl::math::Vec3 ground{0.16f, 0.14f, 0.13f}, cloudTint{0.85f, 0.72f, 0.62f};
    const fuhgl::math::Vec3 sunTint{1.f, 0.55f, 0.25f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float n = noise.turbulence3(dir.x * 3.1f + 4.2f, dir.y * 3.1f, dir.z * 3.1f + 8.8f, 5,
                                                  fuhglgraphics::procedural::Noise::Base::Simplex);
                const float cl = smoothstep01(0.40f, 0.72f, n + 0.15f * dir.y);
                fuhgl::math::Vec3 col =
                    fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                col = fuhgl::math::lerp(col, cloudTint, cl * 0.9f);
                const float sg = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 64.f);
                col += sunTint * sg * 3.0f;
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
        frame.view.fogColor = {0.23f, 0.20f, 0.26f};
        frame.view.fogDensity = 0.0016f;
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
    if (!window.create("fuhglgraphics_marching_cubes_screenshot", 64, 64)) {
        std::fprintf(stderr, "marching_cubes_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "marching_cubes_screenshot: context below GL 3.3\n");
        return 2;
    }

    const procedural::Noise noise(90210u);
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.45f, 0.20f, -0.87f});

    // --- G25 chunked marching cubes: cave terrain from a noise volume ---
    // density < 0 inside: ground height minus y, warped by 3D fBm -> caves + overhangs
    const fuhgl::math::Vec3 vMin{-70.f, -6.f, -70.f}, vMax{70.f, 44.f, 70.f};
    const int VN = 112;
    const procedural::VolumeGrid vol = procedural::sampleField(
        [&](const fuhgl::math::Vec3& p) {
            const float h = 20.f * noise.height2(p.x * 0.018f, p.z * 0.018f, 6) + 4.f;
            const float cave = 7.f * noise.fbm3(p.x * 0.055f, p.y * 0.075f, p.z * 0.055f, 4,
                                                procedural::Noise::Base::Simplex);
            float d = (h - p.y) + cave;
            // keep the field positive near the volume boundary so the iso-surface closes
            // inside the box (otherwise the open boundary reads as see-through holes,
            // because the pipeline culls back faces)
            const float side = std::fmax(std::fabs(p.x), std::fabs(p.z)) - 52.f;
            d += std::fmax(0.f, side) * 3.f;          // >0 beyond |x|,|z| = 52
            d += std::fmax(0.f, p.y - 36.f) * 3.f;    // top
            d += std::fmax(0.f, -4.f - p.y) * 3.f;    // bottom
            return d;
        },
        vMin, vMax, VN, VN, VN);
    std::fprintf(stderr, "marching_cubes_screenshot: sampled %d^3 volume\n", VN);
    const auto chunks = procedural::marchingCubesChunked(vol, 0.f, 4, 1, 4, {1.f, 1.f, 1.f});
    std::size_t totalTris = 0;
    for (const auto& c : chunks) totalTris += c.indices.size() / 3;
    std::fprintf(stderr, "marching_cubes_screenshot: %zu chunks, %zu triangles\n", chunks.size(), totalTris);
    if (chunks.empty()) {
        std::fprintf(stderr, "marching_cubes_screenshot: FAIL (empty volume mesh)\n");
        return 1;
    }

    scene::Scene scene;
    scene::Node& caveNode = scene.root().createChild("cave-terrain");
    scene::MeshComponent& mc = caveNode.attach<scene::MeshComponent>();
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> chunkMeshes; // stable storage
    chunkMeshes.reserve(chunks.size());
    for (const auto& c : chunks) {
        chunkMeshes.push_back(std::make_unique<fuhgl::render::Mesh>());
        chunkMeshes.back()->setMeshData(c);
        scene::MeshEntry& e = mc.addMesh(chunkMeshes.back().get());
        e.albedo = {0.46f, 0.43f, 0.38f};
        e.roughness = 0.95f;
        e.metallic = 0.02f;
        e.materialId = 20;
        e.meshData = &c; // CPU-side copy for picking (G18)
    }

    // --- emissive crystal blobs floating in/above the terrain (bloom picks them up) ---
    auto makeBlob = [&](const fuhgl::math::Vec3& center, float radius, const fuhgl::math::Vec3& emissive) {
        const procedural::VolumeGrid g = procedural::sampleField(
            [&](const fuhgl::math::Vec3& p) {
                const float d = fuhgl::math::length(p - center) - radius;
                return d + 0.22f * radius * noise.fbm3(p.x * 2.1f, p.y * 2.1f, p.z * 2.1f, 4);
            },
            center - fuhgl::math::Vec3{radius * 1.6f, radius * 1.6f, radius * 1.6f},
            center + fuhgl::math::Vec3{radius * 1.6f, radius * 1.6f, radius * 1.6f}, 36, 36, 36);
        auto md = procedural::marchingCubes(g, 0.f, {1.f, 1.f, 1.f});
        auto mesh = std::make_unique<fuhgl::render::Mesh>();
        mesh->setMeshData(md);
        scene::Node& n = scene.root().createChild("crystal");
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(mesh.get());
        e.albedo = {0.12f, 0.10f, 0.16f};
        e.emissive = emissive;
        e.roughness = 0.25f;
        e.metallic = 0.1f;
        e.castShadow = false;
        e.meshData = nullptr;
        return mesh; // keep alive
    };
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> blobs;
    blobs.push_back(makeBlob({0.f, 26.f, -6.f}, 5.5f, {3.2f, 1.5f, 0.5f}));   // amber
    blobs.push_back(makeBlob({-16.f, 20.f, 14.f}, 3.2f, {0.5f, 1.9f, 3.6f}));  // cyan
    blobs.push_back(makeBlob({18.f, 17.f, 10.f}, 2.4f, {3.0f, 0.7f, 2.2f}));   // magenta

    // --- lights + sky/IBL ---
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.55f, 0.32f}, 2.6f);
    // warm fill point light near the big crystal (cave glow)
    scene.root().createChild("glow").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({0.f, 24.f, -6.f}, {1.0f, 0.45f, 0.18f}, 320.f, 60.f);

    fuhgl::Texture skyCube = makeDuskSky(256, noise, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "marching_cubes_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.55f;

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
    scene.root().effects.params.shadowCenter = {0.f, 12.f, 0.f};
    scene.root().effects.params.shadowRadius = 110.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 400.f;
    scene.root().effects.params.ssaoRadius = 2.5f;
    scene.root().effects.params.ssaoIntensity = 1.5f;
    scene.root().effects.params.bloomThreshold = 1.0f;
    scene.root().effects.params.bloomStrength = 1.2f;
    scene.root().effects.params.volumetricDensity = 0.006f;
    scene.root().effects.params.volumetricCloudiness = 0.7f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.05f, 0.10f, 0.28f};
    renderer.skyBottom = {0.55f, 0.38f, 0.30f};
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.30f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 48;

    // shot 1: cave terrain at dusk from the south-east, crystals glowing
    cam.position = {58.f, 34.f, 78.f};
    aimAt(cam, {0.f, 14.f, -4.f});
    const std::string cavePath = args.prefix + "_cave.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, cavePath)) {
        std::fprintf(stderr, "marching_cubes_screenshot: failed to write %s\n", cavePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "marching_cubes_screenshot: wrote %s\n", cavePath.c_str());

    // shot 2: closeup of the amber crystal cluster with bloom + god rays
    cam.position = {14.f, 28.f, 16.f};
    aimAt(cam, {0.f, 25.f, -6.f});
    const std::string blobPath = args.prefix + "_blob.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, blobPath)) {
        std::fprintf(stderr, "marching_cubes_screenshot: failed to write %s\n", blobPath.c_str());
        return 1;
    }
    std::fprintf(stderr, "marching_cubes_screenshot: wrote %s (GL %d.%d %s)\n", blobPath.c_str(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
