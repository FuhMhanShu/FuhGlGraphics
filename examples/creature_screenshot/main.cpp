// G30 procedural-creature screenshot tool: a quadruped and a reptile built from a skeleton
// graph -> SDF (capsules/ellipsoids + smooth-min + simplex detail) -> G25 marching cubes with
// curvature vertex colors, plus baked albedo/normal maps from object-space 3D noise. On a
// G24-noise terrain, dusk sun with CSM shadows + god rays, sky cubemap + IBL, SSAO, bloom,
// volumetric fog, TAA + upscale + ACES. Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_creature_screenshot --prefix g30_creature [--width 1280] [--height 720]
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
    std::string prefix = "g30_creature";
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
    const fuhgl::math::Vec3 zenith{0.08f, 0.16f, 0.42f}, horizon{0.95f, 0.55f, 0.28f};
    const fuhgl::math::Vec3 ground{0.30f, 0.24f, 0.22f}, sunTint{1.f, 0.75f, 0.45f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const fuhgl::math::Vec3 dir = cubeDir(face, i, j, size);
                const float n = noise.turbulence3(dir.x * 3.4f + 2.2f, dir.y * 3.4f, dir.z * 3.4f + 6.1f, 5,
                                                  fuhglgraphics::procedural::Noise::Base::Simplex);
                const float cl = smoothstep01(0.40f, 0.72f, n + 0.10f * dir.y);
                fuhgl::math::Vec3 col =
                    fuhgl::math::lerp(horizon, zenith, std::min(1.f, std::max(0.f, dir.y * 0.5f + 0.5f)));
                if (dir.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -dir.y * 3.f));
                col = fuhgl::math::lerp(col, {1.f, 0.72f, 0.45f}, cl * 0.85f);
                const float sg = std::pow(std::max(fuhgl::math::dot(dir, sunDir), 0.f), 96.f);
                col += sunTint * sg * 3.2f;
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

// uploads a baked RGBA8 map; wrapS = REPEAT so the cylindrical u unwrap is seamless
fuhgl::Texture uploadMap(int size, const std::vector<unsigned char>& data, bool repeatU) {
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, size, size, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 data.data());
    t.generateMipmap(); // required for the LINEAR_MIPMAP_LINEAR min filter below
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR_MIPMAP_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S,
                    repeatU ? fuhgl::gl::GL_REPEAT : fuhgl::gl::GL_CLAMP_TO_EDGE);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    return t;
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
        frame.view.fogColor = {0.72f, 0.55f, 0.42f};
        frame.view.fogDensity = 0.0005f;
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

// one procedural creature: skeleton -> SDF -> mesh (curvature vertex colors) + baked maps
struct CreatureActor {
    fuhgl::render::Mesh mesh;
    fuhgl::Texture albedoTex, normalTex;
    fuhgl::render::MeshData meshData;
    fuhgl::math::Vec3 origin{0.f, 0.f, 0.f}; // skeleton-space head position (for the closeup cam)
};

CreatureActor buildCreature(const fuhglgraphics::procedural::Skeleton& skel,
                            const fuhglgraphics::procedural::CreatureParams& pr, int meshRes,
                            int mapSize) {
    CreatureActor a;
    const fuhglgraphics::procedural::CreatureMesh cm =
        fuhglgraphics::procedural::meshCreature(skel, pr, meshRes, {1.f, 1.f, 1.f}, /*curvatureGain=*/3.f);
    a.meshData = cm.mesh;
    a.mesh.setMeshData(a.meshData);
    const fuhglgraphics::procedural::CreatureMaps maps =
        fuhglgraphics::procedural::bakeCreatureMaps(skel, pr, mapSize);
    a.albedoTex = uploadMap(maps.size, maps.albedo, true);
    a.normalTex = uploadMap(maps.size, maps.normal, true);
    for (int i = 0; i < static_cast<int>(skel.nodes.size()); ++i)
        if (skel.nodes[i].name == "head") a.origin = skel.worldPos(i);
    std::fprintf(stderr, "creature_screenshot: mesh %zu tris, maps %dx%d\n", cm.mesh.indices.size() / 3,
                 maps.size, maps.size);
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
    if (!window.create("fuhglgraphics_creature_screenshot", 64, 64)) {
        std::fprintf(stderr, "creature_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "creature_screenshot: context below GL 3.3\n");
        return 2;
    }

    const procedural::Noise noise(424242u);
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.55f, 0.38f, -0.72f});

    // --- terrain (G24 noise heightmap) ---
    scene::Scene scene;
    scene::Node& tn = scene.root().createChild("terrain");
    scene::TerrainComponent& tc = tn.attach<scene::TerrainComponent>();
    tc.levels = 4;
    tc.fovYDeg = 55.f;
    tc.albedo = {0.32f, 0.30f, 0.22f}; // dry savanna-ish
    tc.customDistances = {60.f, 140.f, 280.f};
    if (!tc.create(160, 160, 200.f, 200.f, 14.f)) {
        std::fprintf(stderr, "creature_screenshot: terrain create failed\n");
        return 1;
    }
    for (int j = 0; j < tc.gridH; ++j)
        for (int i = 0; i < tc.gridW; ++i) {
            const float x = (static_cast<float>(i) / (tc.gridW - 1) - 0.5f) * tc.sizeX;
            const float z = (static_cast<float>(j) / (tc.gridH - 1) - 0.5f) * tc.sizeZ;
            float h = noise.height2(x * 0.02f, z * 0.02f, 5);
            h *= smoothstep01(120.f, 60.f, std::hypot(x, z));
            tc.heights[static_cast<std::size_t>(j) * tc.gridW + i] = h;
        }
    tc.rebuildLod();

    // --- G30 creatures: quadruped + reptile with baked procedural maps ---
    procedural::CreatureParams prQ;
    prQ.seed = 21u;
    prQ.scaleFreq = 6.f;    // readable scale cells                    // big readable scales
    prQ.stripeFreq = 0.8f;                   // wide stripes
    prQ.detailAmp = 0.010f;                  // fine, subtle skin folds (not lumpy)
    prQ.detailFreq = 10.f;
    prQ.bumpStrength = 1.0f;                 // gentle normal-map relief
    prQ.bodyColor = {0.48f, 0.38f, 0.24f};  // tawny
    prQ.bellyColor = {0.78f, 0.70f, 0.55f};
    prQ.accentColor = {0.30f, 0.16f, 0.10f}; // dark stripes
    prQ.roughness = 0.5f;
    const procedural::Skeleton quadruped = procedural::makeQuadruped();
    CreatureActor quad = buildCreature(quadruped, prQ, 80, 256);

    procedural::CreatureParams prR;
    prR.seed = 77u;
    prR.bodyColor = {0.22f, 0.38f, 0.28f};  // green
    prR.bellyColor = {0.55f, 0.62f, 0.38f};
    prR.accentColor = {0.10f, 0.22f, 0.16f};
    prR.scaleFreq = 8.f;                    // smaller scales
    prR.detailAmp = 0.014f;                 // fine, subtle skin folds
    prR.detailFreq = 11.f;
    prR.bumpStrength = 1.0f;
    prR.roughness = 0.35f;                  // wetter skin
    const procedural::Skeleton reptile = procedural::makeReptile();
    CreatureActor rep = buildCreature(reptile, prR, 80, 256);

    const fuhgl::math::Vec2 quadSpot{-5.f, 3.f}, repSpot{6.f, -5.f};
    const float quadYaw = fuhgl::math::radians(35.f), repYaw = fuhgl::math::radians(-120.f);
    {
        scene::Node& n = scene.root().createChild("quadruped");
        n.translation = {quadSpot.x, tc.heightAt(quadSpot.x, quadSpot.y), quadSpot.y};
        n.rotation = fuhgl::math::fromAxisAngle(quadYaw, {0.f, 1.f, 0.f});
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&quad.mesh);
        e.albedo = {1.f, 1.f, 1.f}; // vertex color (curvature) * baked albedo texture
        e.albedoTexture = &quad.albedoTex;
        e.normalMap = &quad.normalTex;
        e.roughness = prQ.roughness;
        e.metallic = 0.05f;
    }
    {
        scene::Node& n = scene.root().createChild("reptile");
        n.translation = {repSpot.x, tc.heightAt(repSpot.x, repSpot.y), repSpot.y};
        n.rotation = fuhgl::math::fromAxisAngle(repYaw, {0.f, 1.f, 0.f});
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&rep.mesh);
        e.albedo = {1.f, 1.f, 1.f};
        e.albedoTexture = &rep.albedoTex;
        e.normalMap = &rep.normalTex;
        e.roughness = prR.roughness;
        e.metallic = 0.1f;
    }

    // --- glowing eyes (small emissive spheres via G29 SDF) ---
    auto addEyes = [&](const CreatureActor& a, const fuhgl::math::Vec2& spot, float yaw,
                       const fuhgl::math::Vec3& headPos) {
        for (int s = -1; s <= 1; s += 2) {
            const fuhgl::render::MeshData eyeMd = procedural::meshFromSdf(
                [](const fuhgl::math::Vec3& p) { return procedural::sdf::sphere(p, 0.06f); },
                {-0.06f, -0.06f, -0.06f}, {0.06f, 0.06f, 0.06f}, 12, {1.f, 1.f, 1.f});
            fuhgl::render::Mesh* eye = new fuhgl::render::Mesh();
            eye->setMeshData(eyeMd);
            const fuhgl::math::Vec3 local = headPos + fuhgl::math::Vec3{0.10f * s, 0.04f, 0.16f};
            const fuhgl::math::Mat4 rot = fuhgl::math::rotateY(yaw);
            const fuhgl::math::Vec3 world = fuhgl::math::Vec3{spot.x, 0.f, spot.y} +
                                            fuhgl::math::Vec3{rot(0, 0) * local.x + rot(2, 0) * local.z,
                                                             local.y + tc.heightAt(spot.x, spot.y),
                                                             rot(0, 2) * local.x + rot(2, 2) * local.z};
            scene::Node& en = scene.root().createChild("eye");
            en.translation = world;
            scene::MeshEntry& ee = en.attach<scene::MeshComponent>().addMesh(eye);
            ee.emissive = {0.5f, 0.25f, 0.08f};  // dim amber (below bloom threshold)
            ee.albedo = {0.02f, 0.015f, 0.01f};
            ee.roughness = 0.15f; // glossy dark eyes
            ee.castShadow = false;
        }
    };
    addEyes(quad, quadSpot, quadYaw, quad.origin);
    addEyes(rep, repSpot, repYaw, rep.origin);

    // --- sun + sky/IBL ---
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.72f, 0.48f}, 3.2f);
    scene.root().createChild("fill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.5f, -0.3f, 0.6f}, {0.35f, 0.45f, 0.65f}, 0.8f);

    fuhgl::Texture skyCube = makeDuskSky(256, noise, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "creature_screenshot: IBL bake failed, continuing without env\n");
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
    scene.root().effects.params.exposure = 1.05f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.shadowCenter = {0.f, 2.f, 0.f};
    scene.root().effects.params.shadowRadius = 40.f;
    scene.root().effects.params.shadowZNear = 0.5f;
    scene.root().effects.params.shadowZFar = 160.f;
    scene.root().effects.params.ssaoRadius = 1.2f;
    scene.root().effects.params.ssaoIntensity = 1.5f;
    scene.root().effects.params.bloomThreshold = 1.0f;
    scene.root().effects.params.bloomStrength = 0.55f;
    scene.root().effects.params.volumetricDensity = 0.0015f;
    scene.root().effects.params.volumetricCloudiness = 0.7f;

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    renderer.enableCsmShadows(1024, 4);
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.08f, 0.16f, 0.42f};
    renderer.skyBottom = {0.95f, 0.55f, 0.28f};
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.30f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 48;

    // shot 1: wide — both creatures on the plain, low dusk sun (god rays + long shadows)
    cam.position = {10.f, tc.heightAt(10.f, 14.f) + 4.5f, 14.f};
    aimAt(cam, {0.5f, tc.heightAt(0.5f, -1.f) + 1.2f, -1.f});
    const std::string widePath = args.prefix + "_wide.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, widePath)) {
        std::fprintf(stderr, "creature_screenshot: failed to write %s\n", widePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "creature_screenshot: wrote %s\n", widePath.c_str());

    // shot 2: closeup of the quadruped flank — scales texture + curvature shading in the folds
    const fuhgl::math::Mat4 rot = fuhgl::math::rotateY(quadYaw);
    const fuhgl::math::Vec3 torsoWorld =
        fuhgl::math::Vec3{quadSpot.x, tc.heightAt(quadSpot.x, quadSpot.y), quadSpot.y} +
        fuhgl::math::Vec3{rot(0, 0) * 0.f + rot(2, 0) * 0.f, 1.0f, rot(0, 2) * 0.f + rot(2, 2) * 0.f};
    // camera offset and aim in the creature's local frame (rotated by quadYaw): flank + head
    const fuhgl::math::Vec3 camOffLocal{2.4f, 1.2f, 2.8f};
    const fuhgl::math::Vec3 aimLocal{0.f, 1.1f, 0.75f};
    const auto rotv = [&](const fuhgl::math::Vec3& v) {
        return fuhgl::math::Vec3{rot(0, 0) * v.x + rot(2, 0) * v.z, v.y, rot(0, 2) * v.x + rot(2, 2) * v.z};
    };
    cam.position = torsoWorld + rotv(camOffLocal);
    aimAt(cam, torsoWorld + rotv(aimLocal));
    const std::string closePath = args.prefix + "_closeup.png";
    if (!renderToPng(scene, cam, renderer, &env, args.frames, args.width, args.height, closePath)) {
        std::fprintf(stderr, "creature_screenshot: failed to write %s\n", closePath.c_str());
        return 1;
    }
    std::fprintf(stderr, "creature_screenshot: wrote %s (GL %d.%d %s)\n", closePath.c_str(),
                 fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
