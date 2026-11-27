#pragma once

/// @file lifegen.hpp
/// @brief G39b: ambient life — global wind field, boids flocks, curl-noise insect swarms and
/// biome-specific ambient particles, plus LifeZone descriptors that worldgen derives from
/// the world data (birds over the lake, fish in rivers, insects over meadows, fireflies in
/// wet forests, sand in deserts, snow dust on ridges). Design: docs/AMBIENT_LIFE.md.
///
/// The sims are pure CPU, fixed-step and deterministic per seed (same stepping -> same
/// trajectories on every platform), so they are unit-testable headless; the app feeds the
/// per-frame positions into the existing G5 particle system / instancing (GPU particles)
/// or tiny instanced meshes. The same WindField math is mirrored app-side in the vertex
/// shader, so grass, tree crowns and airborne particles all react to the same gusts.

#include "noise.hpp"

#include <fuhgl/math.hpp>

#include <cstdint>
#include <vector>

namespace fuhglgraphics::procedural {

// --- global wind field ----------------------------------------------------------------

/// @brief Parameters of the shared 3D wind field: a base flow plus traveling sinus gusts
/// (a gust front sweeps the landscape: grass bends first, then branches, then leaves fly
/// off in exactly that direction).
struct WindFieldParams {
    fuhgl::math::Vec3 baseDir{1.f, 0.f, 0.25f}; ///< base wind direction (normalized on use)
    float baseSpeed = 2.5f;                    ///< m/s base flow
    float gustAmp = 2.0f;                      ///< extra speed of a full gust
    float gustWavelength = 60.f;               ///< world units per gust front
    float gustSpeed = 6.f;                     ///< how fast the front travels (world units/s)
    float turbulenceScale = 0.045f;            ///< noise scale of the turbulent component
    float turbulenceAmp = 0.8f;               ///< m/s
};

/// @brief Shared 3D wind field. sample(x, y, z, t) = base flow + traveling gust front +
/// fbm turbulence; divergence-free-ish and fully deterministic. The app mirrors the same
/// formula in the vertex shader (grass/trees) and passes it to the particle integrate.
class WindField {
public:
    explicit WindField(const WindFieldParams& p = {}) : p_(p) {
        dir_ = fuhgl::math::normalize(p_.baseDir);
    }
    const WindFieldParams& params() const { return p_; }
    /// @brief Wind vector at world position and time (world units/s).
    fuhgl::math::Vec3 sample(float x, float y, float z, float t) const;
private:
    WindFieldParams p_;
    fuhgl::math::Vec3 dir_{1.f, 0.f, 0.f};
    Noise noise_{0x51EDu}; ///< turbulence field of the wind
};

// --- boids -----------------------------------------------------------------------------

/// @brief Boids flock (separation / alignment / cohesion) advected by the shared wind,
/// kept inside a bounding sphere. Fixed-step deterministic.
struct BoidsParams {
    unsigned seed = 1;
    int count = 24;
    fuhgl::math::Vec3 center{0.f, 20.f, 0.f}; ///< flock center (world)
    float radius = 18.f;                      ///< bounding sphere radius
    float speed = 4.f;                        ///< cruise speed (world units/s)
    float neighborDist = 6.f;                 ///< perception radius
    float separationWeight = 1.6f;
    float alignmentWeight = 1.0f;
    float cohesionWeight = 0.8f;
    float windWeight = 0.35f;                 ///< how strongly the flock drifts with the wind
    float maxForce = 12.f;
};

class BoidsSim {
public:
    explicit BoidsSim(const BoidsParams& p = {});
    /// @brief Advances the flock by dt (fixed step recommended, e.g. 1/60 s).
    void step(float dt, const WindField& wind, float t);
    const std::vector<fuhgl::math::Vec3>& positions() const { return pos_; }
    const std::vector<fuhgl::math::Vec3>& velocities() const { return vel_; }
    const BoidsParams& params() const { return p_; }
private:
    BoidsParams p_;
    std::vector<fuhgl::math::Vec3> pos_, vel_;
    std::uint32_t rng_;
};

// --- curl-noise insects ----------------------------------------------------------------

/// @brief 3D curl noise of a vector potential built from simplex3 — divergence-free by
/// construction, so particles follow swirling, never-colliding tumbling paths (butterflies
/// over meadows, midges over swamps). Exposed for tests.
fuhgl::math::Vec3 curlNoise3(const Noise& n, float x, float y, float z);

struct InsectSwarmParams {
    unsigned seed = 1;
    int count = 40;
    fuhgl::math::Vec3 center{0.f, 3.f, 0.f};
    float radius = 8.f;          ///< soft bounding sphere
    float speed = 1.6f;          ///< advection speed through the curl field
    float noiseScale = 0.35f;    ///< curl field scale (1/world unit)
    float windWeight = 0.6f;
};

class InsectSwarm {
public:
    explicit InsectSwarm(const InsectSwarmParams& p = {});
    void step(float dt, const WindField& wind, float t);
    const std::vector<fuhgl::math::Vec3>& positions() const { return pos_; }
private:
    InsectSwarmParams p_;
    Noise noise_;
    std::vector<fuhgl::math::Vec3> pos_;
    std::uint32_t rng_;
};

// --- ambient particles -----------------------------------------------------------------

/// @brief Ambient particle species, mapped from the biome (forest -> leaves/pollen/seeds,
/// desert/coast -> sand/spray, snow -> snow dust, rain -> rain streaks app-side).
enum class AmbientKind { Leaf, Pollen, Seed, Sand, Snow, Spray };

/// @brief Biome code (worldgen Biome cast) + humidity -> ambient particle kind.
AmbientKind ambientKindFor(int biome, float humidity);

struct AmbientParticleParams {
    unsigned seed = 1;
    int count = 120;
    fuhgl::math::Vec3 center{0.f, 0.f, 0.f};
    fuhgl::math::Vec3 halfExtent{30.f, 12.f, 30.f}; ///< spawn box around center
    AmbientKind kind = AmbientKind::Pollen;
    float fallSpeed = 0.35f;    ///< sink (leaf/snow) or rise (pollen) speed, m/s
    float flutterAmp = 0.5f;     ///< sideways sin flutter, m/s
    float windWeight = 1.0f;     ///< advection by the shared wind field
    float respawn = true;        ///< dead particles respawn at the top of the box (continuous)
};

class AmbientParticleSim {
public:
    explicit AmbientParticleSim(const AmbientParticleParams& p = {});
    /// @brief Advances all particles by dt; with respawn the alive count stays constant.
    void step(float dt, const WindField& wind, float t);
    const std::vector<fuhgl::math::Vec3>& positions() const { return pos_; }
    const std::vector<fuhgl::math::Vec3>& velocities() const { return vel_; }
    int alive() const;
private:
    AmbientParticleParams p_;
    std::vector<fuhgl::math::Vec3> pos_, vel_;
    std::vector<float> age_;
    std::uint32_t rng_;
};

// --- life zones derived by the world generator -----------------------------------------

/// @brief A life zone: where a living system lives in the world. The app spawns the matching
/// GPU particles / instanced critters here; the CPU sims above simulate them.
struct LifeZone {
    enum class Kind { BirdFlock, FishSchool, InsectSwarm, Fireflies, AmbientParticles } kind;
    fuhgl::math::Vec3 center{0.f, 0.f, 0.f};
    float radius = 20.f;
    int count = 24;
    int biome = -1;        ///< worldgen Biome cast at the center (-1 = none)
    int ambientKind = -1;  ///< AmbientKind cast for AmbientParticles zones (-1 = none)
};

} // namespace fuhglgraphics::procedural
