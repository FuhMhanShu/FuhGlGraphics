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
layout(location = 7) in vec4 aMaxLife;    // x = max life, y = per-particle seed
uniform mat4 uViewProj;
uniform mat4 uInvView; // camera view inverse: rotation part gives the billboard axes
uniform int uFlame;    // G35: 1 = stretch the billboard vertically (flame teardrop in the FS)
out vec2 vUV;
out vec4 vColor;
out float vLifeFrac;
out float vSeed;
void main() {
    vLifeFrac = clamp(aVelLife.w / max(aMaxLife.x, 1e-4), 0.0, 1.0);
    vSeed = aMaxLife.y;
    if (aVelLife.w <= 0.0) { // dead particle -> degenerate, clipped
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vUV = aCorner; vColor = vec4(0.0);
        return;
    }
    vec3 right = vec3(uInvView[0].xyz);
    vec3 up = vec3(uInvView[1].xyz);
    float stretch = (uFlame == 1) ? 1.55 : 1.0; // flames are taller than wide
    vec3 world = aPosSize.xyz + (right * aCorner.x + up * (aCorner.y * stretch)) * aPosSize.w;
    gl_Position = uViewProj * vec4(world, 1.0);
    vUV = aCorner;
    vColor = aColorAlpha;
}
)GLSL";

constexpr const char* kBillboardFrag = R"GLSL(#version 330 core
in vec2 vUV;
in vec4 vColor;
in float vLifeFrac;
in float vSeed;
uniform sampler2D uSceneDepth; // unit set by beginPass (the G-buffer depth)
uniform vec2 uViewport;        // internal render resolution
uniform float uCsZNear, uCsZFar;
uniform int uBlendMode;        // 0 = additive (premultiplied), 1 = alpha
uniform int uFlame;            // G35: 1 = teardrop flame shading instead of a soft round sprite
uniform float uTime;           // G35: simulation time, scrolls the flame turbulence upward
out vec4 FragColor;
float linearize(float d) {
    return 2.0 * uCsZNear * uCsZFar / max(uCsZFar + uCsZNear - (2.0 * d - 1.0) * (uCsZFar - uCsZNear), 1e-6);
}
// --- G35 flame helpers: deterministic 2D value noise + 3-octave fbm -------------------
float hash21(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm2(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += a * vnoise(p);
        p = p * 2.03 + vec2(17.3, 9.1);
        a *= 0.5;
    }
    return v;
}
void main() {
    float lifeFade = smoothstep(0.0, 0.1, vLifeFrac) * smoothstep(1.0, 0.6, vLifeFrac);
    // soft particles: fade when geometry is close in front of the billboard
    float sceneD = linearize(texture(uSceneDepth, gl_FragCoord.xy / uViewport).r);
    float partD = linearize(gl_FragCoord.z);
    float depthFade = clamp((sceneD - partD) / 0.3, 0.0, 1.0);
    if (uFlame == 1) {
        // G35 flame: teardrop mask in billboard space, vUV.y = -1 base .. +1 tip
        float yy = vUV.y * 0.5 + 0.5;              // 0 at the base .. 1 at the tip
        float w = mix(1.0, 0.16, yy);              // teardrop width profile
        float xx = vUV.x / max(w, 0.05);
        // turbulence: noise scrolls upward with time, seeded per particle
        float n = fbm2(vec2(xx * 2.8 + vSeed * 7.13, yy * 2.4 - uTime * 2.6 + vSeed * 3.7));
        // teardrop as an ellipse in normalized flame space: rounded base, pointed tip at yy ~ 0.94
        float cy = 0.32, ry = 0.62;
        float body = length(vec2(xx, (yy - cy) / ry));
        float shape = smoothstep(1.0, 0.25, body + (n - 0.5) * 0.8);
        // heat: hottest in the core near the base, flickering tongues higher up
        float heat = clamp(1.38 - yy * 1.2 + (n - 0.5) * 0.5, 0.0, 1.0);
        vec3 col = mix(vec3(0.30, 0.03, 0.0), vec3(1.0, 0.10, 0.0), smoothstep(0.0, 0.28, heat));
        col = mix(col, vec3(1.0, 0.45, 0.06), smoothstep(0.28, 0.62, heat));
        col = mix(col, vec3(1.0, 0.92, 0.70), smoothstep(0.62, 0.92, heat));
        col *= mix(vec3(1.0), vColor.rgb, 0.4);   // emitter tint
        float a = shape * vColor.a * lifeFade * depthFade;
        if (a <= 0.001) discard;
        if (uBlendMode == 0) FragColor = vec4(col * a, a); // premultiplied additive
        else FragColor = vec4(col * (0.8 + 0.2 * n), a);
        return;
    }
    float r = length(vUV);
    if (r > 1.0) discard;
    float soft = smoothstep(1.0, 0.15, r);                 // radial falloff
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
