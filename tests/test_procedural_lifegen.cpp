// Unit tests for the G39b ambient-life sims (label "unit"): shared wind field (base + gust
// fronts + turbulence, deterministic), boids (separation/alignment/cohesion, bounded,
// wind-advected, deterministic), curl-noise insects (bounded tumbling paths, divergence-free
// field), biome-mapped ambient particles (kind mapping, respawn, bounds, snow sinks),
// and determinism of all sims at fixed dt.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/lifegen.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

using namespace fuhglgraphics::procedural;
using fuhgl::math::Vec3;

float dot3(const Vec3& a, const Vec3& b) { return fuhgl::math::dot(a, b); }

void testWindField() {
    WindFieldParams p;
    p.baseDir = {1.f, 0.f, 0.f};
    p.baseSpeed = 3.f;
    p.gustAmp = 2.f;
    WindField wf(p);
    // base flow along +x, magnitude in [baseSpeed, baseSpeed+gustAmp] plus turbulence
    const Vec3 w0 = wf.sample(0.f, 10.f, 0.f, 0.f);
    CHECK(w0.x > 1.5f && w0.x < 6.f);
    // gust front travels: wind at a fixed point changes over time
    const Vec3 a = wf.sample(0.f, 10.f, 0.f, 0.f);
    const Vec3 b = wf.sample(0.f, 10.f, 0.f, 3.7f);
    CHECK(std::fabs(a.x - b.x) > 1e-3f);
    // determinism
    CHECK(wf.sample(5.f, 2.f, -3.f, 1.25f).x == wf.sample(5.f, 2.f, -3.f, 1.25f).x);
    // gust front moves downstream: the max-gust position travels +x over time. Scan only one
    // gust wavelength (the gust is periodic, so a wider scan would find several equal peaks).
    // (turbulence off, so the gust peak is cleanly trackable)
    WindFieldParams pg = p;
    pg.turbulenceAmp = 0.f;
    WindField wfg(pg);
    float bestX0 = 0.f, bestX1 = 0.f;
    float v0 = wfg.sample(0.f, 10.f, 0.f, 0.f).x;
    float v1 = wfg.sample(0.f, 10.f, 0.f, 5.f).x;
    for (float x = 1.f; x < pg.gustWavelength; x += 1.f) {
        const float a = wfg.sample(x, 10.f, 0.f, 0.f).x;
        const float b = wfg.sample(x, 10.f, 0.f, 5.f).x;
        if (a > v0) { v0 = a; bestX0 = x; }
        if (b > v1) { v1 = b; bestX1 = x; }
    }
    CHECK(bestX1 > bestX0 + 5.f); // front moved downwind (peak at lambda/4 + gustSpeed*t)
}

void testBoids() {
    BoidsParams p;
    p.seed = 11u;
    p.count = 30;
    p.center = {0.f, 20.f, 0.f};
    p.radius = 15.f;
    BoidsSim sim(p);
    WindField wf;
    const auto& p0 = sim.positions();
    CHECK(p0.size() == 30);
    // step 600 fixed steps (10 s)
    for (int i = 0; i < 600; ++i) sim.step(1.f / 60.f, wf, i / 60.f);
    const auto& ps = sim.positions();
    const auto& vs = sim.velocities();
    CHECK(ps.size() == 30);
    // bounded: flock stays near its center (soft boundary + cohesion)
    for (const auto& q : ps) {
        CHECK(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z));
        CHECK(std::hypot(q.x - p.center.x, q.z - p.center.z) < p.radius * 2.5f);
        CHECK(q.y > 0.f);
    }
    // separation: no two boids collapse onto each other
    float minD = 1e9f;
    for (std::size_t i = 0; i < ps.size(); ++i)
        for (std::size_t j = i + 1; j < ps.size(); ++j) {
            const Vec3 d = ps[i] - ps[j];
            minD = std::min(minD, std::sqrt(dot3(d, d)));
        }
    CHECK(minD > 0.05f);
    // speeds stay in a sane band (cruise speed enforced)
    for (const auto& v : vs) {
        const float s = std::sqrt(dot3(v, v));
        CHECK(s > p.speed * 0.3f && s < p.speed * 2.0f);
    }
    // cohesion: flock centroid stays near the center
    Vec3 cen{0.f, 0.f, 0.f};
    for (const auto& q : ps) cen = cen + q;
    cen = cen * (1.f / ps.size());
    CHECK(std::hypot(cen.x - p.center.x, cen.z - p.center.z) < p.radius * 2.f);
    // determinism at fixed dt
    BoidsSim a(p), b(p);
    for (int i = 0; i < 120; ++i) { a.step(1.f / 60.f, wf, i / 60.f); b.step(1.f / 60.f, wf, i / 60.f); }
    for (std::size_t i = 0; i < a.positions().size(); ++i)
        CHECK(a.positions()[i].x == b.positions()[i].x && a.positions()[i].z == b.positions()[i].z);
}

void testCurlNoiseInsects() {
    Noise n(3u);
    // curl field is bounded and varies smoothly (finite difference divergence ~ 0)
    const Vec3 c0 = curlNoise3(n, 1.f, 2.f, 3.f);
    const Vec3 c1 = curlNoise3(n, 1.05f, 2.f, 3.f);
    CHECK(std::isfinite(c0.x) && std::isfinite(c0.y) && std::isfinite(c0.z));
    CHECK(std::fabs(c0.x) < 25.f && std::fabs(c0.y) < 25.f && std::fabs(c0.z) < 25.f);
    CHECK(std::hypot(c1.x - c0.x, c1.y - c0.y, c1.z - c0.z) < 1.5f); // smooth
    // divergence of the curl field ~ 0 (div curl = 0 analytically): numeric check stays small
    const float e = 0.1f;
    const auto divAt = [&](float x, float y, float z) {
        return (curlNoise3(n, x + e, y, z).x - curlNoise3(n, x - e, y, z).x) / (2 * e) +
               (curlNoise3(n, x, y + e, z).y - curlNoise3(n, x, y - e, z).y) / (2 * e) +
               (curlNoise3(n, x, y, z + e).z - curlNoise3(n, x, y, z - e).z) / (2 * e);
    };
    CHECK(std::fabs(divAt(1.f, 2.f, 3.f)) < 2.0f);
    CHECK(std::fabs(divAt(-4.f, 1.f, 5.f)) < 2.0f);
    // swarm stays bounded and near the ground layer
    InsectSwarmParams ip;
    ip.seed = 12u;
    ip.count = 40;
    ip.center = {0.f, 2.f, 0.f};
    ip.radius = 8.f;
    InsectSwarm swarm(ip);
    WindField wf;
    for (int i = 0; i < 600; ++i) swarm.step(1.f / 60.f, wf, i / 60.f);
    for (const auto& q : swarm.positions()) {
        CHECK(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z));
        CHECK(std::hypot(q.x - ip.center.x, q.z - ip.center.z) < ip.radius * 3.f);
        CHECK(q.y >= 0.1f);
    }
}

void testAmbientParticles() {
    // biome -> kind mapping
    CHECK(ambientKindFor(2, 0.2f) == AmbientKind::Sand);    // desert
    CHECK(ambientKindFor(9, 0.2f) == AmbientKind::Snow);    // snow
    CHECK(ambientKindFor(4, 0.8f) == AmbientKind::Seed);    // wet forest
    CHECK(ambientKindFor(4, 0.3f) == AmbientKind::Leaf);    // dry forest
    CHECK(ambientKindFor(3, 0.7f) == AmbientKind::Pollen);  // humid grassland
    CHECK(ambientKindFor(0, 0.8f) == AmbientKind::Spray);   // coast
    // sim: constant alive count with respawn, bounded box, snow sinks
    AmbientParticleParams p;
    p.seed = 13u;
    p.count = 80;
    p.center = {0.f, 10.f, 0.f};
    p.halfExtent = {20.f, 8.f, 20.f};
    p.kind = AmbientKind::Snow;
    p.fallSpeed = 0.5f;
    AmbientParticleSim sim(p);
    WindField wf;
    CHECK(sim.alive() == 80);
    for (int i = 0; i < 300; ++i) sim.step(1.f / 30.f, wf, i / 30.f);
    CHECK(sim.alive() == 80); // respawn keeps the count
    double vySum = 0;
    for (std::size_t i = 0; i < sim.positions().size(); ++i) {
        const Vec3& q = sim.positions()[i];
        CHECK(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z));
        CHECK(std::fabs(q.x - p.center.x) <= p.halfExtent.x + 1.f);
        CHECK(std::fabs(q.z - p.center.z) <= p.halfExtent.z + 1.f);
        CHECK(q.y <= p.center.y + p.halfExtent.y + 1.f && q.y > -3.f);
        vySum += sim.velocities()[i].y;
    }
    CHECK(vySum / sim.positions().size() < 0.f); // snow sinks on average
    // determinism
    AmbientParticleSim a(p), b(p);
    for (int i = 0; i < 60; ++i) { a.step(1.f / 30.f, wf, i / 30.f); b.step(1.f / 30.f, wf, i / 30.f); }
    for (std::size_t i = 0; i < a.positions().size(); ++i)
        CHECK(a.positions()[i].x == b.positions()[i].x && a.positions()[i].y == b.positions()[i].y);
}

} // namespace

void runProceduralLifegenTests() {
    testWindField();
    testBoids();
    testCurlNoiseInsects();
    testAmbientParticles();
}
