#include "fuhglgraphics/particles/particle_system.hpp"

#include "particle_glsl.hpp"

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render/material.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fuhglgraphics::particles {

// --- CpuSim (pure, no GL) -------------------------------------------------------------

float CpuSim::nextRand() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(rng & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

void CpuSim::emit(const EmitterConfig& cfg, float dt) {
    emitAcc += cfg.rate * dt;
    const int n = static_cast<int>(emitAcc);
    emitAcc -= static_cast<float>(n);
    const fuhgl::math::Vec3 dir = fuhgl::math::normalize(cfg.direction);
    for (int i = 0; i < n; ++i) {
        const float r1 = nextRand(), r2 = nextRand(), r3 = nextRand(), r4 = nextRand();
        const float r5 = nextRand();
        const fuhgl::math::Vec3 jitter{(r1 - 0.5f) * 2.f, (r2 - 0.5f) * 2.f, (r3 - 0.5f) * 2.f};
        Particle p;
        p.pos = cfg.position;
        p.vel = dir * cfg.speed + jitter * (cfg.spread * cfg.speed);
        p.maxLife = cfg.lifetime * (0.5f + r4);
        p.life = p.maxLife;
        p.size = cfg.size;
        p.color = cfg.color;
        p.alpha = cfg.alpha;
        p.seed = r5 * 1000.f;
        particles.push_back(p);
    }
}

void CpuSim::integrate(const EmitterConfig& cfg, float dt) {
    for (Particle& p : particles) {
        if (p.life <= 0.f) continue;
        p.life -= dt;
        p.vel += (cfg.wind + fuhgl::math::Vec3{0.f, cfg.gravity, 0.f}) * dt;
        p.vel = p.vel * std::exp(-cfg.drag * dt);
        p.pos = p.pos + p.vel * dt;
        p.size = cfg.size + cfg.grow * (p.maxLife - p.life);
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(),
                                   [](const Particle& p) { return p.life <= 0.f; }),
                    particles.end());
}

int CpuSim::alive() const { return static_cast<int>(particles.size()); }

// --- ParticleSystem -------------------------------------------------------------------

struct ParticleSystem::Impl {
    // GPU path (GL >= 4.3)
    fuhgl::Buffer ssbo{fuhgl::Buffer::Target::ShaderStorage};   // binding 0: Particle[]
    fuhgl::Buffer counter{fuhgl::Buffer::Target::ShaderStorage}; // binding 1: uint count
    fuhgl::Program emitProg, integrateProg;
    // CPU fallback path
    CpuSim sim;
    fuhgl::Buffer instanceVbo{fuhgl::Buffer::Target::Array};
    // shared render path
    fuhgl::render::Material billboardMat;
    fuhgl::VertexArray vaoGpu, vaoCpu;
    fuhgl::Buffer quadVbo{fuhgl::Buffer::Target::Array};
    float emitAcc = 0.f;
    unsigned frameSeed = 0;
};

ParticleSystem::ParticleSystem() : impl_(std::make_unique<Impl>()) {}
ParticleSystem::~ParticleSystem() = default;
ParticleSystem::ParticleSystem(ParticleSystem&&) noexcept = default;
ParticleSystem& ParticleSystem::operator=(ParticleSystem&&) noexcept = default;

bool ParticleSystem::create(int maxParticles, const EmitterConfig& cfg) {
    if (maxParticles <= 0) return false;
    emitter_ = cfg;
    maxParticles_ = maxParticles;
    Impl& im = *impl_;
    im.sim = CpuSim(cfg.seed);

    const fuhgl::gl::Version v = fuhgl::gl::version();
    gpu_ = v.atLeast(4, 3) && fuhgl::gl::functions().glDispatchCompute != nullptr;

    // shared: quad corners (TRIANGLE_STRIP) + billboard program
    const float quad[8] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    im.quadVbo.create(fuhgl::Buffer::Target::Array);
    im.quadVbo.bind();
    im.quadVbo.setData(sizeof(quad), quad, fuhgl::Buffer::Usage::StaticDraw);

    if (!im.billboardMat.create()) return false;
    {
        fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
        if (!vs.compile(kBillboardVert) || !fs.compile(kBillboardFrag)) {
            std::fprintf(stderr, "ParticleSystem: billboard shader: %s%s\n", vs.infoLog().c_str(),
                         fs.infoLog().c_str());
            return false;
        }
        im.billboardMat.program().attach(vs);
        im.billboardMat.program().attach(fs);
        if (!im.billboardMat.program().link()) {
            std::fprintf(stderr, "ParticleSystem: billboard link: %s\n", im.billboardMat.program().infoLog().c_str());
            return false;
        }
    }

    const std::size_t bytes = static_cast<std::size_t>(maxParticles) * 4 * sizeof(float) * 4; // 4x vec4
    if (gpu_) {
        im.ssbo.create(fuhgl::Buffer::Target::ShaderStorage);
        im.ssbo.bind();
        im.ssbo.setData(static_cast<fuhgl::gl::GLsizeiptr>(bytes), nullptr, fuhgl::Buffer::Usage::DynamicDraw);
        im.counter.create(fuhgl::Buffer::Target::ShaderStorage);
        im.counter.bind();
        const unsigned zeros[2] = {0, 0}; // emitted, alive
        im.counter.setData(sizeof(zeros), zeros, fuhgl::Buffer::Usage::DynamicDraw);

        auto compileComp = [](fuhgl::Program& prog, const char* src, const char* name) {
            fuhgl::Shader cs(fuhgl::Shader::Type::Compute);
            if (!cs.compile(src)) {
                std::fprintf(stderr, "ParticleSystem: %s compute: %s\n", name, cs.infoLog().c_str());
                return false;
            }
            prog.create();
            prog.attach(cs);
            if (!prog.link()) {
                std::fprintf(stderr, "ParticleSystem: %s link: %s\n", name, prog.infoLog().c_str());
                return false;
            }
            return true;
        };
        if (!compileComp(im.emitProg, kEmitComp, "emit") || !compileComp(im.integrateProg, kIntegrateComp, "integrate"))
            return false;
        im.emitProg.bindShaderStorageBlock("Particles", 0);
        im.emitProg.bindShaderStorageBlock("Counter", 1);
        im.integrateProg.bindShaderStorageBlock("Particles", 0);

        // VAO: quad at attrib 0, SSBO as divisor-1 instance attributes 4..7 (like GpuCuller)
        im.vaoGpu.create();
        im.vaoGpu.bind();
        fuhgl::gl::functions().glBindBuffer(fuhgl::gl::GL_ARRAY_BUFFER, im.quadVbo.id());
        im.vaoGpu.setAttrib(0, 2, fuhgl::gl::GL_FLOAT, false, 2 * sizeof(float), 0);
        im.vaoGpu.enableAttrib(0);
        fuhgl::gl::functions().glBindBuffer(fuhgl::gl::GL_ARRAY_BUFFER, im.ssbo.id());
        for (int k = 0; k < 4; ++k) {
            im.vaoGpu.setAttrib(4 + k, 4, fuhgl::gl::GL_FLOAT, false, 4 * 4 * sizeof(float),
                                static_cast<fuhgl::gl::GLintptr>(k * 4 * sizeof(float)));
            im.vaoGpu.enableAttrib(4 + k);
            im.vaoGpu.setDivisor(4 + k, 1);
        }
    } else {
        // CPU fallback: dynamic instance VBO with the same 4x vec4 layout
        im.instanceVbo.create(fuhgl::Buffer::Target::Array);
        im.instanceVbo.bind();
        im.instanceVbo.setData(static_cast<fuhgl::gl::GLsizeiptr>(bytes), nullptr, fuhgl::Buffer::Usage::StreamDraw);
        im.vaoCpu.create();
        im.vaoCpu.bind();
        fuhgl::gl::functions().glBindBuffer(fuhgl::gl::GL_ARRAY_BUFFER, im.quadVbo.id());
        im.vaoCpu.setAttrib(0, 2, fuhgl::gl::GL_FLOAT, false, 2 * sizeof(float), 0);
        im.vaoCpu.enableAttrib(0);
        fuhgl::gl::functions().glBindBuffer(fuhgl::gl::GL_ARRAY_BUFFER, im.instanceVbo.id());
        for (int k = 0; k < 4; ++k) {
            im.vaoCpu.setAttrib(4 + k, 4, fuhgl::gl::GL_FLOAT, false, 4 * 4 * sizeof(float),
                                static_cast<fuhgl::gl::GLintptr>(k * 4 * sizeof(float)));
            im.vaoCpu.enableAttrib(4 + k);
            im.vaoCpu.setDivisor(4 + k, 1);
        }
    }
    fuhgl::VertexArray::unbind();
    ready_ = true;
    return true;
}

void ParticleSystem::setEmitter(const EmitterConfig& cfg) { emitter_ = cfg; }

void ParticleSystem::update(float dt) {
    if (!ready_ || dt <= 0.f) return;
    time_ += dt; // G35: drives the uTime flame-flicker uniform in beginPass
    Impl& im = *impl_;
    if (gpu_) {
        im.emitAcc += emitter_.rate * dt;
        const int n = static_cast<int>(im.emitAcc);
        im.emitAcc -= static_cast<float>(n);
        auto& f = fuhgl::gl::functions();
        // recycle all slots when the next emission would overflow the SSBO
        unsigned count[2] = {0, 0};
        im.counter.bind();
        im.counter.getSubData(0, sizeof(count), count);
        if (count[0] + static_cast<unsigned>(n) > static_cast<unsigned>(maxParticles_)) {
            const unsigned zeros[2] = {0, 0};
            im.counter.setSubData(0, sizeof(zeros), zeros);
        }
        im.ssbo.bindBase(0);
        im.counter.bindBase(1);
        im.emitProg.use();
        im.emitProg.setUniform("uEmitCount", n);
        im.emitProg.setUniform("uMax", static_cast<unsigned>(maxParticles_));
        im.emitProg.setUniform("uPos", emitter_.position);
        im.emitProg.setUniform("uDir", fuhgl::math::normalize(emitter_.direction));
        im.emitProg.setUniform("uSpeed", emitter_.speed);
        im.emitProg.setUniform("uSpread", emitter_.spread);
        im.emitProg.setUniform("uLifetime", emitter_.lifetime);
        im.emitProg.setUniform("uSize", emitter_.size);
        im.emitProg.setUniform("uColor", emitter_.color);
        im.emitProg.setUniform("uAlpha", emitter_.alpha);
        im.emitProg.setUniform("uSeed", emitter_.seed + im.frameSeed);
        fuhgl::gl::dispatchCompute(static_cast<fuhgl::gl::GLuint>((n + 63) / 64), 1, 1);
        fuhgl::gl::memoryBarrier(fuhgl::gl::GL_SHADER_STORAGE_BARRIER_BIT | 0x1000u); // + ATOMIC_COUNTER bit
        im.integrateProg.use();
        im.integrateProg.setUniform("uDt", dt);
        im.integrateProg.setUniform("uMax", static_cast<unsigned>(maxParticles_));
        im.integrateProg.setUniform("uGravity", emitter_.gravity);
        im.integrateProg.setUniform("uWind", emitter_.wind);
        im.integrateProg.setUniform("uDrag", emitter_.drag);
        im.integrateProg.setUniform("uSize0", emitter_.size);
        im.integrateProg.setUniform("uGrow", emitter_.grow);
        fuhgl::gl::dispatchCompute(static_cast<fuhgl::gl::GLuint>((maxParticles_ + 63) / 64), 1, 1);
        fuhgl::gl::memoryBarrier(fuhgl::gl::GL_SHADER_STORAGE_BARRIER_BIT | fuhgl::gl::GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        ++im.frameSeed;
        (void)f;
    } else {
        im.sim.emit(emitter_, dt);
        im.sim.integrate(emitter_, dt);
        // upload alive particles into the instance VBO (4x vec4 per particle)
        std::vector<float> data;
        data.reserve(im.sim.particles.size() * 16);
        for (const CpuSim::Particle& p : im.sim.particles) {
            const float a[4] = {p.pos.x, p.pos.y, p.pos.z, p.size};
            const float b[4] = {p.vel.x, p.vel.y, p.vel.z, p.life};
            const float c[4] = {p.color.x, p.color.y, p.color.z, p.alpha};
            const float d[4] = {p.maxLife, p.seed, 0.f, 0.f};
            data.insert(data.end(), a, a + 4);
            data.insert(data.end(), b, b + 4);
            data.insert(data.end(), c, c + 4);
            data.insert(data.end(), d, d + 4);
        }
        im.instanceVbo.bind();
        const auto bytes = static_cast<fuhgl::gl::GLsizeiptr>(data.size() * sizeof(float));
        const auto cap = static_cast<fuhgl::gl::GLsizeiptr>(maxParticles_) * 64;
        if (bytes > cap) return;
        im.instanceVbo.setData(cap, nullptr, fuhgl::Buffer::Usage::StreamDraw); // orphan
        if (!data.empty()) im.instanceVbo.setSubData(0, bytes, data.data());
    }
}

int ParticleSystem::aliveCount() {
    if (!ready_) return 0;
    if (!gpu_) return impl_->sim.alive();
    unsigned count[2] = {0, 0};
    impl_->counter.bind();
    impl_->counter.getSubData(0, sizeof(count), count);
    return static_cast<int>(count[1]);
}

void ParticleSystem::beginPass(const fuhgl::Camera& camera, float aspect, int viewportW, int viewportH,
                               int depthTexUnit) const {
    fuhgl::Program& p = impl_->billboardMat.program();
    impl_->billboardMat.bind();
    const fuhgl::math::Mat4 view = camera.view();
    p.setUniform("uViewProj", camera.viewProj(aspect));
    p.setUniform("uInvView", view.inverse());
    p.setUniform("uViewport", fuhgl::math::Vec2(static_cast<float>(viewportW), static_cast<float>(viewportH)));
    p.setUniform("uSceneDepth", depthTexUnit);
    p.setUniform("uCsZNear", camera.zNear());
    p.setUniform("uCsZFar", camera.zFar());
    p.setUniform("uBlendMode", emitter_.blend == BlendMode::Additive ? 0 : 1);
    p.setUniform("uFlame", emitter_.flame ? 1 : 0); // G35 flame mode
    p.setUniform("uTime", time_);
}

void ParticleSystem::drawPass() const {
    if (!ready_) return;
    if (gpu_) {
        impl_->vaoGpu.bind();
        fuhgl::gl::drawArraysInstanced(fuhgl::gl::Primitive::TriangleStrip, 0, 4, maxParticles_);
    } else {
        impl_->vaoCpu.bind();
        fuhgl::gl::drawArraysInstanced(fuhgl::gl::Primitive::TriangleStrip, 0, 4, impl_->sim.alive());
    }
    fuhgl::VertexArray::unbind();
}

} // namespace fuhglgraphics::particles
