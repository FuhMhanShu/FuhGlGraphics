#pragma once

// Internal GLSL for the G3 deferred renderer (fuhglgraphics::render::DeferredRenderer).
// All shaders are GLSL 330 core (GL 3.3 baseline). Structure:
//   G-buffer pass (MRT, 6 color attachments + depth) -> lighting pass (HDR) ->
//   post graph: TAA (motion vectors + history) -> FSR1-style upscale (EASU+RCAS) ->
//   ACES tonemap into the LDR output.

#include <fuhgl/gl/shader.hpp>
#include <fuhgl/render/material.hpp>

#include <cstdio>
#include <string>

namespace fuhglgraphics::render {

// --- G-buffer geometry pass ---------------------------------------------------------
constexpr const char* kGBufferVert = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;
layout(location = 4) in mat4 aInstanceModel; // instancing (unused when uInstanced == 0)
uniform mat4 uModel;
uniform mat4 uViewProj; // includes the TAA subpixel jitter when enabled
uniform int uInstanced;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out vec2 vUV;
void main() {
    mat4 model = (uInstanced == 1) ? (uModel * aInstanceModel) : uModel;
    vec4 wp = model * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(model) * aNormal;
    vColor = aColor;
    vUV = aUV;
    gl_Position = uViewProj * wp;
}
)GLSL";

constexpr const char* kGBufferFrag = R"GLSL(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in vec2 vUV;
uniform vec3 uAlbedo;
uniform vec3 uEmissive;
uniform int uUseTex;
uniform sampler2D uMatTex;      // unit kTextureUnitAlbedo (3)
uniform int uUseNormalMap;
uniform sampler2D uNormalTex;   // unit kTextureUnitNormalMap (4)
uniform vec3 uCamPos;           // POM: camera position for the view ray
uniform int uUseHeightMap;     // POM: parallax occlusion mapping on the material's height map
uniform sampler2D uHeightTex;  // unit kTextureUnitHeightMap (5)
uniform float uParallaxScale;  // POM depth scale
uniform int uParallaxLayers;   // POM linear search steps
uniform float uMetallic;
uniform float uRoughness;
uniform float uAo;
uniform float uSss;            // G33: subsurface scattering factor 0..1 -> oPbr.a
uniform uint uMaterialId;      // -> GBufferAttachment::MaterialId
uniform mat4 uPrevViewProj;     // previous frame's (jittered) view-projection, for motion vectors
uniform vec2 uResolution;       // internal G-buffer resolution in pixels
layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oPbr;
layout(location = 3) out vec4 oEmissive;
layout(location = 4) out vec2 oMotion;
layout(location = 5) out vec4 oMatId;
void main() {
    vec2 uv = vUV;
    vec3 N = normalize(vNormal);
    // tangent frame from screen-space derivatives (no tangent attribute needed)
    vec3 dp1 = dFdx(vWorldPos);
    vec3 dp2 = dFdy(vWorldPos);
    vec2 duv1 = dFdx(vUV);
    vec2 duv2 = dFdy(vUV);
    vec3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 B = normalize(-dp1 * duv2.x + dp2 * duv1.x);
    if (uUseHeightMap == 1) {
        // parallax occlusion mapping (POM): march the view ray through the height layers
        // in tangent space, then sample albedo/normal at the offset UV (mirrors
        // render/parallax.hpp on the CPU)
        vec3 V = normalize(uCamPos - vWorldPos);
        vec3 viewTS = vec3(dot(T, V), dot(B, V), dot(N, V));
        vec2 p = viewTS.xy / max(viewTS.z, 0.05) * uParallaxScale;
        float layers = float(max(uParallaxLayers, 1));
        vec2 duv = p / layers;
        float layerDepth = 0.0;
        vec2 curUV = uv;
        float h = texture(uHeightTex, curUV).r;
        for (int i = 0; i < 64 && float(i) < layers; ++i) {
            if (layerDepth >= h) break;
            curUV -= duv;
            layerDepth += 1.0 / layers;
            h = texture(uHeightTex, curUV).r;
        }
        uv = curUV;
    }
    vec3 albedo = uAlbedo * vColor;
    if (uUseTex == 1) albedo *= texture(uMatTex, uv).rgb;
    if (uUseNormalMap == 1) {
        vec3 nTS = texture(uNormalTex, uv).rgb * 2.0 - 1.0;
        N = normalize(T * nTS.x + B * nTS.y + N * nTS.z);
    }
    oAlbedo = vec4(albedo, 1.0);
    oNormal = vec4(N * 0.5 + 0.5, 1.0);
    oPbr = vec4(uMetallic, uRoughness, uAo, uSss); // .a = subsurface factor (G33)
    oEmissive = vec4(uEmissive, 1.0);
    vec2 curUV = gl_FragCoord.xy / uResolution;
    vec4 prevClip = uPrevViewProj * vec4(vWorldPos, 1.0);
    vec2 prevUV = prevClip.xy / max(abs(prevClip.w), 1e-5) * 0.5 + 0.5;
    if (prevClip.w < 0.0) prevUV = curUV; // behind the previous camera: no motion
    oMotion = prevUV - curUV;
    oMatId = vec4(float(uMaterialId & 255u), float((uMaterialId >> 8) & 255u),
                  float((uMaterialId >> 16) & 255u), float((uMaterialId >> 24) & 255u)) / 255.0;
}
)GLSL";

// --- G14 GPU skinning: G-buffer vertex shader variant (GLSL 430, matrix-palette SSBO).
// Only compiled when the context is GL >= 4.3; otherwise the renderer uses kGBufferVert
// with the CPU-skinned mesh (G6 fallback). Attributes 0..3 are the standard layout,
// 5 = joints (float4), 6 = weights (float4); the palette SSBO is at binding 0.
constexpr const char* kGBufferVertSkin = R"GLSL(#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;
layout(location = 5) in vec4 aJoints;
layout(location = 6) in vec4 aWeights;
layout(std430, binding = 0) buffer Palette { mat4 uPalette[]; };
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out vec2 vUV;
void main() {
    mat4 skin = aWeights.x * uPalette[int(aJoints.x)]
              + aWeights.y * uPalette[int(aJoints.y)]
              + aWeights.z * uPalette[int(aJoints.z)]
              + aWeights.w * uPalette[int(aJoints.w)];
    vec4 wp = uModel * (skin * vec4(aPos, 1.0));
    vWorldPos = wp.xyz;
    vNormal = mat3(uModel) * mat3(skin) * aNormal;
    vColor = aColor;
    vUV = aUV;
    gl_Position = uViewProj * wp;
}
)GLSL";

// --- G15 SSAO pass: G-buffer normal+depth -> AO factor (R16F, resource "ssao").
// World-space hemisphere sampling: reconstruct the world position from depth, build a
// tangent frame around the normal, rotate the kernel per pixel (deterministic hash), project
// each sample with the (non-jittered) viewProj and compare linearized depths with a range
// check. Writes 1.0 everywhere when Effect::Ssao is off, so the lighting pass can always
// sample the resource. Units: uGNormal 0, uGDepth 1.
constexpr const char* kSsaoFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uGNormal; // unit 0
uniform sampler2D uGDepth;  // unit 1
uniform mat4 uInvViewProj;  // non-jittered camera viewProj inverse
uniform mat4 uViewProj;     // non-jittered camera viewProj (sample projection)
uniform vec2 uResolution;
uniform vec3 uKernel[64];
uniform int uKernelSize;
uniform float uRadius;
uniform float uBias;
uniform float uIntensity;
uniform int uSamples;
uniform float uRotSeed;
uniform int uSsaoEnabled;
uniform float uZNear, uZFar;
out vec4 FragColor;
float linearize(float d) { return 2.0 * uZNear * uZFar / max(uZFar + uZNear - d * (uZFar - uZNear), 1e-6); }
void main() {
    if (uSsaoEnabled != 1) { FragColor = vec4(1.0); return; }
    float depth = texture(uGDepth, vUV).r;
    if (depth >= 0.99999) { FragColor = vec4(1.0); return; } // sky: no AO
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;
    vec3 N = normalize(texture(uGNormal, vUV).rgb * 2.0 - 1.0);
    float rot = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) + uRotSeed) * 43758.5453) * 6.2831853;
    float cr = cos(rot), sr = sin(rot);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    float pLin = linearize(depth);
    float occ = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= uSamples) break;
        vec3 k = uKernel[i];
        k = vec3(k.x * cr - k.y * sr, k.x * sr + k.y * cr, k.z); // per-pixel rotation in the tangent plane
        vec3 sp = P + (T * k.x + B * k.y + N * k.z) * uRadius;
        vec4 clip = uViewProj * vec4(sp, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sceneLin = linearize(texture(uGDepth, suv).r);
        float sampleLin = linearize(clip.z / clip.w * 0.5 + 0.5);
        float range = clamp(1.0 - abs(pLin - sceneLin) / max(uRadius, 1e-4), 0.0, 1.0);
        occ += (sceneLin < sampleLin - uBias) ? range : 0.0; // occluded
    }
    float ao = clamp(1.0 - (occ / max(float(uSamples), 1.0)) * uIntensity, 0.0, 1.0);
    FragColor = vec4(ao, 0.0, 0.0, 1.0);
}
)GLSL";

// --- G31 HBAO pass: G-buffer normal + depth -> horizon-based AO factor (resource "hbao",
// RG16F, .r), runs before lighting next to G15 SSAO; the lighting pass prefers it when
// Effect::Hbao is on. For uDirs tangent-plane directions (per-pixel rotated by uRotSeed)
// the march samples the scene surface at radius*r along each direction (world space,
// projected with uViewProj), takes the max horizon elevation sin(beta) = heightAbove/len
// above the tangent plane (samples with heightAbove <= uBias are ignored) and multiplies
// cos(beta) per direction (cosine-weighted visible hemisphere fraction). Writes 1.0
// everywhere when Effect::Hbao is off. Pure CPU twin: render/hbao.hpp. Units: uGNormal 0,
// uGDepth 1.
constexpr const char* kHbaoFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uGNormal; // unit 0
uniform sampler2D uGDepth;  // unit 1
uniform mat4 uInvViewProj;  // non-jittered camera viewProj inverse (world reconstruction)
uniform mat4 uViewProj;     // non-jittered camera viewProj (march sample projection)
uniform int uHbaoEnabled;
uniform float uRadius;      // world-space march radius
uniform float uBias;        // height-above-plane bias in world units (self-occlusion)
uniform int uDirs;          // tangent directions (clamped 1..16)
uniform int uSteps;         // march steps per direction (clamped 1..8)
uniform float uPower;       // final exponent
uniform float uRotSeed;     // per-frame direction rotation
out vec4 FragColor;
void main() {
    if (uHbaoEnabled != 1) { FragColor = vec4(1.0); return; }
    float depth = texture(uGDepth, vUV).r;
    if (depth >= 0.99999) { FragColor = vec4(1.0); return; } // sky: no AO
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;                                     // world position
    vec3 N = normalize(texture(uGNormal, vUV).rgb * 2.0 - 1.0);  // world normal
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    float rot = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) + uRotSeed) * 43758.5453) * 6.2831853;
    float cr = cos(rot), sr = sin(rot);
    int dirs = clamp(uDirs, 1, 16);
    int steps = clamp(uSteps, 1, 8);
    float ao = 1.0;
    for (int d = 0; d < 16; ++d) {
        if (d >= dirs) break;
        float phi = 6.2831853 * float(d) / float(dirs);
        vec2 dir2 = vec2(cos(phi) * cr - sin(phi) * sr, cos(phi) * sr + sin(phi) * cr);
        vec3 dirW = T * dir2.x + B * dir2.y; // unit, in the tangent plane (world space)
        float sinBetaMax = -1.5707963;
        for (int s = 1; s <= 8; ++s) {
            if (s > steps) break;
            float r = uRadius * float(s) / float(steps);
            vec4 clip = uViewProj * vec4(P + dirW * r, 1.0);
            if (clip.w <= 0.0) continue;
            vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
            if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
            float sd = texture(uGDepth, suv).r;
            if (sd >= 0.99999) continue; // sky: no occluder along this ray
            vec4 swp = uInvViewProj * vec4(suv * 2.0 - 1.0, sd * 2.0 - 1.0, 1.0);
            vec3 S = swp.xyz / swp.w;
            float heightAbove = dot(S - P, N);
            if (heightAbove <= uBias) continue;
            float len = length(S - P);
            if (len < 1e-5) continue;
            float sinB = heightAbove / len;
            if (sinB > sinBetaMax) sinBetaMax = sinB;
        }
        if (sinBetaMax > 0.0) ao *= clamp(cos(asin(clamp(sinBetaMax, 0.0, 1.0))), 0.0, 1.0);
    }
    ao = clamp(ao, 0.0, 1.0);
    FragColor = vec4(pow(ao, uPower), 0.0, 0.0, 1.0);
}
)GLSL";

// --- G16 SSR pass: lit HDR + G-buffer -> HDR with screen-space reflections (resource
// "hdrS"). World-space raymarch of the reflection vector against the G-buffer depth
// (linearized compare, thickness band, binary refine), then the hit HDR color is added with
// an edge/roughness/fresnel mask. Plain copy when Effect::Ssr is off.
// Units: uHdr 0, uGNormal 1, uGDepth 2, uGPbr 3.
constexpr const char* kSsrFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uHdr;     // unit 0: lit HDR scene
uniform sampler2D uGNormal; // unit 1
uniform sampler2D uGDepth;  // unit 2
uniform sampler2D uGPbr;    // unit 3 (r=metallic, g=roughness)
uniform mat4 uInvViewProj;
uniform mat4 uViewProj;     // non-jittered camera viewProj (projection of march samples)
uniform vec3 uCamPos;
uniform int uSsrEnabled;
uniform int uMaxSteps;
uniform float uThickness;
uniform float uMaxDist;
uniform float uStrength;    // includes the G22 wetness boost
uniform float uZNear, uZFar;
out vec4 FragColor;
float linearize(float d) { return 2.0 * uZNear * uZFar / max(uZFar + uZNear - d * (uZFar - uZNear), 1e-6); }
float edgeFade(vec2 uv) {
    const float m = 0.08;
    float fx = min(min(uv.x, 1.0 - uv.x) / m, 1.0);
    float fy = min(min(uv.y, 1.0 - uv.y) / m, 1.0);
    float f = min(fx, fy);
    return f * f * (3.0 - 2.0 * f);
}
void main() {
    vec3 hdr = texture(uHdr, vUV).rgb;
    if (uSsrEnabled != 1) { FragColor = vec4(hdr, 1.0); return; }
    float depth = texture(uGDepth, vUV).r;
    if (depth >= 0.99999) { FragColor = vec4(hdr, 1.0); return; } // sky reflects nothing here
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;
    vec3 N = normalize(texture(uGNormal, vUV).rgb * 2.0 - 1.0);
    vec4 pbr = texture(uGPbr, vUV);
    vec3 V = normalize(uCamPos - P);
    float ndotv = clamp(dot(N, V), 0.0, 1.0);
    // mask: rough surfaces reflect little, metals at all angles (schlick, F0 = mix(0.04,1,metal))
    float f0 = mix(0.04, 1.0, pbr.r);
    float fres = f0 + (1.0 - f0) * pow(1.0 - ndotv, 5.0);
    float mask = fres * (1.0 - pbr.g) * (1.0 - pbr.g);
    if (mask < 0.02) { FragColor = vec4(hdr, 1.0); return; }
    vec3 R = reflect(-V, N);
    if (dot(R, N) <= 0.0) { FragColor = vec4(hdr, 1.0); return; }
    // march
    float tPrev = 0.0;
    float t = max(uMaxDist / max(float(uMaxSteps), 1.0), 1e-3) * 0.5;
    float hitT = -1.0;
    vec2 hitUV = vec2(0.0);
    for (int i = 0; i < 128; ++i) {
        if (i >= uMaxSteps) break;
        vec3 sp = P + R * t;
        vec4 clip = uViewProj * vec4(sp, 1.0);
        if (clip.w <= 0.0) break;
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break; // left the screen
        float sceneLin = linearize(texture(uGDepth, suv).r);
        float rayLin = linearize(clip.z / clip.w * 0.5 + 0.5);
        if (sceneLin < rayLin && sceneLin > rayLin - uThickness) { hitT = t; hitUV = suv; break; }
        if (sceneLin < rayLin - uThickness) { // went behind the surface: refine between tPrev and t
            float lo = tPrev, hi = t;
            for (int k = 0; k < 4; ++k) {
                float mid = 0.5 * (lo + hi);
                vec3 mp = P + R * mid;
                vec4 mc = uViewProj * vec4(mp, 1.0);
                vec2 muv = mc.xy / mc.w * 0.5 + 0.5;
                float mScene = linearize(texture(uGDepth, muv).r);
                float mRay = linearize(mc.z / mc.w * 0.5 + 0.5);
                if (mScene < mRay) hi = mid; else lo = mid;
            }
            hitT = 0.5 * (lo + hi);
            vec4 fc = uViewProj * vec4(P + R * hitT, 1.0);
            hitUV = fc.xy / fc.w * 0.5 + 0.5;
            break;
        }
        tPrev = t;
        t += uMaxDist / max(float(uMaxSteps), 1.0);
        if (t > uMaxDist) break;
    }
    if (hitT > 0.0) {
        float distFade = clamp(1.0 - hitT / max(uMaxDist, 1e-4), 0.0, 1.0);
        vec3 refl = texture(uHdr, clamp(hitUV, vec2(0.001), vec2(0.999))).rgb;
        hdr += refl * mask * uStrength * distFade * edgeFade(hitUV);
    }
    FragColor = vec4(hdr, 1.0);
}
)GLSL";

// --- G34 screen-space GI pass: hdr -> hdrG (runs after lighting, feeds SSR). For uCones
// cosine-weighted cones around the surface normal, march uSteps steps up to uMaxDist
// (world units) against the G-buffer depth; each hit contributes the lit HDR color there
// (direct light + emissive = the "bounce" source), attenuated by distance and cone width.
// Added as albedo * irradiance * uStrength. Plain copy when Effect::Gi is off.
// Units: uHdr 0, uGAlbedo 1, uGNormal 2, uGDepth 3.
constexpr const char* kGiFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uHdr;     // unit 0: lit HDR scene
uniform sampler2D uGAlbedo; // unit 1
uniform sampler2D uGNormal; // unit 2
uniform sampler2D uGDepth;  // unit 3
uniform mat4 uInvViewProj;
uniform mat4 uViewProj;     // non-jittered (march projection)
uniform int uGiEnabled;
uniform float uStrength;
uniform int uCones;         // 1..4
uniform int uSteps;         // 1..32
uniform float uMaxDist;     // world units
uniform float uThickness;   // hit thickness in world units
uniform float uRotSeed;
out vec4 FragColor;
float edgeFade(vec2 uv) {
    const float m = 0.08;
    float fx = min(min(uv.x, 1.0 - uv.x) / m, 1.0);
    float fy = min(min(uv.y, 1.0 - uv.y) / m, 1.0);
    return min(fx, fy) * min(fx, fy) * (3.0 - 2.0 * min(fx, fy));
}
void main() {
    vec3 hdr = texture(uHdr, vUV).rgb;
    if (uGiEnabled != 1) { FragColor = vec4(hdr, 1.0); return; }
    float depth = texture(uGDepth, vUV).r;
    if (depth >= 0.99999) { FragColor = vec4(hdr, 1.0); return; } // sky: no GI
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;
    vec3 N = normalize(texture(uGNormal, vUV).rgb * 2.0 - 1.0);
    vec3 albedo = texture(uGAlbedo, vUV).rgb;
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    float rot = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) + uRotSeed) * 43758.5453) * 6.2831853;
    float cr = cos(rot), sr = sin(rot);
    int cones = clamp(uCones, 1, 4);
    int steps = clamp(uSteps, 1, 32);
    vec3 irradiance = vec3(0.0);
    for (int c = 0; c < 4; ++c) {
        if (c >= cones) break;
        float phi = 6.2831853 * float(c) / float(cones) + 0.0;
        vec2 dir2 = vec2(cos(phi) * cr - sin(phi) * sr, cos(phi) * sr + sin(phi) * cr);
        // cone axis tilted 45 deg from N toward the tangent direction, spread by the cone
        vec3 axis = normalize(N * 0.7071 + (T * dir2.x + B * dir2.y) * 0.7071);
        float coneW = 0.35; // half-angle-ish weight falloff
        for (int s = 1; s <= 32; ++s) {
            if (s > steps) break;
            float t = uMaxDist * float(s) / float(steps);
            vec3 sp = P + axis * t;
            vec4 clip = uViewProj * vec4(sp, 1.0);
            if (clip.w <= 0.0) break;
            vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
            if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
            float sd = texture(uGDepth, suv).r;
            vec4 swp = uInvViewProj * vec4(suv * 2.0 - 1.0, sd * 2.0 - 1.0, 1.0);
            vec3 S = swp.xyz / swp.w;
            float dist = length(S - P);
            if (dist > t + uThickness) continue; // surface still beyond the march point: keep marching
            // hit: sample the lit scene color, weight by cone solid angle + distance falloff
            vec3 hitCol = texture(uHdr, suv).rgb;
            float w = exp(-dist * 0.35) * (1.0 - coneW * (float(s) / float(steps)));
            irradiance += hitCol * max(w, 0.0);
            break; // one bounce per cone step (single-bounce GI)
        }
    }
    irradiance /= float(cones);
    vec3 indirect = albedo * irradiance * uStrength * edgeFade(vUV);
    FragColor = vec4(hdr + indirect, 1.0);
}
)GLSL";

// --- G17 bloom pass: HDR -> HDR + bloom (resource "hdrB", feeds TAA). Bright-pass above
// uThreshold, 9-tap golden-angle blur at uRadius texels, added with uStrength. Plain copy
// when Effect::Bloom is off. Unit: uSrc 0.
constexpr const char* kBloomFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc; // unit 0: hdrV (lighting + water + particles + G21 fog)
uniform int uBloomEnabled;
uniform float uThreshold;
uniform float uStrength;
uniform float uRadius;
uniform vec2 uTexel;
out vec4 FragColor;
void main() {
    vec3 c = texture(uSrc, vUV).rgb;
    if (uBloomEnabled != 1) { FragColor = vec4(c, 1.0); return; }
    const float golden = 2.39996323;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 9; ++i) {
        float a = float(i) * golden;
        float r = (0.35 + 0.65 * float(i) / 9.0) * uRadius;
        vec2 off = vec2(cos(a), sin(a)) * r * uTexel;
        vec3 s = texture(uSrc, vUV + off).rgb;
        acc += max(s - vec3(uThreshold), vec3(0.0)); // per-channel bright-pass
    }
    FragColor = vec4(c + (acc / 9.0) * uStrength, 1.0);
}
)GLSL";

// --- G30 lens flare + anamorphic streaks pass: hdrB -> hdrF (feeds TAA), runs after bloom.
// Anamorphic (Effect::Anamorphic): wide horizontal blur of the HDR bright-pass above
// uAnamorphicThreshold 4 every bright light (sun, lamps) gets a horizontal streak.
// Lens flare (Effect::LensFlare): around the sun's screen position uSunUV \u2014 central glow,
// 6 inverted ghosts through the screen center (modulated by the bright-pass there) and a
// thin vertical diffraction streak. Plain copy when both are off. Unit: uSrc 0.
constexpr const char* kFlareFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc; // unit 0: hdrB
uniform vec2 uTexel;
uniform vec2 uSunUV;      // sun screen position (valid when uSunValid == 1)
uniform int uSunValid;
uniform int uAnamorphicEnabled;
uniform int uFlareEnabled;
uniform float uAnamorphicStrength;
uniform float uAnamorphicStretch;   // texels per tap
uniform float uAnamorphicThreshold; // HDR luminance threshold of the streak source
uniform vec3 uAnamorphicTint;
uniform float uFlareStrength;
uniform vec3 uFlareTint;
out vec4 FragColor;
float lum3(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    vec3 col = texture(uSrc, vUV).rgb;
    if (uAnamorphicEnabled == 1) {
        const int N = 16;
        vec3 streak = vec3(0.0);
        float wsum = 0.0;
        for (int i = -N; i <= N; ++i) {
            vec2 uv = vec2(clamp(vUV.x + float(i) * uTexel.x * uAnamorphicStretch, 0.0, 1.0), vUV.y);
            vec3 s = texture(uSrc, uv).rgb;
            float w = exp(-abs(float(i)) / 6.0);
            streak += max(s - vec3(uAnamorphicThreshold), vec3(0.0)) * w;
            wsum += w;
        }
        col += uAnamorphicTint * uAnamorphicStrength * (streak / max(wsum, 1e-5));
    }
    if (uFlareEnabled == 1 && uSunValid == 1) {
        vec2 d = vUV - uSunUV;
        col += uFlareTint * uFlareStrength * 0.6 * exp(-dot(d, d) * 220.0); // central glow
        vec2 center = vec2(0.5, 0.5);
        for (int k = 0; k < 6; ++k) { // inverted ghosts through the screen center
            float g = 0.35 + 0.22 * float(k);
            vec2 guv = clamp(center + (uSunUV - center) * (-g), vec2(0.0), vec2(1.0));
            float r = length(vUV - guv);
            float rad = 0.05 + 0.035 * float(k % 3);
            float sprite = smoothstep(rad, rad * 0.25, r);
            vec3 tint = uFlareTint * (0.6 + 0.4 * sin(float(k) * 1.7 + 1.3));
            float srcLum = lum3(texture(uSrc, guv).rgb);
            col += tint * sprite * uFlareStrength * (0.12 + 0.25 * srcLum);
        }
        float streakV = exp(-abs(d.x) * 900.0) * exp(-abs(d.y) * 18.0); // vertical diffraction streak
        col += uFlareTint * uFlareStrength * 0.35 * streakV;
    }
    FragColor = vec4(col, 1.0);
}
)GLSL";

// --- G32 chromatic aberration pass: up -> ca (output resolution, feeds DoF). Radial RGB
// channel split around uCaCenter: red shifted outward, blue inward, green centered —
// the classic lens dispersion. Plain copy when Effect::ChromaticAberration is off.
// Unit: uSrc 0.
constexpr const char* kCaFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;      // unit 0: upscaled HDR
uniform int uCaEnabled;
uniform float uCaStrength;   // uv offset at the frame corner (r^2 = 0.5)
uniform vec2 uCaCenter;      // usually the frame center (0.5, 0.5)
out vec4 FragColor;
void main() {
    vec3 c = texture(uSrc, vUV).rgb;
    if (uCaEnabled != 1) { FragColor = vec4(c, 1.0); return; }
    vec2 dir = vUV - uCaCenter;
    float r2 = dot(dir, dir);
    float k = uCaStrength * r2 * 2.0; // 0 at the center, uCaStrength at the corners
    vec3 col;
    col.r = texture(uSrc, clamp(vUV + dir * k, 0.0, 1.0)).r;
    col.g = c.g;
    col.b = texture(uSrc, clamp(vUV - dir * k, 0.0, 1.0)).b;
    FragColor = vec4(col, 1.0);
}
)GLSL";

// --- G21 volumetric fog/clouds pass: HDR + fog in-scatter (resource "hdrV", feeds bloom).
// Per pixel: reconstruct the world position from the G-buffer depth (sky -> march to
// uMaxDist), raymarch the view ray through the 3D-noise volume (height falloff + fBm),
// accumulate transmittance and single scattering (Henyey-Greenstein phase for the sun +
// flat ambient). Plain copy when Effect::VolumetricFog is off. Units: uHdr 0, uGDepth 1.
constexpr const char* kVolumetricFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uHdr;    // unit 0: hdrP (lighting + water + particles)
uniform sampler2D uGDepth; // unit 1
uniform int uFogEnabled;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;    // normalized, pointing TO the sun
uniform vec3 uSunColor;  // HDR sun radiance
uniform vec3 uAmbient;   // sky ambient scattered into the volume
uniform float uDensity;       // ground-level density per world unit
uniform float uCloudiness;    // 0 = uniform fog, 1 = fully noise-driven
uniform float uHeightFalloff; // scale height
uniform float uNoiseScale;    // world units per noise cell
uniform float uAnisotropy;    // HG g
uniform float uMaxDist;       // sky march end
uniform float uStrength;
uniform int uSteps;
uniform float uTime;
uniform float uZNear, uZFar;
out vec4 FragColor;
float linearize(float d) { return 2.0 * uZNear * uZFar / max(uZFar + uZNear - d * (uZFar - uZNear), 1e-6); }
float hash3(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}
float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i);
    float n100 = hash3(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash3(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash3(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash3(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash3(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash3(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash3(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float fbm3(vec3 p) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < 4; ++i) {
        sum += amp * noise3(p);
        norm += amp;
        amp *= 0.5;
        p = p * 2.03 + vec3(13.7, 7.1, 5.3);
    }
    return sum / max(norm, 1e-6);
}
float densityAt(vec3 wp) {
    float fall = exp(-max(wp.y, 0.0) / max(uHeightFalloff, 1e-4));
    float n = 1.0;
    if (uCloudiness > 0.0)
        n = 1.0 - uCloudiness + uCloudiness * 2.0 * fbm3(wp / max(uNoiseScale, 1e-6) + vec3(0.0, 0.0, uTime));
    return uDensity * fall * n;
}
void main() {
    vec3 hdr = texture(uHdr, vUV).rgb;
    if (uFogEnabled != 1 || uDensity <= 0.0) { FragColor = vec4(hdr, 1.0); return; }
    float depth = texture(uGDepth, vUV).r;
    // view ray in world space
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;
    vec3 dir = normalize(P - uCamPos);
    float tMax = (depth >= 0.99999) ? uMaxDist : min(linearize(depth), uMaxDist);
    if (tMax <= 0.0) { FragColor = vec4(hdr, 1.0); return; }
    float ds = tMax / max(float(uSteps), 1.0);
    float cosT = -dot(dir, uSunDir);
    float gg = uAnisotropy * uAnisotropy;
    float denom = 1.0 + gg - 2.0 * uAnisotropy * cosT;
    float phase = (1.0 - gg) / (4.0 * 3.14159265 * denom * sqrt(max(denom, 1e-6)));
    float T = 1.0;
    vec3 inscatter = vec3(0.0);
    for (int i = 0; i < 64; ++i) {
        if (i >= uSteps) break;
        float t = (float(i) + 0.5) * ds;
        float d = densityAt(uCamPos + dir * t);
        inscatter += (uSunColor * phase + uAmbient) * (d * ds) * T * uStrength;
        T *= exp(-d * ds);
    }
    FragColor = vec4(hdr * T + inscatter, 1.0);
}
)GLSL";

// --- G15 SSAO uniforms for the lighting pass (uSsao on unit 6, kTextureUnitGBufferPbr slot
// is free in this pass — gPbr is bound at unit 2) ---
constexpr const char* kLightFragSsao = R"GLSL(
uniform sampler2D uSsao;      // unit 6: G15 AO factor (1 = unoccluded)
uniform int uSsaoEnabled;
uniform sampler2D uHbao;      // unit 24: G31 horizon-based AO factor (preferred when on)
uniform int uHbaoEnabled;
)GLSL";

// --- G22 wetness/rain modulation for the lighting pass. Pure CPU twins: render/wetness.hpp.
// Up-facing surfaces get a glossy water film (roughness -> 0.06) and a darker albedo; a
// noise puddle mask deepens depressions. Applied in kLightFragTail before shade().
constexpr const char* kLightFragWetness = R"GLSL(
uniform int uWetnessEnabled;
uniform float uWetness;        // global rain amount 0..1
uniform float uWetnessPuddle;  // puddle mask strength 0..1
uniform float uWetnessNoiseScale;
float wetHash(vec2 p) {
    p = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}
float wetNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = wetHash(i);
    float b = wetHash(i + vec2(1.0, 0.0));
    float c = wetHash(i + vec2(0.0, 1.0));
    float d = wetHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// combined wetness factor: rain amount * up-facing * puddle blend (mirror of wetness.hpp)
void applyWetness(inout vec3 albedo, inout float roughness, vec3 worldPos, vec3 N) {
    if (uWetnessEnabled != 1 || uWetness <= 0.0) return;
    float up = clamp(N.y, 0.0, 1.0);
    float n = wetNoise(worldPos.xz / max(uWetnessNoiseScale, 1e-6));
    float m = n * n * (3.0 - 2.0 * n);
    float puddle = mix(1.0, m, clamp(uWetnessPuddle, 0.0, 1.0));
    float w = clamp(uWetness, 0.0, 1.0) * up * puddle;
    albedo *= 1.0 - 0.35 * w;
    roughness = mix(roughness, 0.06, w);
}
)GLSL";

// --- fullscreen triangle (all post passes) ------------------------------------------
constexpr const char* kFsVert = R"GLSL(#version 330 core
out vec2 vUV;
void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";

// --- lighting pass (G-buffer -> HDR). Own PBR + shadow + IBL GLSL, data layout shared
// with FuhGLCore (LightBlock std140 UBO at binding 0, texture units 4/7/8/9/10). ------
constexpr const char* kLightFragHead = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uGAlbedo;    // unit 0
uniform sampler2D uGNormal;    // unit 1
uniform sampler2D uGPbr;       // unit 2
uniform sampler2D uGEmissive;  // unit 3
uniform sampler2D uGDepth;     // unit 5
uniform sampler2D uShadowMap;            // unit 4 (kTextureUnitShadowMap)
uniform sampler2DArray uShadowMapArray;  // unit 10 (kTextureUnitShadowMapArray)
uniform samplerCube uIrradianceMap;      // unit 7 (kTextureUnitIrradiance)
uniform samplerCube uPrefilterMap;       // unit 8 (kTextureUnitPrefilter)
uniform sampler2D uBrdfLut;              // unit 9 (kTextureUnitBrdfLut)
uniform samplerCube uSkyCube;            // unit 11 (kTextureUnitSkyCube)
uniform int uUseSkyCube;                 // 1 = sample the sky cubemap for background pixels
uniform mat4 uInvViewProj;   // camera viewProj inverse: depth -> world position
uniform vec3 uCamPos;
uniform vec3 uAmbient;
uniform int uLightCount;
uniform vec3 uSkyTop;
uniform vec3 uSkyBottom;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uSunDir;    // direction TO the sun
uniform vec3 uSunColor;  // HDR sun disk color (0 = off)
uniform float uSunCos;   // cos of the sun's angular radius
uniform int uEnvEnabled;
uniform float uPrefilterMaxLod;
uniform float uEnvIntensity;
uniform mat4 uLightVP;
uniform mat4 uLightVPs[8];
uniform mat4 uView;
uniform int uShadowEnabled;
uniform int uShadowLightIndex;
uniform float uShadowStrength;
uniform int uShadowMode;   // 0 = single 2D map, 1 = CSM array
uniform int uCascadeCount;
uniform float uCascadeSplits[9];
layout(std140) uniform LightBlock { // bound to binding point 0 from C++ (GLSL 330 has no binding qualifier)
    vec4 lightPosIntensity[64]; // xyz = position, w = intensity
    vec4 lightColor[64];        // rgb = color
    vec4 lightDirType[64];      // xyz = direction, w = type (0 point, 1 directional, 2 spot)
    vec4 lightParams0[64];      // atten const/linear/quadratic, range
    vec4 lightParams1[64];      // cos(coneInner), cos(coneOuter)
};
)GLSL";

// --- F45 shadow-kit uniforms (fuhgl/render/shadow_glsl.hpp kSkFuncs expects these; the
// ones already declared in kLightFragHead \u2014 uShadowMap, uShadowMapArray, uLightVP,
// uLightVPs, uView, uCascadeCount, uCascadeSplits \u2014 are NOT redeclared here) ----------
constexpr const char* kLightFragKit = R"GLSL(
uniform sampler2D uShadowMoments;           // unit 11 (kTextureUnitShadowMoments, VSM 2D)
uniform sampler2DArray uShadowMomentsArray; // unit 12 (VSM array, matches the CSM)
uniform samplerCube uShadowCube;            // unit 13 (point-light cube depth)
uniform samplerCube uShadowCubeMoments;     // unit 14 (point-light cube VSM moments)
uniform sampler2D uSceneDepth;              // unit 5 = uGDepth (contact shadows)
uniform vec3 uLightPos;                     // point-shadow light position
uniform float uShadowFar;                   // point-shadow far plane (cube depth normalize)
uniform float uNormalOffset;               // normal-offset bias, world units
uniform float uLightSize;                   // PCSS world-space light size
uniform float uShadowWorldWidth;            // PCSS: world width covered by one map layer
uniform float uMinVariance;                 // VSM min variance
uniform int uShadowFilterMode;              // 0 = 3x3 PCF, 1 = poisson PCF, 2 = PCSS, 3 = VSM
uniform float uShadowBlend;                 // CSM cross-fade fraction of a cascade span
uniform float uPcfRadius;                   // poisson PCF kernel radius in texels
uniform float uShadowRotation;              // per-frame poisson disk rotation (anti-shimmer)
uniform int uPointShadows;                  // 1 = sample uShadowCube for uPointShadowLightIndex
uniform int uPointShadowLightIndex;
uniform float uPointShadowStrength;
uniform float uPointShadowRadius;           // cube PCF kernel radius in world units
uniform int uContactShadows;                // 1 = screen-space contact shadows on the shadow light
uniform float uContactShadowStrength;
uniform float uContactShadowMaxDist;
uniform float uContactShadowThickness;
uniform int uContactShadowSteps;
uniform mat4 uProj;                         // camera projection (contact shadows)
uniform float uCsZNear, uCsZFar;            // camera near/far (contact shadows)
)GLSL";

// --- G7 light probes: SH L2 irradiance grid, 7 RGBA16F 3D textures (units 17..23) -----
// 27 floats per probe packed into 7 RGBA texels (c0=t0.xyz, c1=(t0.w,t1.x,t1.y), ...);
// hardware-linear 3D sampling interpolates the coefficients trilinearly across the grid.
constexpr const char* kLightFragProbes = R"GLSL(
uniform sampler3D uProbe0; // unit 17 (kFirstTextureUnit)
uniform sampler3D uProbe1; // 18
uniform sampler3D uProbe2; // 19
uniform sampler3D uProbe3; // 20
uniform sampler3D uProbe4; // 21
uniform sampler3D uProbe5; // 22
uniform sampler3D uProbe6; // 23
uniform int uProbesEnabled;
uniform vec3 uProbeMin;      // grid min corner (world)
uniform vec3 uProbeCellInv;  // 1 / cell size (world per probe step)
uniform vec3 uProbeMaxCoord; // dims - 1 (for clamping the grid coords)
uniform float uProbeStrength;
// irradiance E(n) = sum_l A_l c_lm Y_lm(n) from the packed SH L2 coefficients
vec3 probeIrradiance(vec3 worldPos, vec3 N) {
    vec3 g = clamp((worldPos - uProbeMin) * uProbeCellInv, vec3(0.0), uProbeMaxCoord);
    vec3 uvw = (g + 0.5) / (uProbeMaxCoord + 1.0);
    vec4 t0 = texture(uProbe0, uvw);
    vec4 t1 = texture(uProbe1, uvw);
    vec4 t2 = texture(uProbe2, uvw);
    vec4 t3 = texture(uProbe3, uvw);
    vec4 t4 = texture(uProbe4, uvw);
    vec4 t5 = texture(uProbe5, uvw);
    vec4 t6 = texture(uProbe6, uvw);
    vec3 c0 = t0.xyz;
    vec3 c1 = vec3(t0.w, t1.x, t1.y);
    vec3 c2 = vec3(t1.z, t1.w, t2.x);
    vec3 c3 = vec3(t2.y, t2.z, t2.w);
    vec3 c4 = t3.xyz;
    vec3 c5 = vec3(t3.w, t4.x, t4.y);
    vec3 c6 = vec3(t4.z, t4.w, t5.x);
    vec3 c7 = vec3(t5.y, t5.z, t5.w);
    vec3 c8 = t6.xyz;
    const float A0 = 3.14159265, A1 = 2.09439510, A2 = 0.78539816;
    return c0 * (A0 * 0.28209479)
         + c1 * (A1 * 0.48860251 * N.y)
         + c2 * (A1 * 0.48860251 * N.z)
         + c3 * (A1 * 0.48860251 * N.x)
         + c4 * (A2 * 1.09254843 * N.x * N.y)
         + c5 * (A2 * 1.09254843 * N.y * N.z)
         + c6 * (A2 * 0.31539157 * (3.0 * N.z * N.z - 1.0))
         + c7 * (A2 * 1.09254843 * N.x * N.z)
         + c8 * (A2 * 0.54627422 * (N.x * N.x - N.y * N.y));
}
)GLSL";

constexpr const char* kLightFragPbr = R"GLSL(
// G33 subsurface scattering (Effect::Subsurface): wrap diffuse + backlit translucency
uniform int uSssEnabled;
uniform float uSssDistortion; // how much the "light through the object" shifts toward the normal
uniform float uSssPower;      // translucency pow
uniform float uSssScale;      // translucency intensity
uniform float uSssThickness;  // effective object thickness (scales the translucency term)
float distributionGGX(float ndoth, float roughness) {
    float a2 = roughness * roughness * roughness * roughness;
    float d = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-7);
}
float geometrySmith(float ndotv, float ndotl, float roughness) {
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return (ndotv / max(ndotv * (1.0 - k) + k, 1e-7)) * (ndotl / max(ndotl * (1.0 - k) + k, 1e-7));
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
void lightVector(int i, vec3 worldPos, out vec3 L, out float atten) {
    int t = int(lightDirType[i].w + 0.5);
    if (t == 1) { // directional
        L = normalize(-lightDirType[i].xyz);
        atten = lightPosIntensity[i].w;
    } else {
        vec3 toL = lightPosIntensity[i].xyz - worldPos;
        float d = length(toL);
        L = toL / max(d, 1e-5);
        vec4 pa = lightParams0[i];
        atten = lightPosIntensity[i].w / max(pa.x + pa.y * d + pa.z * d * d, 1e-5);
        if (pa.w > 0.0) atten *= clamp(1.0 - pow(clamp(d / pa.w, 0.0, 1.0), 4.0), 0.0, 1.0);
        if (t == 2) { // spot
            float c = dot(-L, normalize(lightDirType[i].xyz));
            atten *= smoothstep(lightParams1[i].y, lightParams1[i].x, c);
        }
    }
}
// legacy 3x3 PCF (bit-identical to the pre-kit behavior, ShadowFilter::Pcf3x3)
float pcf2D(vec2 uv, float z, float bias, sampler2D map) {
    vec2 texel = 1.0 / vec2(textureSize(map, 0));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(map, uv + vec2(float(x), float(y)) * texel).r;
            sum += (z - bias <= d) ? 1.0 : 0.0;
        }
    return sum / 9.0;
}
float pcfArray(vec2 uv, float z, float bias, int layer) {
    vec2 texel = 1.0 / vec2(textureSize(uShadowMapArray, 0).xy);
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(uShadowMapArray, vec3(uv + vec2(float(x), float(y)) * texel, float(layer))).r;
            sum += (z - bias <= d) ? 1.0 : 0.0;
        }
    return sum / 9.0;
}
// PCSS on a 2D-array layer (kit provides the 2D variant; the array variant is this wrapper)
float pcssBlockerArray(sampler2DArray map, vec2 uv, float z, float bias, float searchTexels, int layer,
                       int taps, float rotation) {
    vec2 texel = 1.0 / vec2(textureSize(map, 0).xy);
    float sum = 0.0, cnt = 0.0;
    for (int i = 0; i < taps; ++i) {
        vec2 off = shadowPoissonTap(i, taps, rotation) * searchTexels * texel;
        float d = texture(map, vec3(uv + off, float(layer))).r;
        if (z - bias > d) { sum += d; cnt += 1.0; }
    }
    return cnt > 0.0 ? sum / cnt : z;
}
float pcssArray(sampler2DArray map, vec2 uv, float z, float bias, float lightSize, float worldWidth,
                int layer, int blockerTaps, int filterTaps, float rotation) {
    float mapSize = float(textureSize(map, 0).x);
    float searchTexels = clamp(lightSize / max(worldWidth, 1e-6) * mapSize, 1.0, 64.0);
    float blocker = pcssBlockerArray(map, uv, z, bias, searchTexels, layer, blockerTaps, rotation);
    float penumbraTexels = searchTexels * (z - blocker) / max(blocker, 1e-4);
    return pcfPoissonArray(map, uv, z, bias, clamp(penumbraTexels, 1.0, 64.0), layer, filterTaps, rotation);
}
// one filter mode applied to one cascade layer / the single 2D map
float filterArray(vec3 p, float bias, int layer) {
    if (uShadowFilterMode == 3) return vsmArray2D(uShadowMomentsArray, p.xy, p.z, uMinVariance, layer);
    if (uShadowFilterMode == 2)
        return pcssArray(uShadowMapArray, p.xy, p.z, bias, uLightSize, uShadowWorldWidth, layer, 8, 12, uShadowRotation);
    if (uShadowFilterMode == 1)
        return pcfPoissonArray(uShadowMapArray, p.xy, p.z, bias, uPcfRadius, layer, 12, uShadowRotation);
    return pcfArray(p.xy, p.z, bias, layer);
}
float filter2D(vec3 p, float bias) {
    if (uShadowFilterMode == 3) return vsm2D(uShadowMoments, p.xy, p.z, uMinVariance);
    if (uShadowFilterMode == 2)
        return pcss2D(uShadowMap, p.xy, p.z, bias, uLightSize, uShadowWorldWidth, 8, 12, uShadowRotation);
    if (uShadowFilterMode == 1)
        return pcfPoisson2D(uShadowMap, p.xy, p.z, bias, uPcfRadius, 12, uShadowRotation);
    return pcf2D(p.xy, p.z, bias, uShadowMap);
}
// shadow factor of the shadow light, using the F45 kit functions (shadow_glsl.hpp kSkFuncs)
float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    float bias = shadowBiasSlope(N, L, 0.0015, 0.0004);
    vec3 wp = shadowWorldPos(worldPos, N, uNormalOffset);
    if (uShadowMode == 1) {
        float viewDepth = -(uView * vec4(wp, 1.0)).z;
        int c = cascadeSelect(viewDepth);
        vec3 p = shadowNdc(uLightVPs[c], wp);
        if (!shadowInBounds(p)) return 1.0;
        float sh = filterArray(p, bias, c);
        if (uShadowBlend > 0.0 && c < uCascadeCount - 1) {
            float span = max(uCascadeSplits[c + 1] - uCascadeSplits[c], 1e-6);
            float t = clamp((viewDepth - (uCascadeSplits[c + 1] - uShadowBlend * span)) /
                                max(uShadowBlend * span, 1e-6), 0.0, 1.0);
            if (t > 0.0) {
                vec3 p2 = shadowNdc(uLightVPs[c + 1], wp);
                if (shadowInBounds(p2)) sh = mix(sh, filterArray(p2, bias, c + 1), t);
            }
        }
        return sh;
    }
    vec3 p = shadowNdc(uLightVP, wp);
    if (!shadowInBounds(p)) return 1.0;
    return filter2D(p, bias);
}
// point-light cube shadow (kit cubeShadow / cubeShadowMoments)
float pointShadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    float bias = shadowBiasSlope(N, L, 0.002, 0.0005);
    vec3 wp = shadowWorldPos(worldPos, N, uNormalOffset);
    if (uShadowFilterMode == 3)
        return cubeShadowMoments(uShadowCubeMoments, wp, uLightPos, uShadowFar, uMinVariance);
    return cubeShadow(uShadowCube, wp, uLightPos, uShadowFar, bias, uPointShadowRadius, 8);
}
vec3 pbrDirect(vec3 albedo, float metallic, float roughness, vec3 N, vec3 V, vec3 L, vec3 radiance) {
    vec3 H = normalize(L + V);
    float ndotl = max(dot(N, L), 0.0);
    float ndotv = max(dot(N, V), 0.0);
    float ndoth = max(dot(N, H), 0.0);
    float hdotv = max(dot(H, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(hdotv, F0);
    vec3 spec = (distributionGGX(ndoth, roughness) * geometrySmith(ndotv, ndotl, roughness) * F) /
                max(4.0 * ndotv * ndotl, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo + spec) * radiance * ndotl;
}
vec3 pbrAmbient(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 V, vec3 worldPos) {
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 spec = vec3(0.0);
    vec3 diff;
    if (uEnvEnabled == 1) {
        vec3 irradiance = texture(uIrradianceMap, N).rgb;
        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * uPrefilterMaxLod).rgb;
        vec2 brdf = texture(uBrdfLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
        spec = prefiltered * (F * brdf.x + vec3(brdf.y)) * ao * uEnvIntensity;
        diff = kD * irradiance * albedo * ao * uEnvIntensity;
    } else {
        diff = albedo * uAmbient * ao;
    }
    if (uProbesEnabled == 1) {
        // G7: diffuse indirect from the SH probe grid replaces the diffuse ambient
        // (IBL keeps its specular part); diffuse outgoing = albedo/pi * E(n)
        diff = kD * albedo * (probeIrradiance(worldPos, N) * 0.31830989) * ao * uProbeStrength;
    }
    return diff + spec;
}
vec3 shade(vec3 albedo, float metallic, float roughness, float ao, float sss, vec3 N, vec3 worldPos, vec3 V) {
    vec3 acc = pbrAmbient(albedo, metallic, roughness, ao, N, V, worldPos);
    for (int i = 0; i < uLightCount && i < 64; ++i) {
        vec3 L;
        float atten;
        lightVector(i, worldPos, L, atten);
        float sh = 1.0;
        if (uShadowEnabled == 1 && i == uShadowLightIndex)
            sh = mix(1.0, shadowFactor(worldPos, N, L), uShadowStrength);
        if (uPointShadows == 1 && i == uPointShadowLightIndex)
            sh *= mix(1.0, pointShadowFactor(worldPos, N, L), uPointShadowStrength);
        if (uContactShadows == 1 && i == uShadowLightIndex) {
            vec3 viewPos = (uView * vec4(worldPos, 1.0)).xyz;
            vec3 dirView = normalize((uView * vec4(L, 0.0)).xyz);
            float jitter = fract(dot(gl_FragCoord.xy, vec2(0.5, 0.25)));
            float cs = contactShadow(viewPos, dirView, uContactShadowMaxDist, uContactShadowThickness,
                                     uContactShadowSteps, jitter);
            sh *= mix(1.0, cs, uContactShadowStrength);
        }
        vec3 direct = pbrDirect(albedo, metallic, roughness, N, V, L, lightColor[i].rgb * atten) * sh;
        if (sss > 0.0) {
            // G33 thin-object SSS: wrapped diffuse (light bleeding around the terminator) +
            // backlit translucency (light seen through the object against the view)
            float wrap = 0.5;
            float ndlW = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
            vec3 Hv = normalize(L + N * uSssDistortion);
            float trans = pow(clamp(dot(V, -Hv), 0.0, 1.0), uSssPower) * uSssScale;
            direct += albedo * lightColor[i].rgb * atten * sh * sss * (1.0 - metallic) *
                      (ndlW * ndlW * 0.5 + trans * uSssThickness);
        }
        acc += direct;
    }
    return acc;
}
)GLSL";

constexpr const char* kLightFragTail = R"GLSL(
out vec4 FragColor;
void main() {
    float depth = texture(uGDepth, vUV).r;
    if (depth >= 0.99999) {
        // background: sky gradient + HDR sun disk (bloom source)
        vec4 fp = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
        vec3 ray = normalize(fp.xyz / fp.w - uCamPos);
        vec3 sky = (uUseSkyCube == 1) ? texture(uSkyCube, ray).rgb
                                      : mix(uSkyBottom, uSkyTop, clamp(vUV.y, 0.0, 1.0));
        sky += uSunColor * smoothstep(uSunCos, 1.0, dot(ray, uSunDir));
        FragColor = vec4(sky, 1.0);
        return;
    }
    vec4 ndc = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 wp = uInvViewProj * ndc;
    vec3 worldPos = wp.xyz / wp.w;
    vec3 N = normalize(texture(uGNormal, vUV).rgb * 2.0 - 1.0);
    vec3 albedo = texture(uGAlbedo, vUV).rgb;
    vec3 emissive = texture(uGEmissive, vUV).rgb;
    vec4 pbr = texture(uGPbr, vUV);
    vec3 V = normalize(uCamPos - worldPos);
    // G22: wetness/rain film modulates albedo + roughness before shading
    applyWetness(albedo, pbr.g, worldPos, N);
    // G15/G31: multiply the material AO with the screen-space AO factor (HBAO preferred when on)
    float ssao = (uHbaoEnabled == 1) ? texture(uHbao, vUV).r
                                     : ((uSsaoEnabled == 1) ? texture(uSsao, vUV).r : 1.0);
    // G33: per-item subsurface factor rides in gPbr.a
    float sss = (uSssEnabled == 1) ? pbr.a : 0.0;
    vec3 lit = shade(albedo, pbr.r, pbr.g, pbr.b * ssao, sss, N, worldPos, V) + emissive;
    if (uFogDensity > 0.0) {
        float d = length(worldPos - uCamPos);
        lit = mix(lit, uFogColor, 1.0 - exp(-uFogDensity * d));
    }
    FragColor = vec4(lit, 1.0);
}
)GLSL";

// --- G8 decal pass: project decal boxes into the G-buffer (albedo + normal) -----------
// The decal box (unit box scaled by uHalfExtents) is drawn with depth test LESS; the
// fragment shader reconstructs the G-buffer world position from depth, keeps pixels whose
// surface lies inside the box, gates on the G-buffer material ID and writes the projected
// decal albedo/normal into attachments 0/1. Units: uGAlbedo 0, uGNormal 1, uDecalTex 3
// (kTextureUnitAlbedo), uDecalNormal 4 (kTextureUnitNormalMap), uGDepth 5, uGMatId 6.
constexpr const char* kDecalVert = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos; // unit box in [-1,1]
uniform mat4 uViewProj;
uniform mat4 uDecalWorld;
uniform vec3 uHalfExtents;
out vec3 vLocal;
void main() {
    vLocal = aPos;
    vec4 wp = uDecalWorld * vec4(aPos * uHalfExtents, 1.0);
    gl_Position = uViewProj * wp;
}
)GLSL";

constexpr const char* kDecalFrag = R"GLSL(#version 330 core
in vec3 vLocal;
uniform sampler2D uGAlbedo;     // unit 0
uniform sampler2D uGNormal;     // unit 1
uniform sampler2D uDecalTex;    // unit 3 (kTextureUnitAlbedo)
uniform sampler2D uDecalNormal; // unit 4 (kTextureUnitNormalMap)
uniform sampler2D uGDepth;      // unit 5
uniform sampler2D uGMatId;      // unit 6
uniform mat4 uInvViewProj;
uniform vec2 uResolution;
uniform mat4 uDecalWorld;
uniform mat4 uInvDecalWorld;
uniform vec3 uHalfExtents;
uniform vec3 uAlbedo;
uniform float uOpacity;
uniform int uUseTex;
uniform int uUseNormalMap;
uniform uint uMatIdMask;
layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oNormal;
void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    float depth = texture(uGDepth, uv).r;
    if (depth >= 0.99999) discard; // sky: no G-buffer surface
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 wp = uInvViewProj * ndc;
    vec3 worldPos = wp.xyz / wp.w;
    vec3 lp = (uInvDecalWorld * vec4(worldPos, 1.0)).xyz;
    if (any(greaterThan(abs(lp), uHalfExtents))) discard;
    vec4 mid = texture(uGMatId, uv);
    uint matId = uint(mid.r * 255.0 + 0.5) | (uint(mid.g * 255.0 + 0.5) << 8)
               | (uint(mid.b * 255.0 + 0.5) << 16) | (uint(mid.a * 255.0 + 0.5) << 24);
    if ((matId & uMatIdMask) == 0u) discard;
    vec3 N = normalize(texture(uGNormal, uv).rgb * 2.0 - 1.0);
    vec3 ln = normalize((uInvDecalWorld * vec4(N, 0.0)).xyz);
    vec3 al = abs(ln);
    vec2 duv, he;
    vec3 T, B;
    if (al.y >= al.x && al.y >= al.z) { duv = lp.xz; he = uHalfExtents.xz; T = vec3(1,0,0); B = vec3(0,0,1); }
    else if (al.x >= al.z)            { duv = lp.zy; he = uHalfExtents.zy; T = vec3(0,0,1); B = vec3(0,1,0); }
    else                              { duv = lp.xy; he = uHalfExtents.xy; T = vec3(0,1,0); B = vec3(1,0,0); }
    vec2 tuv = duv / (2.0 * he) + 0.5;
    if (tuv.x < 0.0 || tuv.x > 1.0 || tuv.y < 0.0 || tuv.y > 1.0) discard;
    vec4 tex = (uUseTex == 1) ? texture(uDecalTex, tuv) : vec4(1.0);
    vec3 dAlbedo = uAlbedo * tex.rgb;
    float alpha = clamp(uOpacity * tex.a, 0.0, 1.0);
    vec3 gAlb = texture(uGAlbedo, uv).rgb;
    oAlbedo = vec4(mix(gAlb, dAlbedo, alpha), 1.0);
    vec3 dN = N;
    if (uUseNormalMap == 1 && alpha > 0.0) {
        vec3 nts = texture(uDecalNormal, tuv).rgb * 2.0 - 1.0;
        vec3 lw = normalize(T * nts.x + B * nts.y + ln * nts.z);
        dN = normalize((uDecalWorld * vec4(lw, 0.0)).xyz);
    }
    vec3 gN = texture(uGNormal, uv).rgb * 2.0 - 1.0;
    vec3 outN = normalize(mix(gN, dN, alpha));
    oNormal = vec4(outN * 0.5 + 0.5, 1.0);
}
)GLSL";

// --- G10 DoF pass (after upscale, before tonemap): hexagonal bokeh gather -------------
// CoC from the linearized G-buffer depth; when disabled the pass is a plain copy.
constexpr const char* kDofFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;    // unit 0: upscaled HDR ("up")
uniform sampler2D uGDepth; // unit 1: G-buffer depth (sampled by uv)
uniform int uDofEnabled;
uniform float uZNear, uZFar;
uniform float uFocusDist;   // world-space focus distance (view depth)
uniform float uDofStrength; // CoC scale per unit of focus error
uniform float uMaxCoC;      // max circle of confusion in pixels
uniform float uBokehAngle;  // hexagon rotation
uniform vec2 uTexel;        // 1 / output resolution
out vec4 FragColor;
float viewDepth(float d) { return uZNear * uZFar / max(uZFar - d * (uZFar - uZNear), 1e-5); }
float cocOf(float d) { return clamp(abs(viewDepth(d) - uFocusDist) * uDofStrength, 0.0, uMaxCoC); }
void main() {
    vec4 center = texture(uSrc, vUV);
    if (uDofEnabled == 0) { FragColor = center; return; }
    float coc = cocOf(texture(uGDepth, vUV).r);
    if (coc < 0.5) { FragColor = center; return; }
    // hexagonal bokeh: 6 taps on the full CoC ring + 6 half-radius taps offset by 30 deg
    vec3 acc = center.rgb;
    float wsum = 1.0;
    for (int k = 0; k < 6; ++k) {
        float a = uBokehAngle + float(k) * 1.0471976; // 60 deg
        vec2 uv1 = vUV + vec2(cos(a), sin(a)) * coc * uTexel;
        float w1 = clamp(cocOf(texture(uGDepth, uv1).r) * 1.5, 0.0, 1.0);
        acc += texture(uSrc, uv1).rgb * w1;
        wsum += w1;
        float a2 = a + 0.5235988; // +30 deg
        vec2 uv2 = vUV + vec2(cos(a2), sin(a2)) * coc * 0.5 * uTexel;
        float w2 = clamp(cocOf(texture(uGDepth, uv2).r) * 1.5, 0.0, 1.0);
        acc += texture(uSrc, uv2).rgb * w2;
        wsum += w2;
    }
    FragColor = vec4(acc / wsum, center.a);
}
)GLSL";

// --- TAA pass: motion-vector reprojected history with 3x3 neighborhood clamp ---------
constexpr const char* kTaaFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uCur;    // unit 0: current HDR frame
uniform sampler2D uMotion; // unit 1: G-buffer motion vectors (prevUV - curUV)
uniform sampler2D uHist;   // unit 2: previous accumulated frame (feedback)
uniform float uAlpha;      // 1 on the first frame / when TAA is off (pure copy)
uniform vec2 uTexel;       // 1 / internal resolution
out vec4 FragColor;
void main() {
    vec3 c = texture(uCur, vUV).rgb;
    vec2 m = texture(uMotion, vUV).rg;
    vec3 h = texture(uHist, clamp(vUV + m, vec2(0.001), vec2(0.999))).rgb;
    vec3 mn = c, mx = c;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            vec3 s = texture(uCur, vUV + vec2(float(x), float(y)) * uTexel).rgb;
            mn = min(mn, s);
            mx = max(mx, s);
        }
    h = clamp(h, mn, mx);
    FragColor = vec4(mix(h, c, uAlpha), 1.0);
}
)GLSL";

// --- upscale pass: FSR1-style EASU (lanczos2 3x3, edge-adaptive-ish) + RCAS sharpen ---
constexpr const char* kUpscaleFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;     // unit 0: internal-resolution HDR
uniform vec2 uSrcSize;      // internal resolution in pixels
uniform float uSharpen;     // RCAS sharpening amount (0 = plain bilinear)
uniform int uMode;         // 0 = bilinear, 1 = EASU-style lanczos2 + RCAS
out vec4 FragColor;
void main() {
    if (uMode == 0) {
        FragColor = vec4(texture(uSrc, vUV).rgb, 1.0);
        return;
    }
    vec2 srcPos = vUV * uSrcSize - 0.5;
    vec2 base = floor(srcPos);
    vec2 f = srcPos - base;
    vec3 acc = vec3(0.0);
    float wsum = 0.0;
    vec3 tap[9];
    int k = 0;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            float wx = (i == 1) ? 1.0 : sin(3.14159265 * (i - 1 - f.x)) / (3.14159265 * (i - 1 - f.x)) *
                                     sin(3.14159265 * (i - 1 - f.x) * 0.5) / (3.14159265 * (i - 1 - f.x) * 0.5);
            float wy = (j == 1) ? 1.0 : sin(3.14159265 * (j - 1 - f.y)) / (3.14159265 * (j - 1 - f.y)) *
                                     sin(3.14159265 * (j - 1 - f.y) * 0.5) / (3.14159265 * (j - 1 - f.y) * 0.5);
            float w = wx * wy;
            vec2 tuv = (base + vec2(float(i), float(j)) + 0.5) / uSrcSize;
            tap[k] = texture(uSrc, clamp(tuv, vec2(0.001), vec2(0.999))).rgb;
            acc += tap[k] * w;
            wsum += w;
            ++k;
        }
    vec3 c = acc / max(wsum, 1e-6);
    // RCAS-style cross sharpening from the 3x3 taps, clamped to the local min/max
    vec3 nb = tap[1] + tap[3] + tap[5] + tap[7];
    vec3 mn = min(c, min(min(tap[1], tap[3]), min(tap[5], tap[7])));
    vec3 mx = max(c, max(max(tap[1], tap[3]), max(tap[5], tap[7])));
    vec3 sharp = clamp(c + (c * 4.0 - nb) * (uSharpen * 0.25), mn, mx);
    FragColor = vec4(sharp, 1.0);
}
)GLSL";

// --- tonemap pass: exposure -> ACES -> gamma (or raw clamp when the effect is off) ---
constexpr const char* kTonemapFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uHdr; // unit 0: upscaled HDR
uniform sampler2D uGDepth; // unit 1: G-buffer depth (internal resolution) for ray occlusion
uniform float uExposure;
uniform int uUseTonemap;
uniform int uOperator; // G23: 0 = ACES, 1 = Reinhard, 2 = Uncharted2, 3 = linear (see render/tonemap.hpp)
uniform vec2 uSunScreen;    // sun position in uv of the output image
uniform float uRayStrength; // 0 = god rays off
uniform float uRayDecay;
uniform int uRaySamples;
out vec4 FragColor;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
// G23: Uncharted 2 (Hable) filmic curve, white point 11.2 — CPU twin: tonemapUncharted2()
vec3 uncharted2(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;
    vec3 cur = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - vec3(E / F);
    vec3 white = ((vec3(W) * (A * W + C * B) + D * E) / (vec3(W) * (A * W + B) + D * F)) - vec3(E / F);
    return clamp(cur / white, 0.0, 1.0);
}
vec3 tonemapOp(vec3 c, int op) {
    if (op == 1) return c / (c + vec3(1.0));        // Reinhard
    if (op == 2) return uncharted2(c);               // Uncharted2
    if (op == 3) return clamp(c, 0.0, 1.0);          // linear
    return aces(c);                                  // 0 = ACES (default)
}
void main() {
    vec3 c = texture(uHdr, vUV).rgb * uExposure;
    if (uRayStrength > 0.0) {
        // god rays: weighted average of the HDR image along the segment from this pixel
        // toward the sun's screen position. Each sample is occluded when the G-buffer depth
        // at its uv is closer than the ray line (sun ~ far plane), so geometry (trees,
        // hills) casts real light shafts.
        float depthP = texture(uGDepth, vUV).r;
        vec2 delta = (uSunScreen - vUV) / max(float(uRaySamples), 1.0);
        vec3 acc = vec3(0.0);
        vec2 suv = vUV;
        for (int i = 0; i < 64; ++i) {
            if (i >= uRaySamples) break;
            suv += delta;
            float t = float(i + 1) / max(float(uRaySamples), 1.0);
            float rayDepth = mix(depthP, 1.0, t);
            float sceneDepth = texture(uGDepth, clamp(suv, vec2(0.001), vec2(0.999))).r;
            float occl = step(rayDepth - 0.002, sceneDepth); // 1 = light passes, 0 = blocked
            vec3 s = texture(uHdr, clamp(suv, vec2(0.001), vec2(0.999))).rgb;
            // weight 1 at the sun end, decaying toward the pixel: shafts come from the
            // bright sky/sun region, modulated per-pixel by the geometry occlusion
            float d = pow(uRayDecay, float(uRaySamples - i));
            acc += max(s - vec3(0.35), vec3(0.0)) * d * occl;
        }
        c += (acc / max(float(uRaySamples), 1.0)) * uRayStrength;
    }
    if (uUseTonemap == 1) {
        c = tonemapOp(c, uOperator); // G23: operator selectable per frame
        c = pow(c, vec3(1.0 / 2.2));
    } else {
        c = clamp(c, 0.0, 1.0);
    }
    FragColor = vec4(c, 1.0);
}
)GLSL";

/// @brief Compiles a fullscreen material (kFsVert + fragSrc) for a post pass.
inline bool compilePostFs(fuhgl::render::Material& mat, const std::string& fragSrc, const char* what) {
    if (!mat.create()) return false;
    fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
    if (!vs.compile(kFsVert)) {
        std::fprintf(stderr, "%s VS: %s\n", what, vs.infoLog().c_str());
        return false;
    }
    if (!fs.compile(fragSrc.c_str())) {
        std::fprintf(stderr, "%s FS: %s\n", what, fs.infoLog().c_str());
        return false;
    }
    mat.program().attach(vs);
    mat.program().attach(fs);
    if (!mat.program().link()) {
        std::fprintf(stderr, "%s link: %s\n", what, mat.program().infoLog().c_str());
        return false;
    }
    return true;
}

// --- plain fullscreen copy (water-off fallback + particles pass: src -> target) --------
constexpr const char* kCopyFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
out vec4 FragColor;
void main() { FragColor = texture(uSrc, vUV); }
)GLSL";

// --- G13 water: planar-reflection pass -------------------------------------------------
// The reflection pass renders the scene (without the water mesh) from the mirrored camera
// into a small RGBA16F target. Background pixels get the sky cubemap + sun disk (same
// pattern as the lighting pass), geometry gets cheap lambert+blinn shading (no shadows in
// the reflection — at reflection resolution + wave distortion this is not visible).
// Geometry below the water plane is clipped by pushing its clip-space z below -w.
constexpr const char* kReflVert = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;    // mirrored camera view-projection
uniform vec4 uClipPlane;   // xyz = water plane normal (world), w = d; keep dot(p,n)+d >= 0
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out vec2 vUV;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    vUV = aUV;
    gl_Position = uViewProj * wp;
    if (dot(wp.xyz, uClipPlane.xyz) + uClipPlane.w < 0.0)
        gl_Position.z = -2.0 * gl_Position.w; // below the water plane: clipped
}
)GLSL";

constexpr const char* kReflSkyFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform samplerCube uSkyCube;   // unit kTextureUnitSkyCube (16)
uniform int uUseSkyCube;
uniform mat4 uInvViewProj;      // mirrored camera viewProj inverse
uniform vec3 uCamPos;           // mirrored eye
uniform vec3 uSkyTop;
uniform vec3 uSkyBottom;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunCos;
out vec4 FragColor;
void main() {
    vec4 fp = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 ray = normalize(fp.xyz / fp.w - uCamPos);
    vec3 sky = (uUseSkyCube == 1) ? texture(uSkyCube, ray).rgb
                                  : mix(uSkyBottom, uSkyTop, clamp(vUV.y, 0.0, 1.0));
    sky += uSunColor * smoothstep(uSunCos, 1.0, dot(ray, uSunDir));
    FragColor = vec4(sky, 1.0);
}
)GLSL";

constexpr const char* kReflFrag = R"GLSL(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in vec2 vUV;
uniform vec3 uAlbedo;
uniform int uUseTex;
uniform sampler2D uMatTex;   // unit kTextureUnitAlbedo (3)
uniform vec3 uCamPos;        // mirrored eye
uniform vec3 uAmbient;
uniform vec3 uSunDir;        // direction TO the sun
uniform vec3 uSunIrradiance; // sun color * intensity
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 FragColor;
void main() {
    vec3 albedo = uAlbedo * vColor;
    if (uUseTex == 1) albedo *= texture(uMatTex, vUV).rgb;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(uSunDir);
    vec3 col = albedo * (uAmbient + uSunIrradiance * max(dot(N, L), 0.0));
    vec3 H = normalize(L + V);
    col += uSunIrradiance * pow(max(dot(N, H), 0.0), 64.0) * 0.6;
    if (uFogDensity > 0.0)
        col = mix(col, uFogColor, 1.0 - exp(-uFogDensity * length(vWorldPos - uCamPos)));
    FragColor = vec4(col, 1.0);
}
)GLSL";

// --- G13 water pass: Gerstner-wave surface shaded into the HDR image -------------------
// Vertex: displace the flat grid by the Gerstner sum, derive the wave normal analytically
// (same math as water::evalGerstner on the CPU). Fragment: fresnel mix of the planar
// reflection texture and the refraction (scene HDR distorted by the normal, absorbed with
// water depth), crest foam ("Gicht") + depth-based shore foam, sun glint, fog. The post FBO
// has no depth attachment, so the scene-occlusion test is done manually against gDepth.
constexpr const char* kWaterVert = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform float uTime;
uniform int uWaveCount;
uniform vec4 uWaveA[8]; // xy = direction, z = amplitude, w = k (2*pi/wavelength)
uniform vec4 uWaveB[8]; // x = omega, y = Q (horizontal coefficient)
out vec3 vWorldPos;
out vec3 vNormal;
out float vCrest;   // wave height / maxWaveHeight, ~[-1, 1]
out vec2 vLocalXZ;
void main() {
    vec2 xz = aPos.xz;
    vec3 p = vec3(xz.x, 0.0, xz.y);
    vec3 dPdx = vec3(1.0, 0.0, 0.0);
    vec3 dPdz = vec3(0.0, 0.0, 1.0);
    float height = 0.0, maxA = 1e-5;
    for (int i = 0; i < 8; ++i) {
        if (i >= uWaveCount) break;
        vec2 d = normalize(uWaveA[i].xy);
        float a = uWaveA[i].z;
        float k = uWaveA[i].w;
        float omega = uWaveB[i].x;
        float q = uWaveB[i].y;
        float ph = k * dot(d, xz) - omega * uTime;
        float s = sin(ph), c = cos(ph);
        p.x += q * a * d.x * c;
        p.z += q * a * d.y * c;
        p.y += a * s;
        height += a * s;
        maxA += a;
        float wa = k * a;
        dPdx += vec3(-q * d.x * d.x * wa * s, d.x * wa * c, -q * d.x * d.y * wa * s);
        dPdz += vec3(-q * d.x * d.y * wa * s, d.y * wa * c, -q * d.y * d.y * wa * s);
    }
    vec3 n = normalize(cross(dPdz, dPdx));
    vec4 wp = uModel * vec4(p, 1.0);
    vWorldPos = wp.xyz;
    vNormal = normalize(mat3(uModel) * n);
    vCrest = height / maxA;
    vLocalXZ = xz;
    gl_Position = uViewProj * wp;
}
)GLSL";

constexpr const char* kWaterFrag = R"GLSL(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in float vCrest;
in vec2 vLocalXZ;
uniform sampler2D uSceneHdr; // unit 0: lighting output without the water (refraction)
uniform sampler2D uGDepth;   // unit 1: G-buffer depth (occlusion + shore foam)
uniform sampler2D uReflTex;  // unit 2: planar reflection target
uniform mat4 uInvViewProj;
uniform mat4 uViewProj;      // camera viewProj (un-jittered): water fragment -> depth value
uniform vec3 uCamPos;
uniform vec2 uResolution;    // internal resolution in pixels
uniform float uTime;
uniform vec3 uPlaneN;        // world water-plane normal (for the depth below the surface)
uniform vec3 uDeepColor;
uniform vec3 uShallowColor;
uniform vec3 uFoamColor;
uniform float uFresnelF0;
uniform float uReflectionStrength;
uniform float uDistortion;
uniform float uRefractionDistortion;
uniform float uFoamCrestStart;
uniform float uFoamCrestSoftness;
uniform float uShoreFoamWidth;
uniform float uFoamStrength;
uniform float uGlintPower;
uniform float uDetailScale;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 FragColor;
// procedural ripple heightfield (two scrolling directional sines + one diagonal)
float rippleH(vec2 p, float t) {
    return sin(p.x * 2.1 + t * 1.1) * sin(p.y * 1.7 - t * 0.9) * 0.6
         + sin(dot(p, vec2(4.3, 3.1)) - t * 1.9) * 0.4
         + sin(dot(p, vec2(-7.7, 5.9)) + t * 2.6) * 0.25;
}
void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    // manual depth test: the post-pass FBO has no depth attachment, so the water fragment's
    // own depth value (projected with the camera viewProj) is compared against the G-buffer
    // depth — both live in the same depth-texture quantization
    float sceneDepth = texture(uGDepth, uv).r;
    bool hasScene = sceneDepth < 0.99999;
    vec4 swp = uInvViewProj * vec4(uv * 2.0 - 1.0, sceneDepth * 2.0 - 1.0, 1.0);
    vec3 sceneWorld = swp.xyz / swp.w;
    vec4 waterClip = uViewProj * vec4(vWorldPos, 1.0);
    float waterDepthTex = waterClip.z / max(waterClip.w, 1e-6) * 0.5 + 0.5;
    if (hasScene && waterDepthTex > sceneDepth + 0.0002) discard; // scene in front of the water
    // detail ripple normal on top of the Gerstner wave normal
    vec2 p = vLocalXZ * uDetailScale;
    float e = 0.35;
    float t = uTime;
    float hx = rippleH(p + vec2(e, 0.0), t) - rippleH(p - vec2(e, 0.0), t);
    float hz = rippleH(p + vec2(0.0, e), t) - rippleH(p - vec2(0.0, e), t);
    vec3 detailN = normalize(vec3(-hx / (2.0 * e), 1.0, -hz / (2.0 * e)));
    vec3 N = normalize(vec3(vNormal.xz + detailN.xz * 0.6, vNormal.y));
    vec3 V = normalize(uCamPos - vWorldPos);
    // water depth below this fragment (scene point under the surface) for shore foam + absorption
    float waterDepth = hasScene ? max(-dot(sceneWorld - vWorldPos, uPlaneN), 0.0) : 0.0;
    // smoothstep needs edge0 < edge1: foam fades in as the water gets shallower
    float shoreFoam = hasScene ? (1.0 - smoothstep(0.0, uShoreFoamWidth, waterDepth)) : 0.0;
    float crestFoam = smoothstep(uFoamCrestStart, uFoamCrestStart + uFoamCrestSoftness, vCrest);
    float foam = clamp(max(shoreFoam, crestFoam) * uFoamStrength, 0.0, 1.0);
    // fresnel (dielectric)
    float fres = uFresnelF0 + (1.0 - uFresnelF0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    // planar reflection: the mirrored camera shares the projection, so screen uv maps 1:1
    vec2 reflUV = clamp(uv + N.xz * uDistortion, vec2(0.001), vec2(0.999));
    vec3 refl = texture(uReflTex, reflUV).rgb * uReflectionStrength;
    // refraction: scene HDR distorted by the normal, absorbed with depth, tinted by the body color
    vec2 refrUV = clamp(uv + N.xz * uRefractionDistortion, vec2(0.001), vec2(0.999));
    vec3 refr = texture(uSceneHdr, refrUV).rgb;
    vec3 absorb = exp(-vec3(1.8, 0.60, 0.40) * waterDepth);
    vec3 body = mix(uShallowColor, uDeepColor, clamp(waterDepth * 0.25, 0.0, 1.0));
    vec3 refrCol = refr * absorb + body * (1.0 - absorb) * 0.8;
    vec3 col = mix(refrCol, refl, clamp(fres, 0.0, 1.0));
    // sun glint on the wave normals
    vec3 R = reflect(-V, N);
    col += uSunColor * pow(max(dot(R, uSunDir), 0.0), uGlintPower) * 2.0;
    // foam on top ("Gicht" at the crests + shore foam)
    col = mix(col, uFoamColor, foam);
    // distance fog
    if (uFogDensity > 0.0)
        col = mix(col, uFogColor, 1.0 - exp(-uFogDensity * length(vWorldPos - uCamPos)));
    FragColor = vec4(col, 1.0);
}
)GLSL";

/// @brief Compiles the G13 water-surface material (kWaterVert + kWaterFrag) — a mesh pass,
/// not a fullscreen one.
inline bool compileWaterMat(fuhgl::render::Material& mat) {
    if (!mat.create()) return false;
    fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
    if (!vs.compile(kWaterVert)) {
        std::fprintf(stderr, "water VS: %s\n", vs.infoLog().c_str());
        return false;
    }
    if (!fs.compile(kWaterFrag)) {
        std::fprintf(stderr, "water FS: %s\n", fs.infoLog().c_str());
        return false;
    }
    mat.program().attach(vs);
    mat.program().attach(fs);
    if (!mat.program().link()) {
        std::fprintf(stderr, "water link: %s\n", mat.program().infoLog().c_str());
        return false;
    }
    return true;
}


} // namespace fuhglgraphics::render
