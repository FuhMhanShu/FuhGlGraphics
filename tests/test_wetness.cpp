// Unit tests for the G22 wetness/rain pure helpers (no GL context needed): the deterministic
// puddle-noise mask, the combined wetness factor, and the roughness/albedo modulation —
// mirrors of the GLSL in kLightFragWetness.

#include "test_framework.hpp"

#include <fuhglgraphics/render/effects.hpp>
#include <fuhglgraphics/render/wetness.hpp>

#include <cmath>

using namespace fuhglgraphics::render;

void runWetnessTests() {
    // --- effect registry: G22 present ---
    {
        CHECK(kEffectCount == 28); // + Weathering (G31)
        Effect e;
        CHECK(effectFromName("wetness", e) && e == Effect::Wetness);
        CHECK(effectSupported(Effect::Wetness, 3, 3));
    }

    // --- hash / noise / puddle mask: deterministic, in range ---
    {
        const fuhgl::math::Vec2 p{3.7f, -2.1f};
        CHECK(wetnessHash(p, 42u) == wetnessHash(p, 42u));
        for (int i = 0; i < 64; ++i) {
            const fuhgl::math::Vec2 q{0.37f * i, -1.13f * i};
            const float h = wetnessHash(q, 7u);
            CHECK(h >= 0.f && h <= 1.f);
            const float n = wetnessNoise(q, 3.5f, 2024u);
            CHECK(n >= 0.f && n <= 1.f);
            const float m = wetnessPuddleMask(q, 3.5f, 0.7f);
            CHECK(m >= 0.f && m <= 1.f);
        }
        CHECK(wetnessNoise({1.f, 2.f}, 3.5f, 9u) == wetnessNoise({1.f, 2.f}, 3.5f, 9u));
        // puddleStrength 0 -> flat film (mask 1 everywhere); 1 -> only low-noise spots
        CHECK_NEAR(wetnessPuddleMask({5.f, 5.f}, 3.5f, 0.f), 1.f, 1e-6f);
        float minM = 1.f, maxM = 0.f;
        for (int i = 0; i < 128; ++i) {
            const float m = wetnessPuddleMask({0.31f * i, 0.17f * i}, 3.5f, 1.f);
            minM = std::min(minM, m);
            maxM = std::max(maxM, m);
        }
        CHECK(minM >= 0.f && maxM <= 1.f);
        CHECK(minM < 0.2f && maxM > 0.8f); // the mask actually varies
    }

    // --- wetness factor: up-facing only, bounded ---
    {
        // side-facing surface (ndotUp = 0) stays dry
        CHECK_NEAR(wetnessFactor(0.9f, 0.7f, 0.f, 1.f), 0.f, 1e-6f);
        // fully up-facing, flat film -> the amount
        CHECK_NEAR(wetnessFactor(0.9f, 0.f, 1.f, 1.f), 0.9f, 1e-6f);
        // puddle mask scales it down
        CHECK(wetnessFactor(0.9f, 1.f, 1.f, 0.2f) < wetnessFactor(0.9f, 0.f, 1.f, 1.f));
        // clamping
        CHECK_NEAR(wetnessFactor(5.f, 0.f, 1.f, 1.f), 1.f, 1e-6f);
        CHECK_NEAR(wetnessFactor(-1.f, 0.f, 1.f, 1.f), 0.f, 1e-6f);
    }

    // --- roughness / albedo modulation ---
    {
        // roughness decreases monotonically toward 0.06
        float prev = 1.f;
        for (int i = 0; i <= 10; ++i) {
            const float w = i / 10.f;
            const float r = wetnessRoughness(0.9f, w);
            CHECK(r <= prev + 1e-6f);
            CHECK(r >= 0.06f - 1e-6f);
            prev = r;
        }
        CHECK_NEAR(wetnessRoughness(0.9f, 1.f), 0.06f, 1e-6f);
        CHECK_NEAR(wetnessRoughness(0.9f, 0.f), 0.9f, 1e-6f);
        // albedo scale in [0.65, 1]
        CHECK_NEAR(wetnessAlbedoScale(1.f), 0.65f, 1e-6f);
        CHECK_NEAR(wetnessAlbedoScale(0.f), 1.f, 1e-6f);
        CHECK(wetnessAlbedoScale(0.5f) > 0.65f && wetnessAlbedoScale(0.5f) < 1.f);
    }
}
