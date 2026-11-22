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
float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    float bias = max(0.0015 * (1.0 - dot(N, L)), 0.0004);
    if (uShadowMode == 1) {
        float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
        int cascade = uCascadeCount - 1;
        for (int i = 0; i < uCascadeCount; ++i)
            if (viewDepth <= uCascadeSplits[i + 1]) { cascade = i; break; }
        vec4 lp = uLightVPs[cascade] * vec4(worldPos, 1.0);
        vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
        if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
        return pcfArray(p.xy, p.z, bias, cascade);
    }
    vec4 lp = uLightVP * vec4(worldPos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    return pcf2D(p.xy, p.z, bias, uShadowMap);
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
vec3 pbrAmbient(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 V) {
    if (uEnvEnabled == 1) {
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 irradiance = texture(uIrradianceMap, N).rgb;
        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * uPrefilterMaxLod).rgb;
        vec2 brdf = texture(uBrdfLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
        return (kD * irradiance * albedo + prefiltered * (F * brdf.x + vec3(brdf.y))) * ao * uEnvIntensity;
    }
    return albedo * uAmbient * ao;
}
vec3 shade(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 worldPos, vec3 V) {
    vec3 acc = pbrAmbient(albedo, metallic, roughness, ao, N, V);
    for (int i = 0; i < uLightCount && i < 64; ++i) {
        vec3 L;
        float atten;
        lightVector(i, worldPos, L, atten);
        float sh = 1.0;
        if (uShadowEnabled == 1 && i == uShadowLightIndex)
            sh = mix(1.0, shadowFactor(worldPos, N, L), uShadowStrength);
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
        vec3 sky = mix(uSkyBottom, uSkyTop, clamp(vUV.y, 0.0, 1.0));
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
uniform float uExposure;
uniform int uUseTonemap;
out vec4 FragColor;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main() {
    vec3 c = texture(uHdr, vUV).rgb * uExposure;
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

} // namespace fuhglgraphics::render
