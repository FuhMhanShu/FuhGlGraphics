#pragma once

/// @file effects.hpp
/// @brief G3 effect registry + capability mapping (F39/F40-style): which features of
/// fuhglgraphics::render::DeferredRenderer the current GPU can run, plus a uniform
/// on/off API. The mapping effectSupported() is a pure function of the GL version,
/// so it is unit-testable without a GL context.

#include <fuhgl/gl/info.hpp>
#include <fuhgl/math/func.hpp>
#include <fuhgl/math/vec.hpp>

#include <cstdio>
#include <string>

namespace fuhglgraphics::render {

/// @brief Toggleable features owned by the G3 deferred renderer.
enum class Effect {
    Taa = 0,    ///< temporal anti-aliasing: Halton subpixel jitter + motion-vector history blend
    Upscale,    ///< FSR1-style EASU+RCAS upscaling when renderScale < 1
    Tonemap,    ///< ACES tonemap + gamma in the final pass (off = raw clamped HDR)
    Msaa,       ///< multisampled G-buffer + resolve (create() parameter, not runtime-toggleable)
    Shadows,    ///< single depth shadow map (FuhGLCore ShadowMap on the F45 shadow kit, F15/F46)
    CsmShadows, ///< cascaded shadow maps (FuhGLCore CascadeShadowMap on the F45 shadow kit, F30/F46)
    Ibl,        ///< image-based lighting from SceneView::env (F29)
    PointShadows,   ///< cube depth map for one point light (FuhGLCore CubeShadowMap, F46 shadow kit)
    ContactShadows, ///< screen-space raymarched contact shadows on the shadow light (F46)
    Particles,      ///< G5 particle systems (compute on GL >= 4.3, CPU fallback below)
    LightProbes,    ///< G7 SH-irradiance light-probe grid (diffuse indirect light, 3D textures)
    Decals,         ///< G8 deferred decals projected into the G-buffer (material-ID gated)
    Dof,            ///< G10 depth of field with a hexagonal bokeh kernel (post pass after upscale)
    DebugHud,       ///< G10 mini debug HUD (fps, frame time, effect toggles) on FuhGLCore TextRenderer
    Water,          ///< G13 water surface: Gerstner waves + planar reflection + foam pass
    GpuSkinning,    ///< G14 GPU skinning: matrix-palette SSBO in the G-buffer pass (GL >= 4.3, CPU fallback below)
    Ssao,           ///< G15 screen-space ambient occlusion from the G-buffer (pass before lighting)
    Ssr,            ///< G16 screen-space reflections: raymarch vs the G-buffer depth (post pass after lighting)
    Bloom,          ///< G17 bloom: bright-pass (adjustable threshold) + blur + add in the HDR chain before TAA
    VolumetricFog,  ///< G21 volumetric fog/clouds: raymarched 3D-noise volume with height falloff + HG phase (post pass after particles, before bloom)
    Wetness,        ///< G22 wetness/rain on PBR: lighting-pass roughness/albedo modulation + puddle mask + SSR boost
    LensFlare,      ///< G24 lens flare around the sun: glow + inverted ghosts + diffraction streak (post pass after bloom)
    Anamorphic,     ///< G24 anamorphic streaks: wide horizontal blur of the HDR bright-pass (same post pass, own toggle)
    Hbao,           ///< G25 horizon-based ambient occlusion from the G-buffer (pass before lighting, preferred over G15 SSAO)
};

/// @brief Number of Effect values.
constexpr int kEffectCount = 24;

/// @brief Human-readable name of an Effect (for logs/warnings).
inline const char* effectName(Effect e) {
    switch (e) {
        case Effect::Taa: return "TAA";
        case Effect::Upscale: return "upscaling";
        case Effect::Tonemap: return "tonemap";
        case Effect::Msaa: return "MSAA";
        case Effect::Shadows: return "shadows";
        case Effect::CsmShadows: return "CSM shadows";
        case Effect::Ibl: return "IBL";
        case Effect::PointShadows: return "point shadows";
        case Effect::ContactShadows: return "contact shadows";
        case Effect::Particles: return "particles";
        case Effect::LightProbes: return "light probes";
        case Effect::Decals: return "decals";
        case Effect::Dof: return "DoF";
        case Effect::DebugHud: return "debug HUD";
        case Effect::Water: return "water";
        case Effect::GpuSkinning: return "GPU skinning";
        case Effect::Ssao: return "SSAO";
        case Effect::Ssr: return "SSR";
        case Effect::Bloom: return "bloom";
        case Effect::VolumetricFog: return "volumetric fog";
        case Effect::Wetness: return "wetness";
        case Effect::LensFlare: return "lens flare";
        case Effect::Anamorphic: return "anamorphic streaks";
        case Effect::Hbao: return "HBAO";
    }
    return "unknown";
}

/// @brief Inverse of effectName() (for config files / scene serialization). False when unknown.
inline bool effectFromName(const std::string& name, Effect& out) {
    for (int i = 0; i < kEffectCount; ++i) {
        const Effect e = static_cast<Effect>(i);
        if (name == effectName(e)) {
            out = e;
            return true;
        }
    }
    return false;
}

/// @brief Pure capability mapping (F39): can a GPU with the given GL version run the effect?
/// All G3 shaders are GLSL 330 and multisample textures are core since GL 3.2, so every
/// effect needs GL >= 3.3. Kept as a pure function so unit tests need no GL context.
inline bool effectSupported(Effect e, int glMajor, int glMinor) {
    // G14: the matrix-palette SSBO path of the G-buffer pass needs GL >= 4.3
    // (below that the renderer falls back to CPU skinning, G6)
    if (e == Effect::GpuSkinning)
        return glMajor > 4 || (glMajor == 4 && glMinor >= 3);
    return glMajor > 3 || (glMajor == 3 && glMinor >= 3);
}

/// @brief Same mapping from a queried DeviceInfo (call with a current GL context).
inline bool effectSupported(Effect e, const fuhgl::gl::DeviceInfo& info) {
    return effectSupported(e, info.version.major, info.version.minor);
}

/// @brief Prints a warning that an effect was disabled because the GPU does not
/// support it (graceful degradation, F39).
inline void warnUnsupported(const char* rendererName, Effect e, int glMajor, int glMinor) {
    std::fprintf(stderr, "%s: effect '%s' disabled — not supported by this GPU (GL %d.%d)\n",
                 rendererName, effectName(e), glMajor, glMinor);
}

/// @brief TAA subpixel jitter in NDC units for `frameIndex` at the given internal
/// resolution (Halton(2,3) sequence, centered). Pure/deterministic — unit-testable.
inline fuhgl::math::Vec2 taaJitterNdc(unsigned frameIndex, int width, int height) {
    const fuhgl::math::Vec2 h = fuhgl::math::halton(frameIndex);
    const float w = width > 0 ? static_cast<float>(width) : 1.f;
    const float hgt = height > 0 ? static_cast<float>(height) : 1.f;
    return {(h.x - 0.5f) * 2.f / w, (h.y - 0.5f) * 2.f / hgt};
}

} // namespace fuhglgraphics::render
