// Unit tests for the G21 volumetric fog/clouds pure helpers (no GL context needed):
// deterministic 3D hash/noise/fBm ranges, the height-falloff density, the Henyey-Greenstein
// phase function, and the CPU reference raymarcher (transmittance + in-scatter).

#include "test_framework.hpp"

#include <fuhglgraphics/render/effects.hpp>
#include <fuhglgraphics/render/volumetric.hpp>

#include <fuhgl/math/func.hpp>

#include <cmath>

using namespace fuhglgraphics::render;

void runVolumetricTests() {
    // --- effect registry: G21 present ---
    {
        CHECK(kEffectCount == 27);
        Effect e;
        CHECK(effectFromName("volumetric fog", e) && e == Effect::VolumetricFog);
        CHECK(effectSupported(Effect::VolumetricFog, 3, 3));
        CHECK(effectSupported(Effect::VolumetricFog, 4, 6));
    }

    // --- hash / noise / fbm: deterministic, in range ---
    {
        const fuhgl::math::Vec3 p{3.7f, -2.1f, 8.9f};
        CHECK(volumetricHash(p, 42u) == volumetricHash(p, 42u)); // deterministic
        bool seedDiffers = false;
        for (unsigned s = 0; s < 16u; ++s) seedDiffers |= (volumetricHash(p, s) != volumetricHash(p, 42u));
        CHECK(seedDiffers); // seed actually changes the hash
        for (int i = 0; i < 64; ++i) {
            const fuhgl::math::Vec3 q{0.37f * i, -1.13f * i, 2.71f * i};
            const float h = volumetricHash(q, 7u);
            CHECK(h >= 0.f && h <= 1.f);
            const float n = volumetricNoise(q, 7u);
            CHECK(n >= 0.f && n <= 1.f);
            const float f = volumetricFbm(q, 4, 7u);
            CHECK(f >= 0.f && f <= 1.f);
        }
        // determinism of fbm
        const fuhgl::math::Vec3 q{1.5f, 2.5f, 3.5f};
        CHECK(volumetricFbm(q, 4, 9u) == volumetricFbm(q, 4, 9u));
        // noise is continuous-ish: close samples are close in value
        CHECK(std::fabs(volumetricNoise({1.f, 1.f, 1.f}, 3u) - volumetricNoise({1.001f, 1.f, 1.f}, 3u)) < 0.05f);
    }

    // --- density: height falloff + cloudiness blending, always >= 0 ---
    {
        const float d0 = volumetricDensity({0.f, 0.f, 0.f}, 0.02f, 0.f, 8.f, 6.f);
        const float d10 = volumetricDensity({0.f, 10.f, 0.f}, 0.02f, 0.f, 8.f, 6.f);
        CHECK_NEAR(d0, 0.02f, 1e-6f);                       // no falloff at y=0, cloudiness 0
        CHECK(d10 < d0);                                     // exponential falloff with height
        CHECK_NEAR(d10, 0.02f * std::exp(-10.f / 8.f), 1e-5f);
        // negative heights are clamped to y=0 (no growth below ground)
        CHECK_NEAR(volumetricDensity({0.f, -5.f, 0.f}, 0.02f, 0.f, 8.f, 6.f), 0.02f, 1e-6f);
        // cloudiness in [0,1]: density stays within [0, 2*base] * falloff
        for (int i = 0; i < 32; ++i) {
            const fuhgl::math::Vec3 p{5.f * i, 2.f * i, -3.f * i};
            const float d = volumetricDensity(p, 0.02f, 1.f, 8.f, 6.f, 1337u);
            CHECK(d >= 0.f && d <= 0.04f * std::exp(-std::max(p.y, 0.f) / 8.f) + 1e-6f);
        }
    }

    // --- Henyey-Greenstein: isotropic at g=0, forward peak, normalized-ish ---
    {
        CHECK_NEAR(henyeyGreenstein(0.5f, 0.f), 1.f / (4.f * 3.14159265f), 1e-5f);
        const float fwd = henyeyGreenstein(1.f, 0.6f);
        const float back = henyeyGreenstein(-1.f, 0.6f);
        CHECK(fwd > back);            // forward scattering
        CHECK(fwd > henyeyGreenstein(1.f, 0.f));
        CHECK(henyeyGreenstein(0.f, 0.9f) > 0.f);
    }

    // --- CPU raymarcher: transmittance in (0,1], decreasing with density; in-scatter >= 0 ---
    {
        VolumetricParams prm;
        prm.baseDensity = 0.05f;
        prm.cloudiness = 0.f;
        prm.steps = 32;
        prm.maxDist = 100.f;
        const fuhgl::math::Vec3 origin{0.f, 2.f, 0.f}, dir{0.f, 0.f, -1.f};
        const fuhgl::math::Vec3 sunDir{0.f, 1.f, 0.f}, sunColor{3.f, 3.f, 3.f}, ambient{0.1f, 0.1f, 0.1f};
        const auto r = volumetricMarch(origin, dir, 50.f, prm, sunDir, sunColor, ambient);
        CHECK(r.transmittance > 0.f && r.transmittance < 1.f);
        CHECK(r.inscatter.x > 0.f && r.inscatter.y > 0.f && r.inscatter.z > 0.f);
        // denser volume -> lower transmittance
        VolumetricParams dense = prm;
        dense.baseDensity = 0.2f;
        const auto r2 = volumetricMarch(origin, dir, 50.f, dense, sunDir, sunColor, ambient);
        CHECK(r2.transmittance < r.transmittance);
        // zero density -> identity (no fog)
        VolumetricParams off = prm;
        off.baseDensity = 0.f;
        const auto r3 = volumetricMarch(origin, dir, 50.f, off, sunDir, sunColor, ambient);
        CHECK_NEAR(r3.transmittance, 1.f, 1e-6f);
        CHECK_NEAR(r3.inscatter.x, 0.f, 1e-6f);
        // tMax = 0 -> identity
        const auto r4 = volumetricMarch(origin, dir, 0.f, prm, sunDir, sunColor, ambient);
        CHECK_NEAR(r4.transmittance, 1.f, 1e-6f);
        // uniform fog analytic check: T = exp(-density * tMax) when cloudiness = 0, y constant
        VolumetricParams uni = prm;
        uni.baseDensity = 0.1f;
        uni.heightFalloff = 1e6f; // ~no falloff over 50 units
        const auto r5 = volumetricMarch({0.f, 0.f, 0.f}, dir, 50.f, uni, sunDir, sunColor, ambient);
        CHECK_NEAR(r5.transmittance, std::exp(-0.1f * 50.f), 0.02f); // step integration tolerance
    }
}
