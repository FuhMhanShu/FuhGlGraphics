// Unit tests for the G16 SSR pure helpers (no GL context needed): the screen-edge fade and
// the roughness/fresnel/metallic reflection mask.

#include "test_framework.hpp"

#include <fuhglgraphics/render/ssr.hpp>

using namespace fuhglgraphics::render;

void runSsrTests() {
    // --- ssrEdgeFade: 1 in the center, 0 at the border, smooth ---
    CHECK_NEAR(ssrEdgeFade({0.5f, 0.5f}), 1.f, 1e-6);
    CHECK_NEAR(ssrEdgeFade({0.0f, 0.5f}), 0.f, 1e-6);
    CHECK_NEAR(ssrEdgeFade({1.0f, 1.0f}), 0.f, 1e-6);
    CHECK_NEAR(ssrEdgeFade({0.5f, 0.04f}, 0.08f), 0.5f * 0.5f * (3.f - 2.f * 0.5f), 1e-5);
    {
        // monotonic from border to center
        float prev = -1.f;
        for (int i = 0; i <= 20; ++i) {
            const float f = ssrEdgeFade({0.02f + 0.04f * i, 0.5f}, 0.08f);
            CHECK(f >= prev - 1e-6f);
            prev = f;
        }
    }

    // --- ssrMask: smooth mirror reflects, rough does not, metal at grazing angles ---
    CHECK_NEAR(ssrMask(0.f, 1.f, 0.f), 0.04, 1e-5);   // dielectric, head-on: F0
    CHECK_NEAR(ssrMask(0.f, 0.f, 0.f), 1.0, 1e-5);    // dielectric, grazing: 1
    CHECK(ssrMask(1.f, 1.f, 0.f) < 0.01f);             // fully rough: almost no reflection
    CHECK(ssrMask(0.5f, 1.f, 0.f) < ssrMask(0.1f, 1.f, 0.f)); // rougher -> less
    CHECK(ssrMask(0.f, 0.2f, 1.f) > ssrMask(0.f, 0.2f, 0.f)); // metal > dielectric at grazing
    CHECK_NEAR(ssrMask(0.f, 1.f, 1.f), 1.0, 1e-5);    // perfect metal head-on: F0 = 1
    // clamped inputs stay in [0, 1]
    CHECK(ssrMask(-1.f, 2.f, 5.f) >= 0.f && ssrMask(-1.f, 2.f, 5.f) <= 1.f);
}
