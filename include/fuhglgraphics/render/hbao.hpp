#pragma once

/// @file hbao.hpp
/// @brief Horizon-based ambient occlusion (HBAO, G25) — pure (GL-free) CPU twin of the
/// horizon march in kHbaoFrag: for `dirs` tangent-plane directions the march samples the
/// occluder height above the tangent plane along each ray; the maximum horizon elevation
/// beta (sin beta = height / distance) occludes the cosine-weighted hemisphere around N
/// by cos(beta). Unit-testable without a GL context.

#include <fuhgl/math/vec.hpp>

#include <cmath>
#include <functional>

namespace fuhglgraphics::render {

/// @brief One direction's horizon contribution: cos(beta) for the max horizon elevation
/// `sinBetaMax` above the tangent plane (<= 0 = no occluder -> 1).
inline float hbaoDirectionFactor(float sinBetaMax) {
    if (sinBetaMax <= 0.f) return 1.f;
    const float s = sinBetaMax > 1.f ? 1.f : sinBetaMax;
    const float beta = std::asin(s);
    const float c = std::cos(beta);
    return c < 0.f ? 0.f : c;
}

/// @brief Full HBAO factor at point `P` with unit normal `N` (world space), mirroring
/// kHbaoFrag: `dirs` directions rotated by `rotSeed` (radians), `steps` march samples at
/// radii radius*s/steps. `sample(dir, r, heightAbove, len)` reports the scene surface at
/// world offset `dir * r` from P: its height above the tangent plane (dot(S-P, N)) and the
/// distance len = |S-P|. Samples with heightAbove <= bias are ignored (self-occlusion).
/// Returns the combined AO factor in [0, 1] (1 = unoccluded), raised to `power`.
inline float hbaoFactor(fuhgl::math::Vec3 P, fuhgl::math::Vec3 N, int dirs, float radius, int steps,
                        float bias, float power, float rotSeed,
                        const std::function<void(fuhgl::math::Vec2 dir, float r, float& heightAbove,
                                                 float& len)>& sample) {
    N = fuhgl::math::normalize(N);
    const fuhgl::math::Vec3 up = std::fabs(N.y) < 0.999f ? fuhgl::math::Vec3{0.f, 1.f, 0.f}
                                                        : fuhgl::math::Vec3{1.f, 0.f, 0.f};
    const fuhgl::math::Vec3 T = fuhgl::math::normalize(fuhgl::math::cross(up, N));
    const fuhgl::math::Vec3 B = fuhgl::math::cross(N, T);
    const float cr = std::cos(rotSeed), sr = std::sin(rotSeed);
    const int nd = dirs < 1 ? 1 : (dirs > 16 ? 16 : dirs);
    const int ns = steps < 1 ? 1 : (steps > 8 ? 8 : steps);
    const float rad = radius > 1e-5f ? radius : 1e-5f;
    float ao = 1.f;
    for (int d = 0; d < nd; ++d) {
        const float phi = 6.28318530718f * static_cast<float>(d) / static_cast<float>(nd);
        const float c = std::cos(phi), s = std::sin(phi);
        const fuhgl::math::Vec2 dir2{c * cr - s * sr, c * sr + s * cr};
        const fuhgl::math::Vec3 dir = T * dir2.x + B * dir2.y; // unit, in the tangent plane
        float sinBetaMax = -1.5707963f;
        for (int st = 1; st <= ns; ++st) {
            const float r = rad * static_cast<float>(st) / static_cast<float>(ns);
            float heightAbove = 0.f, len = 0.f;
            if (sample) sample(dir2, r, heightAbove, len);
            if (heightAbove <= bias || len <= 1e-5f) continue;
            const float sinB = heightAbove / len;
            if (sinB > sinBetaMax) sinBetaMax = sinB;
        }
        ao *= hbaoDirectionFactor(sinBetaMax);
    }
    if (ao < 0.f) ao = 0.f;
    if (ao > 1.f) ao = 1.f;
    return std::pow(ao, power > 0.f ? power : 1.f);
}

} // namespace fuhglgraphics::render
