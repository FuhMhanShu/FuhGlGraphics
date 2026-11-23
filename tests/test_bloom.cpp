// Unit tests for the G17 bloom pure helpers (no GL context needed): the bright-pass
// threshold function and the deterministic blur tap pattern.

#include "test_framework.hpp"

#include <fuhglgraphics/render/bloom.hpp>

using namespace fuhglgraphics::render;

void runBloomTests() {
    // --- bloomBrightPass ---
    CHECK_NEAR(bloomBrightPass(0.5f, 1.0f), 0.f, 1e-6);   // below threshold: cut
    CHECK_NEAR(bloomBrightPass(1.0f, 1.0f), 0.f, 1e-6);   // at threshold: 0
    CHECK_NEAR(bloomBrightPass(3.0f, 1.0f), 2.0f, 1e-6);  // above: c - threshold
    CHECK_NEAR(bloomBrightPass(-2.f, 1.0f), 0.f, 1e-6);   // negative input: 0
    CHECK_NEAR(bloomBrightPass(5.f, 0.f), 5.f, 1e-6);     // threshold 0: everything passes

    // --- bloomTaps: count / determinism / radius growth ---
    {
        const auto t = bloomTaps(9);
        CHECK(t.size() == 9);
        for (const auto& v : t) {
            const float r = fuhgl::math::length(v);
            CHECK(r > 0.3f && r <= 1.f + 1e-5f);
        }
        CHECK(fuhgl::math::length(t.back()) > fuhgl::math::length(t.front())); // radius grows
        const auto t2 = bloomTaps(9);
        for (std::size_t i = 0; i < t.size(); ++i) {
            CHECK_NEAR(t[i].x, t2[i].x, 1e-6);
            CHECK_NEAR(t[i].y, t2[i].y, 1e-6);
        }
        CHECK(bloomTaps(0).empty());
        CHECK(bloomTaps(-1).empty());
        // taps spread over the disk (not all in one direction)
        float maxDot = -2.f;
        for (std::size_t i = 1; i < t.size(); ++i) { // skip tap 0 (dot with itself = 1)
            const float d = fuhgl::math::dot(fuhgl::math::normalize(t[i]), fuhgl::math::normalize(t.front()));
            maxDot = maxDot < d ? d : maxDot;
        }
        CHECK(maxDot < 0.99f);
    }
}
