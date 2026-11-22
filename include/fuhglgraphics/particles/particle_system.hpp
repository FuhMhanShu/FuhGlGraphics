#pragma once

/// @file particle_system.hpp
/// @brief G5 — GPU particle system on FuhGLCore's compute pattern (like GpuCuller, F34):
/// emitter + integrate compute shaders over an SSBO of particle structs, rendered as
/// instanced camera-facing billboards (the SSBO is simultaneously the divisor-1 instance
/// attribute buffer). GL >= 4.3 uses the compute path (gpu() == true); on GL 3.3–4.2 the
/// same simulation runs on the CPU (CpuSim) and is uploaded as instance data — F39-style
/// graceful fallback, toggleable via render::Effect::Particles.
///
/// Integration into the G3 renderer: Scene::fillFrame() collects ParticleEmitterComponent
/// systems into FrameData::particleSystems; DeferredRenderer::render(..., &particleSystems)
/// draws them in a "particles" post pass (additive or alpha blend, depth-tested against the
/// G-buffer depth, soft particle fade) between the lighting and the TAA pass.
///
/// Conventions like the rest of the repo: PIMPL, movable, no exceptions (bool + log),
/// namespace fuhglgraphics::particles.

#include <fuhgl/cam/camera.hpp>
#include <fuhgl/math.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace fuhglgraphics::particles {

/// @brief Blend mode of the billboard pass.
enum class BlendMode {
    Additive = 0, ///< glBlendFunc(ONE, ONE) — sparks, fire, glow
    Alpha = 1,    ///< glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA) — smoke, fog
};

/// @brief Emitter parameters (all in world units / seconds).
struct EmitterConfig {
    fuhgl::math::Vec3 position{0.f, 1.f, 0.f};   ///< emitter origin
    fuhgl::math::Vec3 direction{0.f, 1.f, 0.f};  ///< emission cone axis (normalized on use)
    float speed = 2.f;        ///< initial speed along the cone
    float spread = 0.4f;      ///< random velocity offset (cone width factor)
    float rate = 80.f;        ///< emitted particles per second
    float lifetime = 2.f;     ///< seconds (each particle gets lifetime * [0.5, 1.5])
    float size = 0.12f;       ///< billboard world size at birth
    float grow = 0.25f;       ///< size growth per second of age (smoke expands)
    fuhgl::math::Vec3 color{1.f, 0.75f, 0.35f};
    float alpha = 0.7f;
    float gravity = -0.5f;    ///< y acceleration applied in integrate
    fuhgl::math::Vec3 wind{0.f, 0.f, 0.f}; ///< constant acceleration
    float drag = 0.5f;        ///< exponential velocity damping (1/s)
    unsigned seed = 7u;       ///< deterministic per-particle randomness
    BlendMode blend = BlendMode::Additive;
};

/// @brief Pure CPU simulation core — the same math as the integrate/emit compute shaders,
/// without any GL dependency (unit-testable headless). Used as the GL < 4.3 fallback.
struct CpuSim {
    struct Particle {
        fuhgl::math::Vec3 pos{0.f}, vel{0.f};
        float life = 0.f, maxLife = 1.f, size = 0.f;
        fuhgl::math::Vec3 color{1.f};
        float alpha = 1.f;
    };
    std::vector<Particle> particles; ///< alive particles (life > 0), compacted on integrate
    float emitAcc = 0.f;             ///< fractional emission accumulator
    unsigned rng = 0;

    explicit CpuSim(unsigned seed = 1u) : rng(seed ? seed : 1u) {}
    /// @brief Emits rate*dt particles (accumulated fractionally) at the emitter.
    void emit(const EmitterConfig& cfg, float dt);
    /// @brief Advances all particles by dt (gravity, wind, drag, aging) and compacts dead ones.
    void integrate(const EmitterConfig& cfg, float dt);
    int alive() const;

private:
    float nextRand(); ///< deterministic LCG in [0, 1)
};

/// @brief One GPU/CPU particle system (PIMPL, movable). create() needs a current GL
/// context: on GL >= 4.3 it builds the SSBO + compute shaders + instanced-billboard
/// program (gpu() == true); otherwise it falls back to the CPU sim + a dynamic instance
/// VBO (gpu() == false). update(dt) emits + integrates; the DeferredRenderer's
/// "particles" pass calls beginPass()/drawPass().
class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;
    ParticleSystem(ParticleSystem&&) noexcept;
    ParticleSystem& operator=(ParticleSystem&&) noexcept;

    /// @brief Creates the system for up to `maxParticles` particles (SSBO/VBO + shaders).
    bool create(int maxParticles, const EmitterConfig& cfg);
    bool isReady() const { return ready_; }
    bool gpu() const { return gpu_; }        ///< true: compute path (GL >= 4.3)
    int maxParticles() const { return maxParticles_; }

    void setEmitter(const EmitterConfig& cfg);
    const EmitterConfig& emitter() const { return emitter_; }

    /// @brief Emits + integrates one step (GPU: compute dispatch; CPU: CpuSim).
    void update(float dt);
    /// @brief Alive particle count (exact on CPU; GPU reads the atomic counter back).
    int aliveCount();

    /// @brief Binds the billboard program and sets the per-pass uniforms (call once per
    /// frame inside the renderer's particles pass, before drawPass()).
    void beginPass(const fuhgl::Camera& camera, float aspect, int viewportW, int viewportH,
                   int depthTexUnit) const;
    /// @brief Draws all particles as instanced billboards (blend func set by the caller
    /// according to blendMode()).
    void drawPass() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    EmitterConfig emitter_;
    int maxParticles_ = 0;
    bool ready_ = false;
    bool gpu_ = false;
};

} // namespace fuhglgraphics::particles
