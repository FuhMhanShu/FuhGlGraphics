#pragma once

/// @file particle_glsl.hpp
/// @brief G5 particle shaders: instanced billboard (VS+FS, GLSL 330) and the emit /
/// integrate compute shaders (GLSL 430, GL >= 4.3 path). SSBO layout per particle:
/// a = pos.xyz + size, b = vel.xyz + life, c = color.rgb + alpha, d = maxLife + seed.

namespace fuhglgraphics::particles {

constexpr const char* kBillboardVert = R"GLSL(#version 330 core
layout(location = 0) in vec2 aCorner;     // quad corner in [-1,1]^2
layout(location = 4) in vec4 aPosSize;    // xyz = world pos, w = current size
layout(location = 5) in vec4 aVelLife;    // xyz = velocity, w = remaining life
layout(location = 6) in vec4 aColorAlpha; // rgb = color, a = alpha
layout(location = 7) in vec4 aMaxLife;    // x = max life
uniform mat4 uViewProj;
uniform mat4 uInvView; // camera view inverse: rotation part gives the billboard axes
out vec2 vUV;
out vec4 vColor;
out float vLifeFrac;
void main() {
    vLifeFrac = clamp(aVelLife.w / max(aMaxLife.x, 1e-4), 0.0, 1.0);
    if (aVelLife.w <= 0.0) { // dead particle -> degenerate, clipped
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vUV = aCorner; vColor = vec4(0.0);
        return;
    }
    vec3 right = vec3(uInvView[0].xyz);
    vec3 up = vec3(uInvView[1].xyz);
    vec3 world = aPosSize.xyz + (right * aCorner.x + up * aCorner.y) * aPosSize.w;
    gl_Position = uViewProj * vec4(world, 1.0);
    vUV = aCorner;
    vColor = aColorAlpha;
}
)GLSL";

constexpr const char* kBillboardFrag = R"GLSL(#version 330 core
in vec2 vUV;
in vec4 vColor;
in float vLifeFrac;
uniform sampler2D uSceneDepth; // unit set by beginPass (the G-buffer depth)
uniform vec2 uViewport;        // internal render resolution
uniform float uCsZNear, uCsZFar;
uniform int uBlendMode;        // 0 = additive (premultiplied), 1 = alpha
out vec4 FragColor;
float linearize(float d) {
    return 2.0 * uCsZNear * uCsZFar / max(uCsZFar + uCsZNear - (2.0 * d - 1.0) * (uCsZFar - uCsZNear), 1e-6);
}
void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    float soft = smoothstep(1.0, 0.15, r);                 // radial falloff
    float lifeFade = smoothstep(0.0, 0.1, vLifeFrac) * smoothstep(1.0, 0.6, vLifeFrac);
    // soft particles: fade when geometry is close in front of the billboard
    float sceneD = linearize(texture(uSceneDepth, gl_FragCoord.xy / uViewport).r);
    float partD = linearize(gl_FragCoord.z);
    float depthFade = clamp((sceneD - partD) / 0.3, 0.0, 1.0);
    float a = vColor.a * soft * lifeFade * depthFade;
    if (a <= 0.001) discard;
    if (uBlendMode == 0) FragColor = vec4(vColor.rgb * a, a); // premultiplied additive
    else FragColor = vec4(vColor.rgb, a);
}
)GLSL";

// --- compute (GL 4.3+): emit -------------------------------------------------------
constexpr const char* kEmitComp = R"GLSL(#version 430 core
layout(local_size_x = 64) in;
struct Particle { vec4 a, b, c, d; }; // a=pos.xyz+size, b=vel.xyz+life, c=color.rgb+alpha, d=maxLife+seed
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer Counter { uint emitted; uint alive; };
uniform int uEmitCount;
uniform uint uMax;
uniform vec3 uPos; uniform vec3 uDir; uniform float uSpeed, uSpread, uLifetime, uSize;
uniform vec3 uColor; uniform float uAlpha;
uniform uint uSeed;
float hash(uint n) { // deterministic pseudo-random in [0,1)
    n = n * 1664525u + 1013904223u;
    n ^= n >> 16; n *= 2246822519u; n ^= n >> 13;
    return float(n & 0xFFFFFFu) / float(0x1000000u);
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (int(i) >= uEmitCount) return;
    uint idx = atomicAdd(emitted, 1u);
    if (idx >= uMax) return;
    atomicAdd(alive, 1u);
    uint s = uSeed + idx * 7919u + i;
    float r1 = hash(s), r2 = hash(s + 1u), r3 = hash(s + 2u), r4 = hash(s + 3u);
    vec3 jitter = vec3(r1 - 0.5, r2 - 0.5, r3 - 0.5) * 2.0 * uSpread;
    vec3 vel = uDir * uSpeed + jitter * uSpeed;
    float life = uLifetime * (0.5 + r4);
    p[idx].a = vec4(uPos, uSize);
    p[idx].b = vec4(vel, life);
    p[idx].c = vec4(uColor, uAlpha);
    p[idx].d = vec4(life, float(s), 0.0, 0.0);
}
)GLSL";

// --- compute (GL 4.3+): integrate ----------------------------------------------------
constexpr const char* kIntegrateComp = R"GLSL(#version 430 core
layout(local_size_x = 64) in;
struct Particle { vec4 a, b, c, d; };
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer Counter { uint emitted; uint alive; };
uniform float uDt;
uniform uint uMax;
uniform float uGravity; uniform vec3 uWind; uniform float uDrag;
uniform float uSize0, uGrow;
void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uMax) return;
    if (p[idx].b.w <= 0.0) return; // dead
    float life = p[idx].b.w - uDt;
    if (life <= 0.0) atomicAdd(alive, 0xFFFFFFFFu); // died this step
    vec3 vel = p[idx].b.xyz;
    vel += (uWind + vec3(0.0, uGravity, 0.0)) * uDt;
    vel *= exp(-uDrag * uDt);
    vec3 pos = p[idx].a.xyz + vel * uDt;
    float age = p[idx].d.x - life;
    p[idx].a = vec4(pos, uSize0 + uGrow * age);
    p[idx].b = vec4(vel, max(life, 0.0));
}
)GLSL";

} // namespace fuhglgraphics::particles
