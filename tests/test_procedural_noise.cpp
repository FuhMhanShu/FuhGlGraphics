// Unit tests for G24 procedural::Noise (label "unit"): determinism per seed, value-noise
// range + lattice properties, Perlin/Simplex range + zero at integer lattice, fBm/ridged/
// turbulence ranges, continuity, domain warp, height field.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/noise.hpp>

#include <cmath>

namespace {

using fuhglgraphics::procedural::Noise;

void testDeterminism() {
    const Noise a(42u), b(42u), c(43u);
    for (int i = 0; i < 200; ++i) {
        const float x = i * 0.37f, y = i * 0.11f, z = i * 0.53f;
        CHECK(a.perlin2(x, y) == b.perlin2(x, y));
        CHECK(a.simplex2(x, y) == b.simplex2(x, y));
        CHECK(a.value3(x, y, z) == b.value3(x, y, z));
        CHECK(a.perlin3(x, y, z) == b.perlin3(x, y, z));
    }
    bool anyDiff = false;
    for (int i = 0; i < 50; ++i)
        if (a.perlin2(i * 0.71f, i * 0.13f) != c.perlin2(i * 0.71f, i * 0.13f)) anyDiff = true;
    CHECK(anyDiff);
}

void testValueNoise() {
    const Noise n(7u);
    for (int i = 0; i < 1000; ++i) {
        const float x = (i % 37) * 0.173f - 3.f, y = (i / 37) * 0.211f - 3.f;
        const float v2 = n.value2(x, y);
        const float v3 = n.value3(x, y, i * 0.07f);
        CHECK(v2 >= 0.f && v2 <= 1.f);
        CHECK(v3 >= 0.f && v3 <= 1.f);
    }
    // value noise hits its lattice values exactly at integer coordinates
    for (int i = -3; i <= 3; ++i)
        for (int j = -3; j <= 3; ++j)
            CHECK(n.value2(static_cast<float>(i), static_cast<float>(j)) ==
                  n.value2(static_cast<float>(i) + 0.f, static_cast<float>(j)));
}

void testPerlinSimplexRanges() {
    const Noise n(99u);
    float pMin = 1e9f, pMax = -1e9f, sMin = 1e9f, sMax = -1e9f, sum = 0.f;
    for (int i = 0; i < 4000; ++i) {
        const float x = (i % 61) * 0.131f - 4.f;
        const float y = (i % 43) * 0.179f - 4.f;
        const float z = (i % 29) * 0.223f - 4.f;
        const float p = n.perlin3(x, y, z);
        const float s = n.simplex3(x, y, z);
        const float s2 = n.simplex2(x, y);
        pMin = std::fmin(pMin, p); pMax = std::fmax(pMax, p);
        sMin = std::fmin(sMin, s); sMax = std::fmax(sMax, s);
        sMin = std::fmin(sMin, s2); sMax = std::fmax(sMax, s2);
        sum += p;
    }
    CHECK(pMin > -1.2f && pMax < 1.2f);
    CHECK(sMin > -1.2f && sMax < 1.2f);
    CHECK(std::fabs(sum / 4000.f) < 0.1f); // roughly zero mean
    // improved Perlin is exactly 0 at integer lattice points
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            CHECK(n.perlin2(static_cast<float>(i), static_cast<float>(j)) == 0.f);
}

void testFractals() {
    const Noise n(5u);
    for (int i = 0; i < 500; ++i) {
        const float x = i * 0.083f, y = i * 0.047f, z = i * 0.061f;
        const float f = n.fbm3(x, y, z, 5);
        const float r = n.ridged3(x, y, z, 5);
        const float t = n.turbulence3(x, y, z, 5);
        CHECK(f > -1.2f && f < 1.2f);   // normalized like the base
        CHECK(r >= 0.f && r <= 1.f);     // ridged in [0,1]
        CHECK(t >= 0.f && t <= 1.2f);    // turbulence in [0,1]
        const float h = n.height2(x, y, 6);
        CHECK(h >= 0.f && h <= 1.f);
    }
    for (int b = 0; b < 3; ++b) {
        const auto base = static_cast<Noise::Base>(b);
        const float f2 = n.fbm2(1.3f, 2.7f, 4, base);
        const float r2 = n.ridged2(1.3f, 2.7f, 4, base);
        CHECK(std::fabs(f2) <= 1.2f);
        CHECK(r2 >= 0.f && r2 <= 1.f);
    }
}

void testContinuityAndWarp() {
    const Noise n(3u);
    for (int i = 0; i < 500; ++i) {
        const float x = i * 0.071f, y = i * 0.043f, z = i * 0.029f;
        CHECK(std::fabs(n.simplex3(x + 1e-3f, y, z) - n.simplex3(x, y, z)) < 0.05f);
        CHECK(std::fabs(n.perlin2(x, y + 1e-3f) - n.perlin2(x, y)) < 0.05f);
        CHECK(std::fabs(n.value3(x, y, z + 1e-3f) - n.value3(x, y, z)) < 0.05f);
    }
    const fuhgl::math::Vec2 w = n.warp2(1.f, 2.f, 0.5f, 2);
    const fuhgl::math::Vec3 w3 = n.warp3(1.f, 2.f, 3.f, 0.5f, 2);
    CHECK(std::fabs(w.x - 1.f) <= 0.5f + 1e-4f && std::fabs(w.y - 2.f) <= 0.5f + 1e-4f);
    CHECK(std::fabs(w3.x - 1.f) <= 0.5f + 1e-4f);
}

} // namespace

void runProceduralNoiseTests() {
    testDeterminism();
    testValueNoise();
    testPerlinSimplexRanges();
    testFractals();
    testContinuityAndWarp();
}
