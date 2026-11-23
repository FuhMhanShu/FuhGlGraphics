// Unit tests for the G23 tonemapping operators (no GL context needed): the enum/name
// round-trip and the pure CPU twins — ACES, Reinhard, Uncharted2, linear — ranges,
// monotonicity, black point, and the renderer wiring type.

#include "test_framework.hpp"

#include <fuhglgraphics/render/tonemap.hpp>

#include <cmath>

using namespace fuhglgraphics::render;

void runTonemapTests() {
    // --- enum + names ---
    {
        CHECK(kTonemapOperatorCount == 4);
        CHECK(std::string(tonemapOperatorName(TonemapOperator::Aces)) == "aces");
        CHECK(std::string(tonemapOperatorName(TonemapOperator::Reinhard)) == "reinhard");
        CHECK(std::string(tonemapOperatorName(TonemapOperator::Uncharted2)) == "uncharted2");
        CHECK(std::string(tonemapOperatorName(TonemapOperator::Linear)) == "linear");
        for (int i = 0; i < kTonemapOperatorCount; ++i) {
            TonemapOperator back;
            CHECK(tonemapOperatorFromName(tonemapOperatorName(static_cast<TonemapOperator>(i)), back));
            CHECK(back == static_cast<TonemapOperator>(i));
        }
        TonemapOperator dummy;
        CHECK(!tonemapOperatorFromName("bogus", dummy));
    }

    // --- per-channel behavior ---
    {
        for (int op = 0; op < kTonemapOperatorCount; ++op) {
            const auto o = static_cast<TonemapOperator>(op);
            CHECK_NEAR(tonemapApply(o, 0.f), 0.f, 1e-6f);          // black stays black
            CHECK(tonemapApply(o, -1.f) == 0.f);                    // negatives clamped
            float prev = -1.f;
            for (int i = 0; i <= 100; ++i) {                        // monotone, in [0,1]
                const float x = i * 0.2f;                           // 0..20 HDR
                const float y = tonemapApply(o, x);
                CHECK(y >= 0.f && y <= 1.f + 1e-6f);
                CHECK(y >= prev - 1e-6f);
                prev = y;
            }
            CHECK(tonemapApply(o, 1000.f) <= 1.f + 1e-6f);          // huge HDR clamps
        }
        // Reinhard: x/(1+x) — at x=1 -> 0.5
        CHECK_NEAR(tonemapReinhard(1.f), 0.5f, 1e-6f);
        CHECK(tonemapReinhard(10.f) < 1.f);
        // ACES: mid-gray ~0.46-ish, brighter than Reinhard at high x (more contrast)
        CHECK(tonemapAces(1.f) > tonemapReinhard(1.f) - 0.1f);
        CHECK(tonemapAces(4.f) > tonemapReinhard(4.f));
        // Uncharted2 (white point 11.2) has a darker toe/mids than the ACES fit, still < 1 at huge x
        CHECK(tonemapUncharted2(0.18f) < tonemapAces(0.18f));
        CHECK(tonemapUncharted2(0.18f) >= 0.f);
        // linear is identity up to 1
        CHECK_NEAR(tonemapLinear(0.5f), 0.5f, 1e-6f);
        CHECK_NEAR(tonemapLinear(2.f), 1.f, 1e-6f);
    }

    // --- RGB overload ---
    {
        const auto c = tonemapApply(TonemapOperator::Reinhard, {1.f, 2.f, 4.f});
        CHECK_NEAR(c.x, 0.5f, 1e-6f);
        CHECK_NEAR(c.y, 2.f / 3.f, 1e-6f);
        CHECK_NEAR(c.z, 0.8f, 1e-6f);
    }
}
