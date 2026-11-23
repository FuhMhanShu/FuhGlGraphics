// G29 SDF/fractal screenshot tool: a Mandelbulb (power 8, DE) meshed with meshFromSdf via the
// G25 marching cubes (56^3 samples), scaled up as a floating emissive sculpture + 3 small
// satellite bulbs, over a dark plane; violet point light inside the bulb (bloom), warm low sun
// with CSM shadows + god rays, dusk sky + IBL, SSAO, volumetric fog, TAA + upscale + ACES.
// Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_mandelbulb_screenshot --prefix g29_mandelbulb [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0]
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
    std::string prefix = "g29_mandelbulb";
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

// dusk violet sky cubemap (G24 turbulence clouds + warm low sun glow)
fuhgl::Texture makeDuskSky(int size, const fuhglgraphics::procedural::Noise& noise,
                           const fuhgl::math::Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.10f, 0.07f, 0.30f}, horizon{0.72f, 0.38f, 0.42f};
    const fuhgl::math::Vec3 ground{0.10f, 0.09f, 0.12f}, sunTint{1.f, 0.55f, 0.3f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float n = noise.turbulence3(dir.x * 3.3f + 1.3f, dir.y * 3.3f, dir.z * 3.3f + 7.7f, 5,
                                                  fuhglgraphics::procedural::Noise::Base::Simplex);
                const float cl = smoothstep01(0.42f, 0.74f, n + 0.14f * dir.y);
                fuhgl::math::Vec3 col =
                    fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                col = fuhgl::math::lerp(col, {0.8f, 0.6f, 0.7f}, cl * 0.85f);
                const float sg = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 56.f);
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
        frame.view.fogColor = {0.16f, 0.10f, 0.22f};
        frame.view.fogDensity = 0.0018f;
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
    if (!window.create("fuhglgraphics_mandelbulb_screenshot", 64, 64)) {
        std::fprintf(stderr, "mandelbulb_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "mandelbulb_screenshot: context below GL 3.3\n");
        return 2;
    }

    const procedural::Noise noise(86420u);
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.5f, 0.18f, -0.82f});

    // --- G29: Mandelbulb meshed via G25 marching cubes ---
    const auto bulbField = [](const fuhgl::math::Vec3& p) {
        return fuhglgraphics::procedural::sdf::mandelbulb(p, 8.f, 10);
    };
    const fuhgl::render::MeshData mainData =
        procedural::meshFromSdf(bulbField, {-1.4f, -1.4f, -1.4f}, {1.4f, 1.4f, 1.4f}, 56);
    std::fprintf(stderr, "mandelbulb_screenshot: main bulb %zu tris\n", mainData.indices.size() / 3);
    if (mainData.indices.empty()) {
        std::fprintf(stderr, "mandelbulb_screenshot: FAIL (empty fractal mesh)\n");
        return 1;
    }

    scene::Scene scene;

    // dark ground
    fuhgl::render::Mesh groundMesh;
    fuhgl::render::shapes::quad(groundMesh, 70.f, {0.09f, 0.08f, 0.11f});
    scene::Node& gn = scene.root().createChild("ground");
    gn.translation = {0.f, -0.5f, 0.f};
    gn.attach<scene::MeshComponent>().addMesh(&groundMesh).albedo = {0.09f, 0.08f, 0.11f};

    // main bulb, scaled x5, floating
    fuhgl::render::Mesh mainMesh;
    mainMesh.setMeshData(mainData);
    scene::Node& bn = scene.root().createChild("mandelbulb");
    bn.translation = {0.f, 5.2f, 0.f};
    bn.scale = {5.f, 5.f, 5.f};
    {
        scene::MeshEntry& e = bn.attach<scene::MeshComponent>().addMesh(&mainMesh);
        e.albedo = {0.16f, 0.08f, 0.30f};
        e.emissive = {1.6f, 0.55f, 2.6f};
        e.metallic = 0.3f;
        e.roughness = 0.3f;
    }
    // violet core light inside the bulb
    scene.root().createChild("core-light").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({0.f, 5.2f, 0.f}, {0.55f, 0.25f, 1.0f}, 500.f, 45.f);

    // 3 satellite mini-bulbs on an orbit
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> sats;
    const fuhgl::math::Vec3 satPos[3] = {{9.5f, 3.2f, 2.f}, {-7.5f, 6.5f, -5.f}, {1.5f, 8.8f, -8.5f}};
    for (int i = 0; i < 3; ++i) {
        const auto field = [&](const fuhgl::math::Vec3& p) {
            return fuhglgraphics::procedural::sdf::mandelbulb(p, 8.f, 9);
        };
        auto md = procedural::meshFromSdf(field, {-1.2f, -1.2f, -1.2f}, {1.2f, 1.2f, 1.2f}, 30);
        sats.push_back(std::make_unique<fuhgl::render::Mesh>());
        sats.back()->setMeshData(md);
        scene::Node& n = scene.root().createChild("satellite" + std::to_string(i));
        n.translation = satPos[i];
        const float s = 1.4f + 0.3f * i;
        n.scale = {s, s, s};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(sats.back().get());
        e.albedo = {0.05f, 0.16f, 0.22f};
        e.emissive = {0.3f, 1.1f, 1.5f};
        e.roughness = 0.35f;
    }

    // warm low sun for shape/shadows/god rays
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.5f, 0.28f}, 2.2f);

    fuhgl::Texture skyCube = makeDuskSky(256, noise, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "mandelbulb_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.5f;

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
    scene.root().effects.params.shadowCenter = {0.f, 5.f, 0.f};
    scene.root().effects.params.shadowRadius = 30.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 120.f;
    scene.root().effects.params.ssaoRadius = 2.0f;
    scene.root().effects.params.ssaoIntensity = 1.3f;
    scene.root().effects.params.bloomThreshold = 1.0f;
    scene.root().effects.params.bloomStrength = 1.0f;
    scene.root().effects.params.volumetricDensity = 0.007f;
    scene.root().effects.params.volumetricCloudiness = 0.6f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.10f, 0.07f, 0.30f};
    renderer.skyBottom = {0.72f, 0.38f, 0.42f};
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.30f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 48;

    // shot 1: wide — floating mandelbulb with satellites, god rays, fog
    cam.position = {16.f, 9.f, 20.f};
    aimAt(cam, {0.f, 5.f, 0.f});
    const std::string widePath = args.prefix + "_wide.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, widePath)) {
        std::fprintf(stderr, "mandelbulb_screenshot: failed to write %s\n", widePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "mandelbulb_screenshot: wrote %s\n", widePath.c_str());

    // shot 2: closeup of the fractal surface (bloom + detail)
    cam.position = {7.5f, 6.5f, 8.5f};
    aimAt(cam, {0.f, 5.2f, 0.f});
    const std::string closePath = args.prefix + "_closeup.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, closePath)) {
        std::fprintf(stderr, "mandelbulb_screenshot: failed to write %s\n", closePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "mandelbulb_screenshot: wrote %s (GL %d.%d %s)\n", closePath.c_str(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
