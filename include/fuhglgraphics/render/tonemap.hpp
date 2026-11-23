#pragma once

/// @file tonemap.hpp
/// @brief G23 tonemapping operators — `render::TonemapOperator` (ACES / Reinhard / Uncharted2 /
/// linear) selected per frame in the G3 tonemap pass (`uOperator` in kTonemapFrag), plus pure
/// CPU twins of each operator for the linear HDR value (before gamma). Unit-testable.
/// The bloom threshold of G17 (`bloomThreshold`) is adjustable the same way.

#include <fuhgl/math/vec.hpp>

#include <cmath>
#include <string>

namespace fuhglgraphics::render {

/// @brief Tonemapping operators of the final pass (applied to `hdr * exposure`, then gamma).
enum class TonemapOperator {
    Aces = 0,      ///< ACES filmic fit (Narkowicz approximation) — the G3 default
    Reinhard = 1,  ///< x / (1 + x) — simple, rolls off to 1
    Uncharted2 = 2, ///< Hable "Uncharted 2" filmic with white point 11.2 — punchy highlights
    Linear = 3,    ///< plain clamp — for debugging/comparison
};

constexpr int kTonemapOperatorCount = 4;

/// @brief Human-readable name (logs, config, scene serialization).
inline const char* tonemapOperatorName(TonemapOperator op) {
    switch (op) {
        case TonemapOperator::Aces: return "aces";
        case TonemapOperator::Reinhard: return "reinhard";
        case TonemapOperator::Uncharted2: return "uncharted2";
        case TonemapOperator::Linear: return "linear";
    }
    return "unknown";
}

/// @brief Inverse of tonemapOperatorName(). False when unknown.
inline bool tonemapOperatorFromName(const std::string& name, TonemapOperator& out) {
    for (int i = 0; i < kTonemapOperatorCount; ++i) {
        const TonemapOperator op = static_cast<TonemapOperator>(i);
        if (name == tonemapOperatorName(op)) {
            out = op;
            return true;
        }
    }
    return false;
}

/// @brief ACES filmic approximation (Narkowicz), per channel, clamped to [0, 1].
inline float tonemapAces(float x) {
    if (x <= 0.f) return 0.f;
    const float c = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    return c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
}

/// @brief Reinhard: x / (1 + x), in [0, 1) for x >= 0.
inline float tonemapReinhard(float x) {
    return x <= 0.f ? 0.f : x / (1.f + x);
}

/// @brief Uncharted 2 (Hable) filmic curve with white point W = 11.2, per channel, [0, 1].
inline float tonemapUncharted2(float x) {
    constexpr float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f, W = 11.2f;
    if (x <= 0.f) return 0.f;
    const float cur = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    const float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    const float r = cur / white;
    return r < 0.f ? 0.f : (r > 1.f ? 1.f : r);
}

/// @brief Linear operator: plain clamp.
inline float tonemapLinear(float x) {
    return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}

/// @brief Applies `op` to one linear HDR channel (after exposure, before gamma).
inline float tonemapApply(TonemapOperator op, float x) {
    switch (op) {
        case TonemapOperator::Aces: return tonemapAces(x);
        case TonemapOperator::Reinhard: return tonemapReinhard(x);
        case TonemapOperator::Uncharted2: return tonemapUncharted2(x);
        case TonemapOperator::Linear: return tonemapLinear(x);
    }
    return tonemapAces(x);
}

/// @brief Applies `op` to an RGB color (after exposure, before gamma).
inline fuhgl::math::Vec3 tonemapApply(TonemapOperator op, fuhgl::math::Vec3 c) {
    return {tonemapApply(op, c.x), tonemapApply(op, c.y), tonemapApply(op, c.z)};
}

} // namespace fuhglgraphics::render
