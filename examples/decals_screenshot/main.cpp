// G8 decal demo: a plaza (ground = material ID 1, back wall = ID 2, crates = ID 3) with
// bullet-hole decals on the ground and the wall plus one big ground marking, projected
// into the G-buffer by the G3 DeferredRenderer's decal pass (Effect::Decals, material-ID
// gated). Headless to PNG for docs/images/.
//
// Usage: fuhglgraphics_decals_screenshot --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--scale 1.0] [--msaa 0] [--no-decals] [--no-shadows] [--no-rays]
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
    std::string out = "decals.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
    float scale = 1.f;
    bool rays = true, shadows = true, decals = true;
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
        else if (std::strcmp(argv[i], "--no-shadows") == 0) a.shadows = false;
        else if (std::strcmp(argv[i], "--no-decals") == 0) a.decals = false;
    }
    return a;
}

// Builds an RGBA8 texture from a per-pixel lambda (u, v in [0,1] -> rgba).
fuhgl::Texture makeTex(int size, const std::function<void(float, float, unsigned char*)>& px) {
    std::vector<unsigned char> data(static_cast<std::size_t>(size) * size * 4, 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            px(static_cast<float>(x) / size, static_cast<float>(y) / size,
               &data[(static_cast<std::size_t>(y) * size + x) * 4]);
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    // NOTE: setImage2D alone (no texStorage2D — an immutable storage must not be re-allocated)
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, size, size, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 data.data());
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    return t;
}

float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// dark bullet hole with a sooty ring
fuhgl::Texture makeBulletTex() {
    return makeTex(64, [](float u, float v, unsigned char* o) {
        const float dx = u - 0.5f, dy = v - 0.5f;
        const float r = std::sqrt(dx * dx + dy * dy) * 2.f; // 0 center .. 1 edge
        const float hole = clamp01(1.f - r * 2.2f);       // dark center
        const float soot = clamp01(1.6f - r) * 0.5f;       // soot ring
        const float a = clamp01(hole * 0.95f + soot);
        o[0] = 28;
        o[1] = 26;
        o[2] = 24;
        o[3] = static_cast<unsigned char>(a * 255.f);
    });
}

// red ring marking with a center dot
fuhgl::Texture makeMarkTex() {
    return makeTex(256, [](float u, float v, unsigned char* o) {
        const float dx = u - 0.5f, dy = v - 0.5f;
        const float r = std::sqrt(dx * dx + dy * dy) * 2.f;
        const float ring = clamp01(1.f - std::fabs(r - 0.72f) * 14.f);
        const float dot = clamp01(1.f - r * 6.f);
        const float a = clamp01(ring + dot) * 0.9f;
        o[0] = 220;
        o[1] = 30;
        o[2] = 24;
        o[3] = static_cast<unsigned char>(a * 255.f);
    });
}

fuhgl::render::Mesh& addBox(fuhglgraphics::scene::Scene& scene, std::vector<fuhgl::render::Mesh>& meshes,
                           const char* name, const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& half,
                           const fuhgl::math::Vec3& albedo, std::uint32_t materialId) {
    meshes.emplace_back();
    fuhgl::render::Mesh& m = meshes.back();
    m.create();
    m.setMeshData(fuhgl::render::shapes::boxData(half, albedo));
    auto& node = scene.root().createChild(name);
    node.translation = pos;
    auto& e = node.attach<fuhglgraphics::scene::MeshComponent>().addMesh(&m);
    e.albedo = albedo;
    e.roughness = 0.9f;
    e.castShadow = true;
    e.materialId = materialId;
    return m;
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
    if (!window.create("fuhglgraphics_decals_screenshot", 64, 64)) {
        std::fprintf(stderr, "decals_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "decals_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- stage: plaza ground (ID 1), back wall (ID 2), crates (ID 3) ---
    scene::Scene scene;
    std::vector<fuhgl::render::Mesh> meshes;
    meshes.reserve(8);
    addBox(scene, meshes, "ground", {0.f, -0.1f, 0.f}, {15.f, 0.1f, 15.f}, {0.58f, 0.57f, 0.54f}, 1);
    addBox(scene, meshes, "backWall", {0.f, 2.5f, -14.5f}, {15.f, 2.5f, 0.5f}, {0.66f, 0.64f, 0.60f}, 2);
    addBox(scene, meshes, "crate1", {-3.4f, 0.7f, -2.6f}, {0.7f, 0.7f, 0.7f}, {0.62f, 0.47f, 0.28f}, 3);
    addBox(scene, meshes, "crate2", {-2.4f, 0.45f, -1.7f}, {0.45f, 0.45f, 0.45f}, {0.50f, 0.42f, 0.38f}, 3);
    addBox(scene, meshes, "crate3", {3.1f, 0.6f, -4.2f}, {0.6f, 0.6f, 0.6f}, {0.55f, 0.50f, 0.42f}, 3);

    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.5f, -0.8f, 0.35f});
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(sunDir, {1.f, 0.85f, 0.62f}, 3.0f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {7.8f, 4.6f, 9.2f};
    cam.fovY = fuhgl::math::radians(52.f);
    const fuhgl::math::Vec3 target{-1.5f, 0.8f, -3.5f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // --- decals: bullet holes on the ground (mask 1) + wall (mask 2), one big marking ---
    fuhgl::Texture bulletTex = makeBulletTex();
    fuhgl::Texture markTex = makeMarkTex();
    auto addDecal = [&](const char* name, const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& half,
                        const fuhgl::Texture* tex, std::uint32_t mask, float opacity) -> scene::DecalComponent& {
        scene::Node& n = scene.root().createChild(name);
        n.translation = pos;
        scene::DecalComponent& dc = n.attach<scene::DecalComponent>();
        dc.decal.halfExtents = half;
        dc.decal.albedo = {1.f, 1.f, 1.f};
        dc.decal.albedoTexture = tex;
        dc.decal.opacity = opacity;
        dc.decal.materialIdMask = mask;
        return dc;
    };
    // decal boxes must straddle the surface plane (ground top y=0, wall front z=-14)
    const fuhgl::math::Vec3 bulletHalf{0.16f, 0.05f, 0.16f};
    const fuhgl::math::Vec3 wallBulletHalf{0.14f, 0.14f, 0.05f};
    const fuhgl::math::Vec3 spots[8] = {{-2.0f, 0.02f, -1.2f}, {-1.2f, 0.02f, -2.2f}, {-0.4f, 0.02f, -0.8f},
                                        {0.6f, 0.02f, -1.9f},  {1.4f, 0.02f, -0.6f},  {-3.0f, 0.02f, -3.1f},
                                        {2.2f, 0.02f, -2.8f},  {0.1f, 0.02f, -3.4f}};
    for (int i = 0; i < 8; ++i) addDecal("bullet", spots[i], bulletHalf, &bulletTex, 1u, 0.95f);
    addDecal("wallHit1", {-2.2f, 1.6f, -13.98f}, wallBulletHalf, &bulletTex, 2u, 0.9f);
    addDecal("wallHit2", {1.8f, 2.2f, -13.98f}, wallBulletHalf, &bulletTex, 2u, 0.9f);
    addDecal("wallHit3", {0.2f, 0.9f, -13.98f}, wallBulletHalf, &bulletTex, 2u, 0.85f);
    addDecal("marking", {-0.6f, 0.02f, -2.6f}, {1.6f, 0.05f, 1.6f}, &markTex, 1u, 1.f);

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Upscale);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    if (args.decals) scene.root().effects.enable(render::Effect::Decals);
    scene.root().effects.params.shadowCenter = {0.f, 2.f, 0.f};
    scene.root().effects.params.shadowRadius = 18.f;
    scene.root().effects.params.shadowZFar = 60.f;

    // --- sky + renderer ---
    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.66f}, skyHorizon{0.92f, 0.84f, 0.66f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "decals_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "decals_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.setRenderScale(args.scale);
    if (args.shadows) {
        renderer.enableCsmShadows(2048, 4);
        renderer.setShadowSceneBounds({0.f, 2.f, 0.f}, 18.f);
        renderer.setShadowZRange(0.5f, 60.f);
        renderer.setShadowFilter(fuhgl::render::ShadowFilter::Pcss);
        renderer.shadowLightSize = 0.6f;
    }
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;
    renderer.godRays.enabled = args.rays;
    renderer.godRays.strength = 0.22f;
    renderer.godRays.decay = 0.94f;
    renderer.godRays.samples = 48;

    for (int i = 0; i < args.frames; ++i) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        if (!args.shadows) {
            frame.effects.disable(render::Effect::Shadows);
            frame.effects.disable(render::Effect::CsmShadows);
        }
        if (!args.decals) frame.effects.disable(render::Effect::Decals);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.0012f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "decals_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "decals_screenshot: wrote %s (%dx%d, %d frames, decals=%d, GL %d.%d %s)\n",
                 args.out.c_str(), args.width, args.height, args.frames, args.decals ? 1 : 0,
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
