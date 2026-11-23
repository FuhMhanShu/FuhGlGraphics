// Unit tests for the G15 SSAO pure helpers (no GL context needed): the deterministic
// hemisphere kernel, the per-pixel rotation hash and the depth linearization.

#include "test_framework.hpp"

#include <fuhglgraphics/render/ssao.hpp>

#include <fuhgl/math/func.hpp>

#include <cmath>

using namespace fuhglgraphics::render;

void runSsaoTests() {
    // --- ssaoKernel: size / hemisphere / scale / determinism ---
    {
        const auto k = ssaoKernel(24, 1337u);
        CHECK(k.size() == 24);
        for (const auto& v : k) {
            CHECK(v.z >= 0.f);                                  // hemisphere
            CHECK(fuhgl::math::length(v) <= 1.f + 1e-5f);       // unit-ish, scaled <= 1
            CHECK(fuhgl::math::length(v) >= 0.1f - 1e-5f);      // scale >= 0.1
        }
        // scale grows monotonically by construction: |k[i]| = 0.1 + 0.9*(i/n)^2
        CHECK(fuhgl::math::length(k.back()) > fuhgl::math::length(k.front()));
        // determinism: same seed -> identical kernel
        const auto k2 = ssaoKernel(24, 1337u);
        CHECK(k2.size() == k.size());
        for (std::size_t i = 0; i < k.size(); ++i) {
            CHECK_NEAR(k[i].x, k2[i].x, 1e-6);
            CHECK_NEAR(k[i].y, k2[i].y, 1e-6);
            CHECK_NEAR(k[i].z, k2[i].z, 1e-6);
        }
        // different seed -> different kernel
        const auto k3 = ssaoKernel(24, 42u);
        bool anyDiff = false;
        for (std::size_t i = 0; i < k.size(); ++i)
            if (std::fabs(k[i].x - k3[i].x) > 1e-6) anyDiff = true;
        CHECK(anyDiff);
        // edge cases
        CHECK(ssaoKernel(0, 1u).empty());
        CHECK(ssaoKernel(-3, 1u).empty());
    }

    // --- ssaoRotation: deterministic, in [0, 2*pi) ---
    for (unsigned seed = 0; seed < 4; ++seed) {
        const float a = ssaoRotation(13, 7, seed);
        const float b = ssaoRotation(13, 7, seed);
        CHECK_NEAR(a, b, 1e-6);                 // same pixel + seed -> same rotation
        CHECK(a >= 0.f && a < 2.f * fuhgl::math::kPi);
        CHECK(std::fabs(a - ssaoRotation(14, 7, seed)) > 1e-7f); // other pixel -> other angle
    }

    // --- linearizeDepth: d=0 -> ~2*zn, d=1 -> zf, monotonic ---
    const float zn = 0.1f, zf = 200.f;
    CHECK_NEAR(linearizeDepth(0.f, zn, zf), 2.f * zn, 1e-3);
    CHECK_NEAR(linearizeDepth(1.f, zn, zf), zf, 0.1);
    float prev = 0.f;
    for (int i = 0; i <= 100; ++i) {
        const float d = i / 100.f;
        const float lin = linearizeDepth(d, zn, zf);
        CHECK(lin > prev - 1e-4f); // monotonically increasing
        prev = lin;
    }
    CHECK(linearizeDepth(0.5f, zn, zf) > 2.f * zn && linearizeDepth(0.5f, zn, zf) < zf);
}
