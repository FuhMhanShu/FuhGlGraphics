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
uniform float uMetallic;
uniform float uRoughness;
uniform float uAo;
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
    vec3 albedo = uAlbedo * vColor;
    if (uUseTex == 1) albedo *= texture(uMatTex, vUV).rgb;
    vec3 N = normalize(vNormal);
    if (uUseNormalMap == 1) {
        // tangent frame from screen-space derivatives (no tangent attribute needed)
        vec3 dp1 = dFdx(vWorldPos);
        vec3 dp2 = dFdy(vWorldPos);
        vec2 duv1 = dFdx(vUV);
        vec2 duv2 = dFdy(vUV);
        vec3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
        vec3 B = normalize(-dp1 * duv2.x + dp2 * duv1.x);
        vec3 nTS = texture(uNormalTex, vUV).rgb * 2.0 - 1.0;
        N = normalize(T * nTS.x + B * nTS.y + N * nTS.z);
    }
    oAlbedo = vec4(albedo, 1.0);
    oNormal = vec4(N * 0.5 + 0.5, 1.0);
    oPbr = vec4(uMetallic, uRoughness, uAo, 1.0);
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
vec3 shade(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 worldPos, vec3 V) {
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
        acc += pbrDirect(albedo, metallic, roughness, N, V, L, lightColor[i].rgb * atten) * sh;
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
    vec3 lit = shade(albedo, pbr.r, pbr.g, pbr.b, N, worldPos, V) + emissive;
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
uniform vec2 uSunScreen;    // sun position in uv of the output image
uniform float uRayStrength; // 0 = god rays off
uniform float uRayDecay;
uniform int uRaySamples;
out vec4 FragColor;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
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
        c = aces(c);
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

// --- plain fullscreen copy (particles pass: hdr -> hdrP before the blended billboards) --
constexpr const char* kCopyFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
out vec4 FragColor;
void main() { FragColor = texture(uSrc, vUV); }
)GLSL";

} // namespace fuhglgraphics::render
