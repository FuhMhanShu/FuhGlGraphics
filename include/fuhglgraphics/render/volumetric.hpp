#pragma once

/// @file volumetric.hpp
/// @brief G21 volumetric fog / clouds — pure (GL-free) helpers of the "volumetric" post pass:
/// a deterministic 3D value-noise/fBm field, the height-falloff density of the volume, the
/// Henyey–Greenstein phase function and a CPU reference raymarcher (single-scattering
/// in-scatter + transmittance along a ray). The GLSL pass in the G3 renderer mirrors these
/// formulas; this header is the unit-testable twin.

#include <fuhgl/math/vec.hpp>

#include <cmath>

namespace fuhglgraphics::render {

/// @brief Deterministic 3D hash -> [0, 1] (same input -> same output, no state).
inline float volumetricHash(fuhgl::math::Vec3 p, unsigned seed) {
    const float s = static_cast<float>(seed) * 0.1031f;
    p.x += s;
    p.y += s * 0.7f;
    p.z += s * 1.3f;
    float k = p.x * 12.9898f + p.y * 78.233f + p.z * 37.719f;
    k = k - std::floor(k);
    // second scramble for better distribution
    k = k * k * (3.f - 2.f * k);
    const float k2 = (p.x + 31.7f) * (p.y + 11.3f) * (p.z + 17.9f) + s;
    return (k + (k2 - std::floor(k2))) * 0.5f;
}

/// @brief Trilinear-interpolated 3D value noise in [0, 1] at `p` (grid cells of size 1).
inline float volumetricNoise(fuhgl::math::Vec3 p, unsigned seed = 0u) {
    const int x0 = static_cast<int>(std::floor(p.x));
    const int y0 = static_cast<int>(std::floor(p.y));
    const int z0 = static_cast<int>(std::floor(p.z));
    const float fx = p.x - static_cast<float>(x0);
    const float fy = p.y - static_cast<float>(y0);
    const float fz = p.z - static_cast<float>(z0);
    const float u = fx * fx * (3.f - 2.f * fx);
    const float v = fy * fy * (3.f - 2.f * fy);
    const float w = fz * fz * (3.f - 2.f * fz);
    auto val = [&](int x, int y, int z) {
        return volumetricHash({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}, seed);
    };
    const float c00 = val(x0, y0, z0) + u * (val(x0 + 1, y0, z0) - val(x0, y0, z0));
    const float c10 = val(x0, y0 + 1, z0) + u * (val(x0 + 1, y0 + 1, z0) - val(x0, y0 + 1, z0));
    const float c01 = val(x0, y0, z0 + 1) + u * (val(x0 + 1, y0, z0 + 1) - val(x0, y0, z0 + 1));
    const float c11 = val(x0, y0 + 1, z0 + 1) + u * (val(x0 + 1, y0 + 1, z0 + 1) - val(x0, y0 + 1, z0 + 1));
    const float c0 = c00 + v * (c10 - c00);
    const float c1 = c01 + v * (c11 - c01);
    return c0 + w * (c1 - c0);
}

/// @brief Fractal Brownian motion: `octaves` layers of value noise, amplitude halving — [0, 1].
inline float volumetricFbm(fuhgl::math::Vec3 p, int octaves, unsigned seed = 0u) {
    float sum = 0.f, amp = 0.5f, norm = 0.f;
    for (int i = 0; i < octaves && i < 8; ++i) {
        sum += amp * volumetricNoise(p, seed + static_cast<unsigned>(i) * 101u);
        norm += amp;
        amp *= 0.5f;
        p = p * 2.03f + fuhgl::math::Vec3{13.7f, 7.1f, 5.3f};
    }
    return norm > 0.f ? sum / norm : 0.f;
}

/// @brief Volume density at a world position: exponential height falloff times
/// (base + cloudiness * fBm noise). `heightFalloff` = scale height (world units),
/// `noiseScale` = world units per noise cell, `cloudiness` in [0, 1] blends base fog and
/// billowy clouds. Result >= 0 (per world unit).
inline float volumetricDensity(fuhgl::math::Vec3 worldPos, float baseDensity, float cloudiness,
                               float heightFalloff, float noiseScale, unsigned seed = 0u) {
    const float h = heightFalloff > 1e-4f ? heightFalloff : 1e-4f;
    const float fall = std::exp(-std::max(worldPos.y, 0.f) / h);
    float n = 1.f;
    if (cloudiness > 0.f && noiseScale > 1e-6f)
        n = 1.f - cloudiness + cloudiness * 2.f * volumetricFbm(worldPos / noiseScale, 4, seed);
    return baseDensity * fall * n;
}

/// @brief Henyey–Greenstein phase function for scattering angle cosine `cosTheta` and
/// anisotropy `g` in (-1, 1). Forward peak for g -> 1, isotropic at g = 0.
inline float henyeyGreenstein(float cosTheta, float g) {
    const float gg = g * g;
    const float denom = 1.f + gg - 2.f * g * cosTheta;
    return (1.f - gg) / (4.f * 3.14159265f * denom * std::sqrt(std::max(denom, 1e-6f)));
}

/// @brief Parameters of the CPU reference raymarcher (mirrors the GLSL uniforms).
struct VolumetricParams {
    float baseDensity = 0.02f;   ///< fog density at ground level (per world unit)
    float cloudiness = 0.6f;     ///< 0 = uniform fog, 1 = fully noise-driven clouds
    float heightFalloff = 8.f;   ///< scale height of the exponential falloff
    float noiseScale = 6.f;      ///< world units per noise cell
    unsigned seed = 1337u;
    float strength = 1.f;        ///< scales the in-scattered light
    float maxDist = 120.f;       ///< march end for sky pixels (world units)
    int steps = 24;              ///< march samples
    float anisotropy = 0.6f;     ///< Henyey–Greenstein g
};

/// @brief Result of volumetricMarch(): accumulated in-scattered light (radiance) and the
/// remaining transmittance along the ray.
struct VolumetricResult {
    fuhgl::math::Vec3 inscatter{0.f, 0.f, 0.f};
    float transmittance = 1.f;
};

/// @brief CPU reference of the "volumetric" post pass: marches the ray from `origin` along
/// `dir` (normalized) up to `tMax` (scene distance for geometry, params.maxDist for sky),
/// accumulating single scattering of `sunColor` from `sunDir` (phase function) plus a flat
/// `ambient` term. Mirrors the GLSL loop so the pass can be unit-tested without GL.
inline VolumetricResult volumetricMarch(fuhgl::math::Vec3 origin, fuhgl::math::Vec3 dir, float tMax,
                                        const VolumetricParams& prm, fuhgl::math::Vec3 sunDir,
                                        fuhgl::math::Vec3 sunColor, fuhgl::math::Vec3 ambient) {
    VolumetricResult r;
    if (tMax <= 0.f || prm.steps <= 0 || prm.baseDensity <= 0.f) return r;
    const float ds = tMax / static_cast<float>(prm.steps);
    const float cosTheta = -(dir.x * sunDir.x + dir.y * sunDir.y + dir.z * sunDir.z);
    const float phase = henyeyGreenstein(cosTheta, prm.anisotropy);
    float t = 0.5f * ds;
    for (int i = 0; i < prm.steps; ++i, t += ds) {
        const fuhgl::math::Vec3 p = origin + dir * t;
        const float d = volumetricDensity(p, prm.baseDensity, prm.cloudiness, prm.heightFalloff,
                                         prm.noiseScale, prm.seed);
        const float tau = d * ds;
        r.inscatter += (sunColor * phase + ambient) * (d * ds) * r.transmittance * prm.strength;
        r.transmittance *= std::exp(-tau);
    }
    r.transmittance = std::max(r.transmittance, 0.f);
    return r;
}

} // namespace fuhglgraphics::render
