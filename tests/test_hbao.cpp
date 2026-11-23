// Unit tests for the HBAO CPU twin (no GL context needed): hbaoFactor mirrors the
// horizon march of kHbaoFrag — no occluder -> 1, a tall occluder in one direction lowers
// the factor by cos(beta) per direction, bias ignores low occluders, power scales,
// direction/step clamping and determinism.

#include "test_framework.hpp"

#include <fuhglgraphics/render/hbao.hpp>

#include <cmath>

using namespace fuhglgraphics::render;
using fuhgl::math::Vec2;
using fuhgl::math::Vec3;

void runHbaoTests() {
    // --- no occluder anywhere: factor 1 ---
    {
        const auto none = [](Vec2, float, float& h, float& len) { h = -1.f; len = 1.f; };
        const float ao = hbaoFactor({0, 0, 0}, {0, 1, 0}, 8, 2.f, 4, 0.05f, 1.6f, 0.f, none);
        CHECK_NEAR(ao, 1.f, 1e-6f);
    }
    // --- wall to the east (+X), height 2 at distance 4 -> sinB = 0.5, beta = 30 deg ---
    // dirs = 8 at phi = k*45 deg: dir.x = sin(phi) > 0.5 for k = 1,2,3 (45/90/135 deg)
    {
        const auto wall = [](Vec2 dir, float r, float& h, float& len) {
            if (dir.y > 0.5f) { h = 2.f; len = 4.f; } else { h = -1.f; len = 1.f; } // dir.y = world dir.x for N=(0,1,0)
        };
        const float ao = hbaoFactor({0, 0, 0}, {0, 1, 0}, 8, 4.f, 1, 0.05f, 1.f, 0.f, wall);
        const float expect = std::pow(std::cos(std::asin(0.5f)), 3.f); // 3 of 8 directions occluded
        CHECK_NEAR(ao, expect, 1e-5f);
    }
    // --- same wall, but bias above the occluder height -> ignored -> factor 1 ---
    {
        const auto wall = [](Vec2 dir, float r, float& h, float& len) {
            if (dir.y > 0.5f) { h = 2.f; len = 4.f; } else { h = -1.f; len = 1.f; } // dir.y = world dir.x for N=(0,1,0)
        };
        const float ao = hbaoFactor({0, 0, 0}, {0, 1, 0}, 8, 4.f, 1, 2.5f, 1.f, 0.f, wall);
        CHECK_NEAR(ao, 1.f, 1e-6f);
    }
    // --- full enclosure (occluder in every direction, sinB = 1 -> beta = 90 deg -> cos = 0) ---
    {
        const auto dome = [](Vec2, float, float& h, float& len) { h = 4.f; len = 4.f; };
        const float ao = hbaoFactor({0, 0, 0}, {0, 1, 0}, 8, 4.f, 2, 0.05f, 1.f, 0.f, dome);
        CHECK_NEAR(ao, 0.f, 1e-6f);
    }
    // --- power exponent: ao^power with power 2 halves-ish ---
    {
        const auto wall = [](Vec2 dir, float r, float& h, float& len) {
            if (dir.y > 0.5f) { h = 2.f; len = 4.f; } else { h = -1.f; len = 1.f; } // dir.y = world dir.x for N=(0,1,0)
        };
        const float ao1 = hbaoFactor({0, 0, 0}, {0, 1, 0}, 8, 4.f, 1, 0.05f, 1.f, 0.f, wall);
        const float ao2 = hbaoFactor({0, 0, 0}, {0, 1, 0}, 8, 4.f, 1, 0.05f, 2.f, 0.f, wall);
        CHECK_NEAR(ao2, ao1 * ao1, 1e-5f);
    }
    // --- march takes the max over steps (near step lower, far step higher) ---
    {
        int calls = 0;
        const auto steps = [&](Vec2 dir, float r, float& h, float& len) {
            ++calls;
            if (dir.y > 0.5f) { h = r < 1.f ? 0.5f : 3.f; len = 4.f; } else { h = -1.f; len = 1.f; } // dir.y = world dir.x
        };
        const float ao = hbaoFactor({0, 0, 0}, {0, 1, 0}, 8, 4.f, 4, 0.05f, 1.f, 0.f, steps);
        const float expect = std::pow(std::cos(std::asin(3.f / 4.f)), 3.f); // far step wins
        CHECK_NEAR(ao, expect, 1e-5f);
        CHECK(calls == 8 * 4);
    }
    // --- clamping: dirs/steps beyond limits, negative radius; determinism ---
    {
        const auto wall = [](Vec2 dir, float r, float& h, float& len) {
            if (dir.y > 0.5f) { h = 2.f; len = 4.f; } else { h = -1.f; len = 1.f; } // dir.y = world dir.x for N=(0,1,0)
        };
        const float a = hbaoFactor({0, 0, 0}, {0, 1, 0}, 1000, -3.f, 100, 0.05f, 1.f, 0.3f, wall);
        const float b = hbaoFactor({0, 0, 0}, {0, 1, 0}, 16, 1e-5f, 8, 0.05f, 1.f, 0.3f, wall);
        const float c = hbaoFactor({0, 0, 0}, {0, 1, 0}, 1000, -3.f, 100, 0.05f, 1.f, 0.3f, wall);
        CHECK(a == c);          // deterministic
        CHECK(a >= 0.f && a <= 1.f);
        CHECK(b >= 0.f && b <= 1.f);
    }
    // --- hbaoDirectionFactor edge cases ---
    {
        CHECK_NEAR(hbaoDirectionFactor(-0.9f), 1.f, 1e-6f); // below the tangent plane: free
        CHECK_NEAR(hbaoDirectionFactor(0.f), 1.f, 1e-6f);
        CHECK_NEAR(hbaoDirectionFactor(1.f), 0.f, 1e-6f);  // horizon straight up: fully occluded
        CHECK_NEAR(hbaoDirectionFactor(0.5f), std::cos(std::asin(0.5f)), 1e-6f);
        CHECK_NEAR(hbaoDirectionFactor(2.f), 0.f, 1e-6f);  // clamped
    }
}
