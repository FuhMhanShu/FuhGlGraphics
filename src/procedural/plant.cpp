// PlantGenerator implementation: per-kind L-system presets (deciduous / fir / birch / bush)
// with seed jitter, per-kind palettes (birch = white trunk, fir = dark spruce green) and crown
// shaping after generation — conical squash of the leaf vertices for firs, droop for birches.
// Leaf quads are recognized by their vertex color (exactly the leaf color passed to
// LSystem::generate). Deterministic per seed via splitmix32.

#include <fuhglgraphics/procedural/plant.hpp>

#include <fuhglgraphics/procedural/lsystem.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fuhglgraphics::procedural {

namespace {

struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() {
        state += 0x9E3779B9u;
        std::uint32_t z = state;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }
    float nextf() { return static_cast<float>(next() * (1.0 / 4294967296.0)); }
    float range(float a, float b) { return a + (b - a) * nextf(); }
};

bool isLeafVertex(const float* v, const fuhgl::math::Vec3& leafColor) {
    return std::fabs(v[6] - leafColor.x) < 1e-4f && std::fabs(v[7] - leafColor.y) < 1e-4f &&
           std::fabs(v[8] - leafColor.z) < 1e-4f;
}

/// @brief L-system preset for a plant kind with per-seed jitter (angle/step/leaf size).
LSystem makeLSystem(PlantKind kind, Rng& rng) {
    LSystem s;
    switch (kind) {
        case PlantKind::Fir: {
            // conifer whorls: two pitched branches per segment, each ending in a needle tuft
            // (L); the conical crown comes from the post-squash in generate()
            s.axiom = "F";
            s.rules = {{'F', "FF[&+F]L[-&F]L"}};
            s.iterations = 5;
            s.sides = 5;
            s.leaves = true;
            s.angleDeg = 40.f * rng.range(0.9f, 1.15f);
            s.step = 0.55f * rng.range(0.85f, 1.1f);
            s.radius0 = 0.06f;
            s.radiusTaper = 0.7f;
            s.leafSize = 0.30f * rng.range(0.8f, 1.2f);
            break;
        }
        case PlantKind::Birch: // broadleaf skeleton, white bark + light drooping crown
            s = LSystem::makeTree(static_cast<int>(rng.next() % 3u), rng.next());
            s.angleDeg *= rng.range(0.9f, 1.1f);
            s.leafSize *= rng.range(0.7f, 1.0f); // smaller leaves
            break;
        case PlantKind::Bush:
            s = LSystem::makeBush();
            s.angleDeg *= rng.range(0.85f, 1.2f);
            s.step *= rng.range(0.8f, 1.2f);
            s.leafSize *= rng.range(0.85f, 1.25f);
            break;
        case PlantKind::Deciduous:
        default:
            s = LSystem::makeTree(static_cast<int>(rng.next() % 3u), rng.next());
            s.angleDeg *= rng.range(0.9f, 1.15f);
            s.step *= rng.range(0.85f, 1.15f);
            s.leafSize *= rng.range(0.85f, 1.2f);
            break;
    }
    return s;
}

} // namespace

PlantParams PlantGenerator::randomParams(PlantKind kind, unsigned seed) {
    PlantParams p;
    p.kind = kind;
    p.seed = seed;
    Rng rng(seed);
    p.scale = rng.range(0.7f, 1.4f);
    return p;
}

PlantMesh PlantGenerator::generate(const PlantParams& p) const {
    Rng rng(p.seed);
    PlantMesh pm;
    pm.kind = p.kind;
    // per-kind palette: birch gets white bark, fir dark spruce-green needles
    pm.trunkColor = {0.32f, 0.22f, 0.14f};
    pm.leafColor = {0.16f, 0.42f, 0.18f};
    if (p.kind == PlantKind::Birch) {
        pm.trunkColor = {0.88f, 0.87f, 0.82f};
        pm.leafColor = {0.45f, 0.62f, 0.22f};
    } else if (p.kind == PlantKind::Fir) {
        pm.trunkColor = {0.25f, 0.17f, 0.11f};
        pm.leafColor = {0.10f, 0.30f, 0.14f};
    } else if (p.kind == PlantKind::Bush) {
        pm.leafColor = {0.14f, 0.38f, 0.16f};
    }
    LSystem ls = makeLSystem(p.kind, rng);
    pm.mesh = ls.generate(pm.trunkColor, pm.leafColor);

    // overall scale + measured height
    float maxY = 1e-6f;
    for (std::size_t i = 0; i < pm.mesh.vertexCount(); ++i)
        maxY = std::max(maxY, pm.mesh.vertices[i * 11 + 1]);
    for (std::size_t i = 0; i < pm.mesh.vertexCount(); ++i) {
        float* v = &pm.mesh.vertices[i * 11];
        v[0] *= p.scale;
        v[1] *= p.scale;
        v[2] *= p.scale;
    }
    pm.height = maxY * p.scale;

    // crown shaping on the leaf vertices: firs become conical, birches droop
    if (p.kind == PlantKind::Fir || p.kind == PlantKind::Birch) {
        const float crownFactor = p.kind == PlantKind::Fir ? 0.75f : 0.0f;
        const float droop = p.kind == PlantKind::Birch ? 0.82f : 1.f;
        for (std::size_t i = 0; i < pm.mesh.vertexCount(); ++i) {
            float* v = &pm.mesh.vertices[i * 11];
            if (!isLeafVertex(v, pm.leafColor)) continue;
            const float t = std::min(1.f, std::max(0.f, v[1] / std::max(1e-6f, pm.height)));
            const float squash = 1.f - crownFactor * t; // higher leaves -> closer to the axis
            v[0] *= squash;
            v[2] *= squash;
            v[1] *= droop;
        }
    }
    return pm;
}

} // namespace fuhglgraphics::procedural
