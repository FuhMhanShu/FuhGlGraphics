// G39b ambient life: shared 3D wind field (base + traveling gust fronts + fbm turbulence),
// boids flocks (separation/alignment/cohesion, wind-advected, sphere-bounded), curl-noise
// insect swarms (divergence-free tumbling paths), biome-driven ambient particles (leaves,
// pollen, seeds, sand, snow, spray). All pure CPU, fixed-step, deterministic per seed.

#include <fuhglgraphics/procedural/lifegen.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fuhglgraphics::procedural {

namespace {

struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed ? seed : 1u) {}
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

using fuhgl::math::dot;

/// @brief Vector potential field from three offset simplex3 octaves.
fuhgl::math::Vec3 vecNoise3(const Noise& n, float x, float y, float z) {
    return {n.simplex3(x, y, z),
            n.simplex3(x + 31.4f, y + 47.2f, z + 11.8f),
            n.simplex3(x + 83.1f, y + 13.7f, z + 59.3f)};
}

} // namespace

fuhgl::math::Vec3 curlNoise3(const Noise& n, float x, float y, float z) {
    // curl = nabla x F with F = vecNoise3 (vector potential), via central differences —
    // divergence-free by construction, so advected particles never collide head-on
    const float e = 0.1f, inv = 1.f / (2.f * e);
    const fuhgl::math::Vec3 dxp = vecNoise3(n, x + e, y, z), dxm = vecNoise3(n, x - e, y, z);
    const fuhgl::math::Vec3 dyp = vecNoise3(n, x, y + e, z), dym = vecNoise3(n, x, y - e, z);
    const fuhgl::math::Vec3 dzp = vecNoise3(n, x, y, z + e), dzm = vecNoise3(n, x, y, z - e);
    return {(dyp.z - dym.z - dzp.y + dzm.y) * inv,  // dFz/dy - dFy/dz
            (dzp.x - dzm.x - dxp.z + dxm.z) * inv,  // dFx/dz - dFz/dx
            (dxp.y - dxm.y - dyp.x + dym.x) * inv}; // dFy/dx - dFx/dy
}

// --- wind field ------------------------------------------------------------------------

fuhgl::math::Vec3 WindField::sample(float x, float y, float z, float t) const {
    // traveling gust front along the base direction: sin(dot(pos,dir)/lambda*2pi - t*w)
    const float phase = (x * dir_.x + z * dir_.z) / p_.gustWavelength * 2.f * fuhgl::math::kPi -
                        t * p_.gustSpeed / p_.gustWavelength * 2.f * fuhgl::math::kPi;
    const float gust = 0.5f + 0.5f * std::sin(phase); // 0..1
    fuhgl::math::Vec3 w = dir_ * (p_.baseSpeed + p_.gustAmp * gust);
    // turbulence: fbm in a moving frame + slight vertical shear
    const float turb = noise_.fbm3(x * p_.turbulenceScale + t * 0.7f,
                                   y * p_.turbulenceScale,
                                   z * p_.turbulenceScale + t * 0.5f, 3, Noise::Base::Simplex);
    w.x += turb * p_.turbulenceAmp;
    w.z += noise_.fbm3(z * p_.turbulenceScale * 1.3f - t * 0.4f, y * p_.turbulenceScale,
                       x * p_.turbulenceScale, 2, Noise::Base::Simplex) * p_.turbulenceAmp;
    w.y += 0.15f * p_.turbulenceAmp * std::sin(t * 0.9f + x * 0.1f);
    return w;
}

// --- boids -----------------------------------------------------------------------------

BoidsSim::BoidsSim(const BoidsParams& p) : p_(p), rng_(p.seed ? p.seed : 1u) {
    Rng r(rng_);
    pos_.reserve(p_.count);
    vel_.reserve(p_.count);
    for (int i = 0; i < p_.count; ++i) {
        const float a = r.range(0.f, 6.283f), b = r.range(-1.f, 1.f);
        const float rr = r.range(0.f, p_.radius * 0.5f);
        const float s = std::sqrt(1.f - b * b);
        pos_.push_back(p_.center + fuhgl::math::Vec3{std::cos(a) * s * rr, b * rr * 0.5f,
                                                     std::sin(a) * s * rr});
        const float va = r.range(0.f, 6.283f);
        vel_.push_back({std::cos(va) * p_.speed, r.range(-0.3f, 0.3f) * p_.speed,
                        std::sin(va) * p_.speed});
    }
}

void BoidsSim::step(float dt, const WindField& wind, float t) {
    const int n = static_cast<int>(pos_.size());
    if (n == 0) return;
    std::vector<fuhgl::math::Vec3> newPos(n), newVel(n);
    const fuhgl::math::Vec3 w = wind.sample(p_.center.x, p_.center.y, p_.center.z, t);
    for (int i = 0; i < n; ++i) {
        fuhgl::math::Vec3 sep{0.f, 0.f, 0.f}, ali{0.f, 0.f, 0.f}, coh{0.f, 0.f, 0.f};
        int neighbors = 0;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const fuhgl::math::Vec3 d = pos_[j] - pos_[i];
            const float dist2 = dot(d, d);
            if (dist2 > p_.neighborDist * p_.neighborDist) continue;
            ++neighbors;
            sep = sep - d * (1.f / std::max(dist2, 0.01f)); // separation: push apart
            ali = ali + vel_[j];                           // alignment: average velocity
            coh = coh + pos_[j];                           // cohesion: toward centroid
        }
        fuhgl::math::Vec3 force{0.f, 0.f, 0.f};
        if (neighbors > 0) {
            ali = ali * (1.f / neighbors) - vel_[i];
            coh = coh * (1.f / neighbors) - pos_[i];
            force = force + sep * p_.separationWeight + ali * p_.alignmentWeight +
                    coh * (p_.cohesionWeight * 0.05f);
        }
        force = force + w * p_.windWeight; // shared wind advects the flock
        // soft boundary: steer back toward the center when leaving the sphere
        const fuhgl::math::Vec3 fromC = pos_[i] - p_.center;
        const float dc = std::sqrt(dot(fromC, fromC));
        if (dc > p_.radius) force = force - fromC * ((dc - p_.radius) * 0.5f);
        const float fl = std::sqrt(dot(force, force));
        if (fl > p_.maxForce) force = force * (p_.maxForce / fl);
        fuhgl::math::Vec3 v = vel_[i] + force * dt;
        // keep cruise speed in a band
        const float vl = std::sqrt(dot(v, v));
        const float target = p_.speed * (0.7f + 0.6f * (0.5f + 0.5f * std::sin(t * 0.7f + i)));
        if (vl > 1e-4f) v = v * (target / vl);
        newVel[i] = v;
        newPos[i] = pos_[i] + v * dt;
    }
    pos_ = std::move(newPos);
    vel_ = std::move(newVel);
}

// --- insects ---------------------------------------------------------------------------

InsectSwarm::InsectSwarm(const InsectSwarmParams& p) : p_(p), noise_(p.seed ^ 0x1BEEu), rng_(p.seed ? p.seed : 1u) {
    Rng r(rng_);
    pos_.reserve(p_.count);
    for (int i = 0; i < p_.count; ++i) {
        const float a = r.range(0.f, 6.283f), b = r.range(-1.f, 1.f);
        const float rr = std::sqrt(r.nextf()) * p_.radius;
        const float s = std::sqrt(1.f - b * b);
        pos_.push_back(p_.center + fuhgl::math::Vec3{std::cos(a) * s * rr, r.range(0.5f, 2.5f),
                                                     std::sin(a) * s * rr});
    }
}

void InsectSwarm::step(float dt, const WindField& wind, float t) {
    const fuhgl::math::Vec3 w = wind.sample(p_.center.x, p_.center.y, p_.center.z, t);
    for (auto& p : pos_) {
        // curl noise: swirling, non-colliding tumble + shared wind advection
        const fuhgl::math::Vec3 c = curlNoise3(noise_, p.x * p_.noiseScale, p.y * p_.noiseScale,
                                               p.z * p_.noiseScale);
        fuhgl::math::Vec3 v = c * p_.speed + w * p_.windWeight;
        p = p + v * dt;
        // soft boundary
        const fuhgl::math::Vec3 fromC = p - p_.center;
        const float dc = std::sqrt(dot(fromC, fromC));
        if (dc > p_.radius) p = p - fromC * ((dc - p_.radius) * 0.2f);
        // keep insects near the ground layer
        if (p.y < 0.2f) p.y = 0.2f;
    }
    (void)t;
}

// --- ambient particles -----------------------------------------------------------------

AmbientKind ambientKindFor(int biome, float humidity) {
    // worldgen Biome cast: 2 Desert, 3 Grassland, 4 Forest, 5 Jungle, 6 Taiga, 7 Tundra,
    // 8 Rock, 9 Snow, 0 Ocean, 1 Shore
    switch (biome) {
        case 2: return AmbientKind::Sand;   // desert: blowing sand
        case 9: return AmbientKind::Snow;   // snow peaks: snow dust
        case 0: case 1: return humidity > 0.6f ? AmbientKind::Spray : AmbientKind::Sand; // coast
        case 4: case 5: return humidity > 0.65f ? AmbientKind::Seed : AmbientKind::Leaf; // forest/jungle
        case 6: return AmbientKind::Snow;   // taiga: drifting snow
        case 8: return AmbientKind::Sand;   // rock: wind-blown dust
        default: return humidity > 0.55f ? AmbientKind::Pollen : AmbientKind::Leaf; // grassland
    }
}

AmbientParticleSim::AmbientParticleSim(const AmbientParticleParams& p)
    : p_(p), rng_(p.seed ? p.seed : 1u) {
    Rng r(rng_);
    pos_.reserve(p_.count);
    vel_.reserve(p_.count);
    age_.reserve(p_.count);
    for (int i = 0; i < p_.count; ++i) {
        pos_.push_back(p_.center + fuhgl::math::Vec3{r.range(-p_.halfExtent.x, p_.halfExtent.x),
                                                     r.range(0.f, p_.halfExtent.y),
                                                     r.range(-p_.halfExtent.z, p_.halfExtent.z)});
        vel_.push_back({0.f, 0.f, 0.f});
        age_.push_back(r.range(0.f, 10.f));
    }
}

int AmbientParticleSim::alive() const { return static_cast<int>(pos_.size()); }

void AmbientParticleSim::step(float dt, const WindField& wind, float t) {
    Rng r(rng_);
    const int n = static_cast<int>(pos_.size());
    for (int i = 0; i < n; ++i) {
        const fuhgl::math::Vec3 w = wind.sample(pos_[i].x, pos_[i].y, pos_[i].z, t);
        // per-kind vertical drift: leaves/snow sink slowly, pollen/seeds hover, sand/spry low
        float fall = p_.fallSpeed;
        if (p_.kind == AmbientKind::Pollen || p_.kind == AmbientKind::Seed) fall = -0.06f; // rise
        if (p_.kind == AmbientKind::Sand || p_.kind == AmbientKind::Spray) fall = 0.15f;   // low
        if (p_.kind == AmbientKind::Snow) fall = 0.5f;
        const float flut = p_.flutterAmp * std::sin(t * (1.3f + 0.2f * i) + i * 1.7f);
        fuhgl::math::Vec3 v = w * p_.windWeight + fuhgl::math::Vec3{flut, -fall, flut * 0.7f};
        pos_[i] = pos_[i] + v * dt;
        vel_[i] = v;
        age_[i] += dt;
        // respawn: out of the box or too old -> back to the upwind/top edge
        const fuhgl::math::Vec3 rel = pos_[i] - p_.center;
        const bool out = std::fabs(rel.x) > p_.halfExtent.x || std::fabs(rel.z) > p_.halfExtent.z ||
                         rel.y > p_.halfExtent.y || rel.y < -2.f || age_[i] > 30.f;
        if (out && p_.respawn) {
            pos_[i] = p_.center + fuhgl::math::Vec3{r.range(-p_.halfExtent.x, p_.halfExtent.x),
                                                    p_.halfExtent.y * 0.9f,
                                                    r.range(-p_.halfExtent.z, p_.halfExtent.z)};
            age_[i] = 0.f;
        }
    }
}

} // namespace fuhglgraphics::procedural
