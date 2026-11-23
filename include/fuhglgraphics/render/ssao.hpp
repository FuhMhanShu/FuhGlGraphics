#pragma once

/// @file ssao.hpp
/// @brief G15 SSAO — pure (GL-free) helpers of the screen-space ambient occlusion pass:
/// the deterministic hemisphere sample kernel, the per-pixel kernel rotation hash and the
/// depth linearization used when comparing against the G-buffer depth. Unit-testable.

#include <fuhgl/math/vec.hpp>

#include <vector>

namespace fuhglgraphics::render {

/// @brief Deterministic hemisphere sample kernel: `samples` unit-ish vectors with z >= 0,
/// scaled by lerp(0.1, 1, (i/n)^2) so most samples sit near the origin. Same seed -> same
/// kernel (pure function of `samples` + `seed`).
std::vector<fuhgl::math::Vec3> ssaoKernel(int samples, unsigned seed);

/// @brief Deterministic per-pixel 2D rotation angle in [0, 2*pi) from pixel coords + seed
/// (hash-based, same for a given pixel on every frame — no temporal shimmer by itself).
float ssaoRotation(unsigned px, unsigned py, unsigned seed);

/// @brief Linear view-space distance of a non-linear (perspective) depth-buffer value `d`
/// in [0, 1] for the given near/far planes. d = 0 -> ~2*zn, d = 1 -> zf.
inline float linearizeDepth(float d, float zn, float zf) {
    const float denom = zf + zn - d * (zf - zn);
    return (2.f * zn * zf) / (denom > 1e-6f ? denom : 1e-6f);
}

} // namespace fuhglgraphics::render
