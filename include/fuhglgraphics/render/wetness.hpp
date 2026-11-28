#pragma once

/// @file wetness.hpp
/// @brief G22 wetness / rain on PBR materials — pure (GL-free) helpers of the wetness
/// modulation in the G3 lighting pass: a deterministic 2D puddle mask (value noise over the
/// world xz), the combined wetness factor (rain amount * up-facing * puddle blend), and the
/// resulting roughness / albedo changes. Unit-testable twins of the GLSL in kLightFragWetness.

#include <fuhgl/math/vec.hpp>

#include <cmath>

namespace fuhglgraphics::render {

/// @brief Deterministic 2D hash -> [0, 1].
inline float wetnessHash(fuhgl::math::Vec2 p, unsigned seed) {
    const float s = static_cast<float>(seed) * 0.1031f;
    float k = (p.x + s) * 12.9898f + (p.y + s * 0.7f) * 78.233f;
    k = k - std::floor(k);
    return k * k * (3.f - 2.f * k);
}

/// @brief Bilinear value noise over world xz in [0, 1] (cells of `scale` world units).
inline float wetnessNoise(fuhgl::math::Vec2 xz, float scale, unsigned seed = 0u) {
    if (scale < 1e-6f) scale = 1e-6f;
    const fuhgl::math::Vec2 p = xz / scale;
    const int x0 = static_cast<int>(std::floor(p.x));
    const int y0 = static_cast<int>(std::floor(p.y));
    const float fx = p.x - static_cast<float>(x0);
    const float fy = p.y - static_cast<float>(y0);
    const float u = fx * fx * (3.f - 2.f * fx);
    const float v = fy * fy * (3.f - 2.f * fy);
    auto val = [&](int x, int y) {
        return wetnessHash({static_cast<float>(x), static_cast<float>(y)}, seed);
    };
    const float c00 = val(x0, y0) + u * (val(x0 + 1, y0) - val(x0, y0));
    const float c10 = val(x0, y0 + 1) + u * (val(x0 + 1, y0 + 1) - val(x0, y0 + 1));
    return c00 + v * (c10 - c00);
}

/// @brief Puddle mask in [0, 1]: low-frequency noise thresholded so only "depressions" get
/// puddles — `puddleStrength` in [0, 1] blends between a flat wet film and the mask.
inline float wetnessPuddleMask(fuhgl::math::Vec2 xz, float noiseScale, float puddleStrength,
                               unsigned seed = 2024u) {
    const float n = wetnessNoise(xz, noiseScale, seed);
    const float m = n * n * (3.f - 2.f * n); // smoothstep-ish, biases to 0
    const float s = puddleStrength < 0.f ? 0.f : (puddleStrength > 1.f ? 1.f : puddleStrength);
    return 1.f - s + s * m;
}

/// @brief Combined wetness factor in [0, 1]: rain `amount` acts on up-facing surfaces
/// (`ndotUp` = clamp(N.y, 0, 1)); the puddle mask deepens it locally.
inline float wetnessFactor(float amount, float puddleStrength, float ndotUp, float puddle) {
    const float a = amount < 0.f ? 0.f : (amount > 1.f ? 1.f : amount);
    const float up = ndotUp < 0.f ? 0.f : (ndotUp > 1.f ? 1.f : ndotUp);
    return a * up * puddle;
}

/// @brief Wet-surface roughness: mixes toward a glossy ~0.06 film with the wetness factor.
inline float wetnessRoughness(float roughness, float wet) {
    const float w = wet < 0.f ? 0.f : (wet > 1.f ? 1.f : wet);
    return roughness + (0.06f - roughness) * w;
}

/// @brief Wet-surface albedo scale: water darkens the diffuse lobe (~35 % at full wetness).
inline float wetnessAlbedoScale(float wet) {
    const float w = wet < 0.f ? 0.f : (wet > 1.f ? 1.f : wet);
    return 1.f - 0.35f * w;
}

/// @brief G38 bridge waterline band in [0, 1]: 1 at the waterline, smoothstep-fading to 0 over
/// `bandWidth` world units above it (mirror of applyBridgeWaterline in kLightFragBridgeWaterline:
/// band = 1 - smoothstep(0.05, 1.1, worldY - waterLevel)). <= 0 below/at the waterline handled
/// by the caller; the band is only evaluated for worldY > waterLevel.
inline float bridgeWaterlineBand(float worldY, float waterLevelWorld, float bandWidth = 1.1f,
                                 float edge = 0.05f) {
    const float h = worldY - waterLevelWorld;
    if (h <= edge) return 1.f;
    const float t = (h - edge) / std::max(bandWidth - edge, 1e-6f);
    const float c = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return 1.f - c * c * (3.f - 2.f * c);
}

/// @brief G38 bridge waterline: wet-surface roughness in the band (mixes toward glossy 0.08).
inline float bridgeWaterlineRoughness(float roughness, float band, float strength) {
    const float w = band < 0.f ? 0.f : (band > 1.f ? 1.f : band) *
                                      (strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength));
    return roughness + (0.08f - roughness) * w;
}

/// @brief G38 bridge waterline: albedo darkening in the band (mirror of the GLSL 1 - 0.45*w).
inline float bridgeWaterlineAlbedoScale(float band, float strength) {
    const float w = band < 0.f ? 0.f : (band > 1.f ? 1.f : band) *
                                      (strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength));
    return 1.f - 0.45f * w;
}

} // namespace fuhglgraphics::render
