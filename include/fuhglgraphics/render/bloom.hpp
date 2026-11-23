#pragma once

/// @file bloom.hpp
/// @brief G17 bloom — pure (GL-free) helpers of the bloom post pass: the bright-pass
/// threshold function and the deterministic poisson-ish tap offsets of the blur.
/// Unit-testable.

#include <fuhgl/math/vec.hpp>

#include <cmath>
#include <vector>

namespace fuhglgraphics::render {

/// @brief Bright-pass: everything above `threshold` passes (HDR), below is cut.
/// `c` is a linear HDR luminance-ish channel value, result >= 0.
inline float bloomBrightPass(float c, float threshold) {
    return c > threshold ? c - threshold : 0.f;
}

/// @brief Deterministic blur tap offsets (poisson-ish, golden angle), `count` taps with
/// radius growing 0.35..1.0 — scaled by uRadius * texel in the shader.
inline std::vector<fuhgl::math::Vec2> bloomTaps(int count) {
    std::vector<fuhgl::math::Vec2> taps;
    if (count <= 0) return taps;
    taps.reserve(static_cast<std::size_t>(count));
    constexpr float kGolden = 2.39996323f; // radians
    const float n = static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float a = static_cast<float>(i) * kGolden;
        const float r = 0.35f + 0.65f * (static_cast<float>(i) / n);
        taps.push_back({std::cos(a) * r, std::sin(a) * r});
    }
    return taps;
}

} // namespace fuhglgraphics::render
