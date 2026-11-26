#pragma once

/// @file sky.hpp
/// @brief G38 procedural sky (Effect::Sky) — pure (GL-free) twins of the sky branch in the
/// G3 lighting pass: a Rayleigh-ish gradient by sun elevation, an HDR sun disc + halo, and
/// a wind-scrolled 2D fBm cloud layer whose coverage/density come from the weather state
/// (weather::WeatherParams). The GLSL pass in deferred_glsl.hpp (kLightFragSky) mirrors
/// these formulas with its own hash noise; this header is the unit-testable CPU twin built
/// on the shared G24 procedural::Noise.

#include "../procedural/noise.hpp"
#include "../weather/weather.hpp"

#include <fuhgl/math/vec.hpp>

#include <cmath>

namespace fuhglgraphics::render {

/// @brief Sky gradient colors by sun elevation: zenith/horizon at day, dark at night.
/// `sunEl` = sin of the sun elevation (-1..1), `cloudShade` darkens the sky under clouds.
struct SkyGradient {
    fuhgl::math::Vec3 zenith;   ///< overhead sky color
    fuhgl::math::Vec3 horizon;  ///< horizon sky color
};

/// @brief Day/night sky gradient for a sun elevation (`sunEl` = ray.y of the sun direction).
/// Warm tint near the horizon at low sun (sunrise/sunset). Pure function of the elevation.
inline SkyGradient skyGradient(float sunEl, float cloudShade = 1.f) {
    const float day = fuhgl::math::smoothstep(-0.05f, 0.15f, sunEl);
    const fuhgl::math::Vec3 dayZen{0.16f, 0.34f, 0.72f};
    const fuhgl::math::Vec3 dayHor{0.72f, 0.80f, 0.92f};
    const fuhgl::math::Vec3 duskZen{0.10f, 0.12f, 0.30f};
    const fuhgl::math::Vec3 duskHor{0.95f, 0.55f, 0.28f}; // low-sun warm horizon
    const fuhgl::math::Vec3 nightZen{0.008f, 0.012f, 0.030f};
    const fuhgl::math::Vec3 nightHor{0.020f, 0.028f, 0.060f};
    // warm horizon near sunrise/sunset: dusk peaks at sunEl ~ 0 and vanishes at zenith/nadir
    const float dusk = fuhgl::math::clamp(1.f - std::fabs(sunEl) / 0.22f, 0.f, 1.f) * (1.f - day) +
                       fuhgl::math::clamp(sunEl / 0.18f, 0.f, 1.f) * day * (1.f - fuhgl::math::clamp(sunEl / 0.3f, 0.f, 1.f));
    SkyGradient g;
    g.zenith = fuhgl::math::lerp(fuhgl::math::lerp(nightZen, duskZen, dusk), dayZen, day);
    g.horizon = fuhgl::math::lerp(fuhgl::math::lerp(nightHor, duskHor, dusk), dayHor, day);
    g.zenith = g.zenith * cloudShade;
    g.horizon = g.horizon * cloudShade;
    return g;
}

/// @brief 2D fBm cloud field in [0, 1] sampled on the sky dome: the view ray `dir`
/// (normalized) is projected onto the plane y = cloudHeight (perspective-ish), scrolled by
/// the wind (`windSpeed` along `windDir`) and `time`. Seeded via G24 Noise (simplex fBm).
inline float skyCloudField(fuhgl::math::Vec3 dir, const procedural::Noise& noise, float cloudHeight,
                           float windSpeed, float windDir, float time, float scale) {
    const float y = std::max(dir.y, 0.02f); // avoid the zenith singularity of the projection
    const float px = dir.x / y * cloudHeight;
    const float pz = dir.z / y * cloudHeight;
    // windDirection convention (weather.hpp): radians in the XZ plane, 0 = +Z
    const float wx = std::sin(windDir) * windSpeed * time * 0.35f;
    const float wz = std::cos(windDir) * windSpeed * time * 0.35f;
    const float s = scale > 1e-4f ? scale : 1e-4f;
    // domain-warped fBm -> billowy clouds; remapped from [-1,1] to [0,1]
    const float w = noise.fbm2(px * 0.11f + 5.2f, pz * 0.11f + 1.3f, 2, procedural::Noise::Base::Simplex);
    const float n = noise.fbm2((px + wx + w * 1.6f) / s, (pz + wz - w * 1.2f) / s, 4,
                               procedural::Noise::Base::Simplex);
    return n * 0.5f + 0.5f;
}

/// @brief Cloud cover mask in [0, 1] for one sky direction: `coverage` shifts the fBm
/// threshold (0 = clear sky everywhere, 1 = overcast), `density` darkens/thickens the
/// remaining clouds. Pure function — unit-testable twin of the GLSL cloudLayer().
inline float skyCloudCover(fuhgl::math::Vec3 dir, const procedural::Noise& noise, float coverage,
                           float density, float windSpeed, float windDir, float time, float scale,
                           float cloudHeight = 60.f) {
    if (coverage <= 0.001f) return 0.f;
    const float f = skyCloudField(dir, noise, cloudHeight, windSpeed, windDir, time, scale);
    // threshold the field: f < (1 - coverage) -> clear
    const float t = 1.f - coverage;
    float m = fuhgl::math::smoothstep(t - 0.12f, t + 0.12f, f);
    // density scales how dark/opaque the covered part is
    return m * fuhgl::math::clamp(density, 0.f, 1.f);
}

/// @brief Full procedural-sky color for a view ray (twin of proceduralSky() in kLightFragSky):
/// gradient + sun disc/halo + cloud layer (white lit by the sun, grey shaded) + lightning
/// flash tint. `sunDir` points TO the sun; `sunColor` is the HDR sun radiance (0 = night).
inline fuhgl::math::Vec3 proceduralSkyColor(fuhgl::math::Vec3 ray, fuhgl::math::Vec3 sunDir,
                                            fuhgl::math::Vec3 sunColor, const weather::WeatherParams& w,
                                            const procedural::Noise& noise, float time,
                                            float lightningFlash = 0.f) {
    const float sunEl = sunDir.y;
    const float shade = (1.f - 0.75f * w.cloudCoverage * w.cloudDensity) * (1.f - 0.4f * w.stormIntensity);
    const SkyGradient g = skyGradient(sunEl, shade);
    const float h = fuhgl::math::clamp(ray.y, 0.f, 1.f);
    fuhgl::math::Vec3 sky = fuhgl::math::lerp(g.horizon, g.zenith, std::pow(h, 0.65f));
    // sun disc + halo (HDR — bloom/god rays pick it up)
    const float cosA = ray.x * sunDir.x + ray.y * sunDir.y + ray.z * sunDir.z;
    sky += sunColor * (std::max(cosA, 0.f) > 0.99955f ? 1.f : 0.f) * w.sunVisibility;
    sky += sunColor * std::pow(std::max(cosA, 0.f), 350.f) * 0.6f * w.sunVisibility;
    // cloud layer
    const float cover = skyCloudCover(ray, noise, w.cloudCoverage, w.cloudDensity, w.windSpeed,
                                      w.windDirection, time, 6.f);
    if (cover > 0.f) {
        const float sunUp = std::max(sunDir.y, 0.f);
        const fuhgl::math::Vec3 cloudLit = fuhgl::math::lerp(fuhgl::math::Vec3{0.75f, 0.78f, 0.85f},
                                                            fuhgl::math::Vec3{1.05f, 1.0f, 0.92f}, sunUp) *
                                           (0.55f + 0.45f * w.sunVisibility);
        const fuhgl::math::Vec3 cloudDark{0.16f, 0.17f, 0.20f};
        fuhgl::math::Vec3 cloud = fuhgl::math::lerp(cloudLit, cloudDark, 1.f - w.cloudDensity * 0.5f);
        cloud = cloud * shade;
        sky = fuhgl::math::lerp(sky, cloud, fuhgl::math::clamp(cover, 0.f, 1.f));
    }
    // lightning sky flash
    if (lightningFlash > 0.f) sky += fuhgl::math::Vec3{0.75f, 0.82f, 1.0f} * lightningFlash * 2.5f;
    return sky;
}

} // namespace fuhglgraphics::render
