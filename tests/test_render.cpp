// Unit tests for the G3 deferred renderer's pure parts (no GL context needed):
// effect capability mapping, G-buffer material-ID packing, render-scale helpers,
// TAA jitter sequence.

#include "test_framework.hpp"

#include <fuhglgraphics/render/deferred_renderer.hpp>
#include <fuhglgraphics/render/effects.hpp>
#include <fuhglgraphics/render/gbuffer.hpp>

using namespace fuhglgraphics::render;

void runRenderTests() {
    // --- effectName / kEffectCount ---
    CHECK(kEffectCount == 10);
    CHECK(std::string(effectName(Effect::Taa)) == "TAA");
    CHECK(std::string(effectName(Effect::Upscale)) == "upscaling");
    CHECK(std::string(effectName(Effect::Tonemap)) == "tonemap");
    CHECK(std::string(effectName(Effect::Msaa)) == "MSAA");
    CHECK(std::string(effectName(Effect::Shadows)) == "shadows");
    CHECK(std::string(effectName(Effect::CsmShadows)) == "CSM shadows");
    CHECK(std::string(effectName(Effect::Ibl)) == "IBL");
    CHECK(std::string(effectName(Effect::PointShadows)) == "point shadows");
    CHECK(std::string(effectName(Effect::ContactShadows)) == "contact shadows");
    CHECK(std::string(effectName(Effect::Particles)) == "particles");

    // --- effectSupported: pure GL-version mapping (F39) ---
    for (int e = 0; e < kEffectCount; ++e) {
        const Effect eff = static_cast<Effect>(e);
        CHECK(effectSupported(eff, 3, 3));  // baseline: everything runs
        CHECK(effectSupported(eff, 4, 5));
        CHECK(!effectSupported(eff, 3, 0)); // below baseline
        CHECK(!effectSupported(eff, 2, 1));
    }

    // --- material ID pack/unpack roundtrip ---
    for (std::uint32_t id : {0u, 1u, 7u, 255u, 256u, 0xDEADBEEFu, 0xFFFFFFFFu}) {
        unsigned char rgba[4];
        packMaterialId(id, rgba);
        CHECK(unpackMaterialId(rgba) == id);
    }
    unsigned char rgba[4];
    packMaterialId(0x01020304u, rgba);
    CHECK(rgba[0] == 0x04 && rgba[1] == 0x03 && rgba[2] == 0x02 && rgba[3] == 0x01);

    // --- GBufferAttachment layout ---
    CHECK(static_cast<int>(GBufferAttachment::kColorCount) == 6);
    CHECK(static_cast<int>(GBufferAttachment::Albedo) == 0);
    CHECK(static_cast<int>(GBufferAttachment::MaterialId) == 5);

    // --- render scale clamping + internal size ---
    CHECK_NEAR(clampedRenderScale(0.1f), 0.25, 1e-6);
    CHECK_NEAR(clampedRenderScale(2.0f), 1.0, 1e-6);
    CHECK_NEAR(clampedRenderScale(0.6f), 0.6, 1e-6);
    int w = 0, h = 0;
    internalSize(1280, 720, 1.0f, w, h);
    CHECK(w == 1280 && h == 720);
    internalSize(1280, 720, 0.5f, w, h);
    CHECK(w == 640 && h == 360);
    internalSize(1280, 720, 0.0f, w, h); // clamped to 0.25
    CHECK(w == 320 && h == 180);
    internalSize(1, 1, 0.25f, w, h); // never below 1x1
    CHECK(w == 1 && h == 1);

    // --- TAA jitter: deterministic, bounded by one pixel in NDC ---
    const fuhgl::math::Vec2 j0 = taaJitterNdc(0, 1280, 720);
    const fuhgl::math::Vec2 j0b = taaJitterNdc(0, 1280, 720);
    CHECK_NEAR(j0.x, j0b.x, 1e-7);
    CHECK_NEAR(j0.y, j0b.y, 1e-7);
    for (unsigned i = 0; i < 64; ++i) {
        const fuhgl::math::Vec2 j = taaJitterNdc(i, 1280, 720);
        CHECK(std::fabs(j.x) <= 2.0 / 1280.0 + 1e-6);
        CHECK(std::fabs(j.y) <= 2.0 / 720.0 + 1e-6);
        CHECK(j.x > -1.f && j.x < 1.f && j.y > -1.f && j.y < 1.f);
    }
    // consecutive frames differ (subpixel jitter actually moves)
    const fuhgl::math::Vec2 j1 = taaJitterNdc(1, 1280, 720);
    CHECK(std::fabs(j1.x - j0.x) > 1e-9 || std::fabs(j1.y - j0.y) > 1e-9);
}
