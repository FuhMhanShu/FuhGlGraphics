// G32 creature-showcase screenshot tool: three different G32 creatures (allometric blueprint
// -> layered anatomy SDF -> marching cubes + curvature/material masks + baked Turing-pattern
// /countershading maps) standing side by side on procedural Corinthian columns in a pitch-dark
// hall, lit only by a few flickering torch point lights with G35 flame particles — so the
// creatures stay visible in the shadows. Headless (surfaceless EGL) to PNG for docs/images/.
//
// Usage: fuhglgraphics_creature_showcase --prefix g32_creature_showcase [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhglgraphics/procedural/creaturegen.hpp>
#include <fuhglgraphics/procedural/marching_cubes.hpp>
#include <fuhglgraphics/procedural/noise.hpp>
#include <fuhglgraphics/procedural/sdf.hpp>

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
    std::string prefix = "g32_creature_showcase";
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

// uploads a baked RGBA8 map; wrapS = REPEAT so the cylindrical u unwrap is seamless
fuhgl::Texture uploadMap(int size, const std::vector<unsigned char>& data, bool repeatU) {
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, size, size, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 data.data());
    t.generateMipmap();
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR_MIPMAP_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S,
                    repeatU ? fuhgl::gl::GL_REPEAT : fuhgl::gl::GL_CLAMP_TO_EDGE);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    return t;
}

// almost-black night sky cubemap with a few faint stars
fuhgl::Texture makeNightSky(int size, const fuhglgraphics::procedural::Noise& noise) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const Vec3 zenith{0.004f, 0.007f, 0.016f}, horizon{0.020f, 0.026f, 0.048f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i) {
                const float u = 2.f * (i + 0.5f) / size - 1.f;
                const float v = 2.f * (j + 0.5f) / size - 1.f;
                Vec3 d;
                switch (face) {
                    case 0: d = {1.f, -v, -u}; break;
                    case 1: d = {-1.f, -v, u}; break;
                    case 2: d = {u, 1.f, v}; break;
                    case 3: d = {u, -1.f, -v}; break;
                    case 4: d = {u, -v, 1.f}; break;
                    default: d = {-u, -v, -1.f}; break;
                }
                d = fuhgl::math::normalize(d);
                Vec3 col = fuhgl::math::lerp(horizon, zenith,
                                             fuhgl::math::clamp(d.y * 0.5f + 0.5f, 0.f, 1.f));
                const float star = noise.value3(d.x * 220.f, d.y * 220.f, d.z * 220.f);
                if (d.y > 0.05f && star > 0.9975f) col += Vec3{0.35f, 0.38f, 0.45f} * (star - 0.9975f) * 400.f;
                unsigned char* p = px.data() + (static_cast<std::size_t>(j) * size + i) * 4;
                p[0] = static_cast<unsigned char>(fuhgl::math::clamp(col.x, 0.f, 1.f) * 255.f);
                p[1] = static_cast<unsigned char>(fuhgl::math::clamp(col.y, 0.f, 1.f) * 255.f);
                p[2] = static_cast<unsigned char>(fuhgl::math::clamp(col.z, 0.f, 1.f) * 255.f);
                p[3] = 255;
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

// one G32 creature: ecology -> blueprint -> skeleton -> layered SDF -> surface + baked maps
struct CreatureActor {
    fuhgl::render::Mesh mesh;
    fuhgl::Texture albedoTex, normalTex;
    Vec3 headLocal{0.f, 0.f, 0.f};
};

CreatureActor buildG32Creature(const fuhglgraphics::procedural::Ecology& eco, unsigned seed,
                               const fuhglgraphics::procedural::Palette& pal, int meshRes,
                               int mapSize, float stripeAnisotropy) {
    using namespace fuhglgraphics::procedural;
    const Blueprint bp = deriveBlueprint(eco, seed);
    const Skeleton skel = buildBlueprintSkeleton(bp);
    Anatomy an;
    an.seed = seed;
    an.detailAmp = 0.012f;
    an.scaleAmp = 0.010f;
    an.scaleFreq = 10.f;
    const SurfaceMasks surf = extractSurface(skel, an, meshRes, {1.f, 1.f, 1.f});
    CreatureActor a;
    a.mesh.setMeshData(surf.mesh);
    Pattern pt;
    pt.seed = seed * 7u + 1u;
    pt.iterations = 350;
    pt.anisotropy = stripeAnisotropy;
    const BakedMaps maps = bakeMaterial(skel, an, pal, pt, mapSize);
    a.albedoTex = uploadMap(maps.size, maps.albedo, true);
    a.normalTex = uploadMap(maps.size, maps.normal, true);
    for (int i = 0; i < static_cast<int>(skel.nodes.size()); ++i)
        if (skel.nodes[i].name == "head") a.headLocal = skel.worldPos(i);
    std::fprintf(stderr, "creature_showcase: %s seed=%u -> %zu tris, maps %dx%d\n",
                 eco.diet == Diet::Carnivore ? "carnivore"
                 : eco.diet == Diet::Herbivore ? "herbivore" : "omnivore",
                 seed, surf.mesh.indices.size() / 3, maps.size, maps.size);
    return a;
}

// Corinthian-style column SDF (local space, base at y=0): plinth + fluted shaft +
// echinus flare + acanthus-leaf ring + abacus
float columnSdf(const Vec3& p, float H, const fuhglgraphics::procedural::Noise& nz) {
    using fuhglgraphics::procedural::sdf::smoothMin;
    float d = 1e9f;
    d = smoothMin(d, fuhglgraphics::procedural::sdf::roundedBox(p - Vec3{0.f, 0.10f, 0.f},
                                                                {0.55f, 0.10f, 0.55f}, 0.03f), 0.05f);
    d = smoothMin(d, fuhglgraphics::procedural::sdf::roundedBox(p - Vec3{0.f, 0.24f, 0.f},
                                                                {0.46f, 0.06f, 0.46f}, 0.03f), 0.05f);
    // fluted shaft: exact segment-cylinder with an angle-modulated radius
    const float y0 = 0.30f, y1 = H - 0.55f;
    const float theta = std::atan2(p.z, p.x);
    const float flute = 0.020f * std::pow(std::fmax(0.f, std::sin(18.f * theta)), 2.f);
    const float dr = std::sqrt(p.x * p.x + p.z * p.z) - (0.26f - flute);
    const float dy = std::fmax(y0 - p.y, p.y - y1);
    const float seg = fuhgl::math::length(Vec3{std::fmax(dr, 0.f), std::fmax(dy, 0.f), 0.f}) +
                      std::fmin(std::fmax(dr, dy), 0.f);
    d = smoothMin(d, seg, 0.03f);
    // echinus: tapering cone collar
    const float ct = fuhgl::math::clamp((p.y - y1) / 0.22f, 0.f, 1.f);
    const float dr2 = std::sqrt(p.x * p.x + p.z * p.z) - (0.26f + 0.55f * ct);
    const float dy2 = std::fmax(y1 - p.y, p.y - (y1 + 0.22f));
    const float cone = fuhgl::math::length(Vec3{std::fmax(dr2, 0.f), std::fmax(dy2, 0.f), 0.f}) +
                       std::fmin(std::fmax(dr2, dy2), 0.f);
    d = smoothMin(d, cone, 0.04f);
    // acanthus leaves: a ring of small spheres under the abacus
    for (int k = 0; k < 8; ++k) {
        const float a = k * fuhgl::math::kPi / 4.f + 0.39f;
        const Vec3 c{0.33f * std::cos(a), y1 + 0.10f, 0.33f * std::sin(a)};
        d = smoothMin(d, fuhglgraphics::procedural::sdf::sphere(p - c, 0.065f), 0.03f);
    }
    // abacus
    d = smoothMin(d, fuhglgraphics::procedural::sdf::roundedBox(p - Vec3{0.f, y1 + 0.30f, 0.f},
                                                                {0.48f, 0.08f, 0.48f}, 0.03f), 0.04f);
    d += 0.008f * nz.fbm3(p.x * 7.f, p.y * 7.f, p.z * 7.f, 2,
                          fuhglgraphics::procedural::Noise::Base::Simplex); // stone grain
    return d;
}

constexpr float kColumnH = 3.4f;
constexpr float kColumnTop = kColumnH - 0.17f; // abacus top in column-local space

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
    if (!window.create("fuhglgraphics_creature_showcase", 64, 64)) {
        std::fprintf(stderr, "creature_showcase: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "creature_showcase: context below GL 3.3\n");
        return 2;
    }

    const procedural::Noise noise(20240u);
    scene::Scene scene;

    // --- dark stone floor slab ---
    {
        fuhgl::render::Mesh* ground = new fuhgl::render::Mesh();
        ground->setMeshData(procedural::meshFromSdf(
            [](const Vec3& p) {
                return procedural::sdf::box(p - Vec3{0.f, -0.5f, 0.f}, {24.f, 0.5f, 24.f});
            },
            {-24.f, -1.01f, -24.f}, {24.f, 0.01f, 24.f}, 4, {0.15f, 0.14f, 0.16f}));
        scene::Node& n = scene.root().createChild("floor");
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(ground);
        e.albedo = {0.55f, 0.53f, 0.58f}; // vertex color * albedo
        e.roughness = 0.95f;
    }

    // --- one shared Corinthian column mesh, three instances ---
    fuhgl::render::Mesh* columnMesh = new fuhgl::render::Mesh();
    columnMesh->setMeshData(procedural::meshFromSdf(
        [&noise](const Vec3& p) { return columnSdf(p, kColumnH, noise); },
        {-0.75f, -0.05f, -0.75f}, {0.75f, kColumnH + 0.45f, 0.75f}, 56, {0.78f, 0.74f, 0.68f}));
    const float columnX[3] = {-4.2f, 0.f, 4.2f};
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("column" + std::to_string(i));
        n.translation = {columnX[i], 0.f, 0.f};
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(columnMesh);
        e.albedo = {1.f, 1.f, 1.f};
        e.roughness = 0.9f;
    }

    // --- three different G32 creatures on the columns ---
    procedural::Palette palHerb;
    palHerb.dorsal = {0.45f, 0.36f, 0.24f};
    palHerb.ventral = {0.80f, 0.72f, 0.58f};
    palHerb.accent = {0.25f, 0.14f, 0.08f};
    procedural::Palette palCarn;
    palCarn.dorsal = {0.16f, 0.17f, 0.19f};
    palCarn.ventral = {0.40f, 0.40f, 0.42f};
    palCarn.accent = {0.50f, 0.20f, 0.10f};
    palCarn.roughness = 0.4f;
    procedural::Palette palSerp;
    palSerp.dorsal = {0.55f, 0.47f, 0.32f};
    palSerp.ventral = {0.85f, 0.80f, 0.66f};
    palSerp.accent = {0.30f, 0.18f, 0.10f};

    procedural::Ecology ecoHerb;
    ecoHerb.bodyMassKg = 80.f;
    ecoHerb.locomotion = procedural::Locomotion::Quadruped;
    ecoHerb.diet = procedural::Diet::Herbivore; // horns
    ecoHerb.climate = procedural::Climate::Temperate;
    procedural::Ecology ecoCarn = ecoHerb;
    ecoCarn.bodyMassKg = 12.f;
    ecoCarn.diet = procedural::Diet::Carnivore; // frontal eyes
    procedural::Ecology ecoSerp;
    ecoSerp.bodyMassKg = 25.f;
    ecoSerp.locomotion = procedural::Locomotion::Serpentine;
    ecoSerp.diet = procedural::Diet::Carnivore;
    ecoSerp.climate = procedural::Climate::Arid;

    CreatureActor herb = buildG32Creature(ecoHerb, 42u, palHerb, 72, 192, 1.f);
    CreatureActor carn = buildG32Creature(ecoCarn, 7u, palCarn, 72, 192, 2.5f);
    CreatureActor serp = buildG32Creature(ecoSerp, 99u, palSerp, 72, 192, 6.f); // stripes

    CreatureActor* actors[3] = {&herb, &carn, &serp};
    const float yaws[3] = {fuhgl::math::radians(-18.f), fuhgl::math::radians(38.f),
                           fuhgl::math::radians(22.f)}; // profile view for all three
    for (int i = 0; i < 3; ++i) {
        scene::Node& n = scene.root().createChild("creature" + std::to_string(i));
        n.translation = {columnX[i], kColumnTop + 0.02f, 0.f};
        n.rotation = fuhgl::math::fromAxisAngle(yaws[i], {0.f, 1.f, 0.f});
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(&actors[i]->mesh);
        e.albedo = {1.f, 1.f, 1.f}; // vertex color (curvature shade) * baked albedo texture
        e.albedoTexture = &actors[i]->albedoTex;
        e.normalMap = &actors[i]->normalTex;
        e.roughness = 0.55f;
        e.metallic = 0.05f;
        e.castShadow = false;
    }

    // --- torches: pole mesh + G35 flame emitter + flickering point light ---
    struct Torch {
        scene::LightComponent* light = nullptr;
        scene::ParticleEmitterComponent* fire = nullptr;
        Vec3 pos{0.f, 0.f, 0.f};
    };
    std::vector<Torch> torches;
    const Vec3 torchPos[3] = {{-2.3f, 0.f, 1.6f}, {2.3f, 0.f, 1.6f}, {0.f, 0.f, -2.9f}};
    const Vec3 torchColor{1.0f, 0.52f, 0.22f};
    const float torchIntensity = 18.f, torchRange = 22.f;
    for (int i = 0; i < 3; ++i) {
        const Vec3& tp = torchPos[i];
        // iron pole + bowl
        fuhgl::render::Mesh* pole = new fuhgl::render::Mesh();
        pole->setMeshData(procedural::meshFromSdf(
            [](const Vec3& p) {
                float d = fuhglgraphics::procedural::sdf::cylinder(p, 0.045f, 2.0f);
                d = fuhglgraphics::procedural::sdf::smoothMin(
                    d, fuhglgraphics::procedural::sdf::sphere(p - Vec3{0.f, 2.05f, 0.f}, 0.13f), 0.06f);
                return d;
            },
            {-0.2f, -0.05f, -0.2f}, {0.2f, 2.25f, 0.2f}, 20, {0.5f, 0.45f, 0.42f}));
        scene::Node& pn = scene.root().createChild("torchPole" + std::to_string(i));
        pn.translation = tp;
        scene::MeshEntry& pe = pn.attach<scene::MeshComponent>().addMesh(pole);
        pe.albedo = {0.24f, 0.22f, 0.22f};
        pe.roughness = 0.7f;
        pe.metallic = 0.6f;
        pe.castShadow = false;
        // flame particles at the bowl
        scene::Node& fn = scene.root().createChild("torchFire" + std::to_string(i));
        fn.translation = tp + Vec3{0.f, 2.12f, 0.f};
        auto& em = fn.attach<scene::ParticleEmitterComponent>();
        em.config.position = {0.f, 0.f, 0.f};
        em.config.direction = {0.f, 1.f, 0.f};
        em.config.speed = 0.55f;
        em.config.spread = 0.22f;
        em.config.rate = 140.f;
        em.config.lifetime = 0.9f;
        em.config.size = 0.40f;
        em.config.grow = 0.22f;
        em.config.color = {1.0f, 0.70f, 0.28f};
        em.config.alpha = 0.6f;
        em.config.gravity = 0.5f;
        em.config.drag = 1.2f;
        em.config.blend = particles::BlendMode::Additive;
        em.config.flame = true;
        if (!em.system.create(8192, em.config)) {
            std::fprintf(stderr, "creature_showcase: flame emitter create failed\n");
            return 1;
        }
        // point light at the flame
        scene::Node& ln = scene.root().createChild("torchLight" + std::to_string(i));
        ln.translation = tp + Vec3{0.f, 2.35f, 0.f};
        auto& lc = ln.attach<scene::LightComponent>();
        lc.light = fuhgl::render::Light::point({0.f, 0.f, 0.f}, torchColor, torchIntensity, torchRange);
        Torch t;
        t.light = &lc;
        t.fire = &em;
        t.pos = tp;
        torches.push_back(t);
    }
    // faint cool moonlight so the silhouettes read in the dark (no shadows from it)
    scene.root().createChild("moon").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({0.2f, -1.f, 0.3f}, {0.10f, 0.13f, 0.20f}, 0.25f);

    // pre-simulate the flames so the first frame already shows developed fire
    for (const Torch& t : torches)
        for (int i = 0; i < 120; ++i) t.fire->system.update(1.f / 60.f);

    // --- night sky + renderer: dark scene, torch-lit, bloom on the flames ---
    fuhgl::Texture nightCube = makeNightSky(128, noise);
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    renderer.setRenderScale(args.scale);
    if (nightCube.id() != 0) renderer.setSkyCubemap(&nightCube);
    renderer.skyTop = {0.004f, 0.007f, 0.016f};
    renderer.skyBottom = {0.020f, 0.026f, 0.048f};

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::Ssao);
    scene.root().effects.enable(render::Effect::Bloom);
    scene.root().effects.enable(render::Effect::Particles);
    scene.root().effects.enable(render::Effect::VolumetricFog);
    scene.root().effects.params.exposure = 1.5f;
    scene.root().effects.params.taaBlend = 0.08f;
    scene.root().effects.params.ssaoRadius = 0.8f;
    scene.root().effects.params.ssaoIntensity = 1.2f;
    scene.root().effects.params.bloomThreshold = 0.55f;
    scene.root().effects.params.bloomStrength = 0.9f;
    scene.root().effects.params.volumetricDensity = 0.0006f;
    scene.root().effects.params.volumetricCloudiness = 0.5f;

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(50.f);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    const auto renderShot = [&](const std::string& outPath) {
        const float dt = 1.f / 60.f;
        for (int f = 0; f < args.frames; ++f) {
            const float t = static_cast<float>(f) * dt;
            for (int i = 0; i < static_cast<int>(torches.size()); ++i) {
                const float flick = 0.78f + 0.22f * std::sin(11.f * t + i * 2.3f) +
                                    0.08f * std::sin(29.f * t + i * 5.1f);
                torches[i].light->light = fuhgl::render::Light::point(
                    {0.f, 0.f, 0.f}, torchColor, torchIntensity * flick, torchRange);
            }
            for (const Torch& tc : torches) tc.fire->system.update(dt);
            scene::FrameData frame;
            scene.fillFrame(frame, &cam, args.width, args.height);
            frame.effects.applyTo(renderer);
            frame.view.fogColor = {0.030f, 0.025f, 0.020f};
            frame.view.fogDensity = 0.0012f;
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow,
                            &frame.particleSystems);
        }
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
        fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA,
                              fuhgl::gl::GL_UNSIGNED_BYTE, pixels.data());
        if (!fuhglgraphics::screenshot::writePng(outPath.c_str(), pixels.data(), args.width,
                                                  args.height)) {
            std::fprintf(stderr, "creature_showcase: failed to write %s\n", outPath.c_str());
            return false;
        }
        std::fprintf(stderr, "creature_showcase: wrote %s\n", outPath.c_str());
        return true;
    };

    // shot 1: wide — the three creatures side by side on their columns, torch-lit from below/front
    cam.position = {0.f, kColumnTop + 2.6f, 12.5f};
    aimAt(cam, {0.f, kColumnTop + 0.7f, 0.f});
    if (!renderShot(args.prefix + "_wide.png")) return 1;

    // shot 2: closeup of the middle (carnivore) creature in profile — Turing spots +
    // countershading in torchlight
    cam.position = {2.2f, kColumnTop + 1.7f, 4.4f};
    aimAt(cam, {0.f, kColumnTop + 1.0f, 0.f});
    if (!renderShot(args.prefix + "_closeup.png")) return 1;

    std::fprintf(stderr, "creature_showcase: done (GL %d.%d %s)\n", fuhgl::gl::version().major,
                 fuhgl::gl::version().minor, fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
