// Unit tests for the G5 particle system's pure CPU core (CpuSim — the same math as the
// emit/integrate compute shaders, no GL context needed): emission rate accumulation,
// integration (gravity/wind/drag/aging), compaction of dead particles, determinism.

#include "test_framework.hpp"

#include <fuhglgraphics/particles/particle_system.hpp>

#include <cmath>

using namespace fuhglgraphics::particles;

namespace {

void testCpuSimEmit() {
    EmitterConfig cfg;
    cfg.position = {1.f, 2.f, 3.f};
    cfg.direction = {0.f, 1.f, 0.f};
    cfg.speed = 2.f;
    cfg.rate = 100.f;
    cfg.lifetime = 2.f;
    CpuSim sim(42u);
    CHECK(sim.alive() == 0);
    sim.emit(cfg, 0.5f); // 50 particles
    CHECK(sim.alive() == 50);
    for (const auto& p : sim.particles) {
        CHECK_NEAR(p.pos.x, 1.f, 1e-6);
        CHECK_NEAR(p.pos.y, 2.f, 1e-6);
        CHECK_NEAR(p.pos.z, 3.f, 1e-6);
        CHECK(p.life > 0.f && p.life <= cfg.lifetime * 1.5f + 1e-6);
        CHECK(p.size == cfg.size);
        // velocity along +Y with spread
        CHECK(p.vel.y > 0.f);
    }
    // fractional accumulation: 0.1s at rate 100 -> 10
    CpuSim sim2(1u);
    sim2.emit(cfg, 0.1f);
    CHECK(sim2.alive() == 10);
    // determinism: same seed -> same first particle velocity
    CpuSim a(7u), b(7u);
    a.emit(cfg, 0.1f);
    b.emit(cfg, 0.1f);
    CHECK(a.alive() == b.alive());
    if (!a.particles.empty() && !b.particles.empty())
        CHECK_NEAR(a.particles[0].vel.x, b.particles[0].vel.x, 1e-6);
}

void testCpuSimIntegrate() {
    EmitterConfig cfg;
    cfg.direction = {0.f, 1.f, 0.f};
    cfg.speed = 1.f;
    cfg.spread = 0.f;
    cfg.rate = 10.f;
    cfg.lifetime = 1.f;
    cfg.gravity = -10.f;
    cfg.wind = {1.f, 0.f, 0.f};
    cfg.drag = 0.f;
    cfg.size = 0.1f;
    cfg.grow = 1.f;
    CpuSim sim(3u);
    sim.emit(cfg, 1.f); // 10 particles, life 0.5..1.5
    const int before = sim.alive();
    CHECK(before == 10);
    sim.integrate(cfg, 0.1f);
    CHECK(sim.alive() == before); // none dead yet
    // pos moved up (speed 1 up, gravity -10 -> after 0.1s vel.y = 1 - 1 = 0, pos.y += ~0.95)
    CHECK(sim.particles[0].pos.y > 2.f * 0.9f - 1.f); // moved up from 0
    CHECK(sim.particles[0].pos.x > 0.f);              // wind pushed +x
    // size grew by grow*age
    const float age = sim.particles[0].maxLife - sim.particles[0].life;
    CHECK_NEAR(sim.particles[0].size, cfg.size + cfg.grow * age, 1e-5);
    // run past all lifetimes -> all dead and compacted
    for (int i = 0; i < 40; ++i) sim.integrate(cfg, 0.1f);
    CHECK(sim.alive() == 0);
}

} // namespace

void runParticlesTests() {
    testCpuSimEmit();
    testCpuSimIntegrate();
}
