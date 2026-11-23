// Unit tests for the G3 deferred renderer's pure parts (no GL context needed):
// effect capability mapping, G-buffer material-ID packing, render-scale helpers,
// TAA jitter sequence.

#include "test_framework.hpp"

#include <fuhglgraphics/render/deferred_renderer.hpp>
#include <fuhglgraphics/render/decals.hpp>
#include <fuhglgraphics/render/effects.hpp>
#include <fuhglgraphics/render/gbuffer.hpp>

using namespace fuhglgraphics::render;

void runRenderTests() {
    // --- effectName / kEffectCount ---
    CHECK(kEffectCount == 20);
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
    CHECK(std::string(effectName(Effect::LightProbes)) == "light probes");
    CHECK(std::string(effectName(Effect::Decals)) == "decals");
    CHECK(std::string(effectName(Effect::Dof)) == "DoF");
    CHECK(std::string(effectName(Effect::DebugHud)) == "debug HUD");
    CHECK(std::string(effectName(Effect::Water)) == "water");
    CHECK(std::string(effectName(Effect::GpuSkinning)) == "GPU skinning");
    CHECK(std::string(effectName(Effect::Ssao)) == "SSAO");
    CHECK(std::string(effectName(Effect::Ssr)) == "SSR");
    CHECK(std::string(effectName(Effect::Bloom)) == "bloom");
    CHECK(std::string(effectName(Effect::VolumetricFog)) == "volumetric fog");

    // --- effectFromName: inverse of effectName for every effect ---
    for (int e = 0; e < kEffectCount; ++e) {
        const Effect eff = static_cast<Effect>(e);
        Effect back = Effect::Taa;
        CHECK(effectFromName(effectName(eff), back));
        CHECK(back == eff);
    }
    {
        Effect dummy = Effect::Taa;
        CHECK(!effectFromName("no-such-effect", dummy));
    }

    // --- effectSupported: pure GL-version mapping (F39) ---
    for (int e = 0; e < kEffectCount; ++e) {
        const Effect eff = static_cast<Effect>(e);
        CHECK(effectSupported(eff, 4, 5));
        CHECK(!effectSupported(eff, 3, 0)); // below baseline
        CHECK(!effectSupported(eff, 2, 1));
        if (eff == Effect::GpuSkinning) {
            // G14: the SSBO palette path needs GL >= 4.3 (CPU fallback below)
            CHECK(effectSupported(eff, 4, 3));
            CHECK(effectSupported(eff, 4, 6));
            CHECK(!effectSupported(eff, 4, 2));
            CHECK(!effectSupported(eff, 3, 3));
        } else {
            CHECK(effectSupported(eff, 3, 3)); // baseline: everything else runs
        }
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

    // --- G8 decalProject: box containment + dominant-axis UV (pure) ---
    const fuhgl::math::Mat4 identity{};
    const fuhgl::math::Vec3 he{2.f, 0.5f, 3.f};
    fuhgl::math::Vec2 uv{0.f, 0.f};
    // ground point under a flat box: dominant axis = Y -> uv from (x, z)
    CHECK(decalProject(identity, he, {1.f, 0.f, -2.f}, {0.f, 1.f, 0.f}, uv));
    CHECK_NEAR(uv.x, (1.f / (2.f * 2.f)) + 0.5f, 1e-6f);  // x=1 of half 2
    CHECK_NEAR(uv.y, (-2.f / (2.f * 3.f)) + 0.5f, 1e-6f); // z=-2 of half 3
    // outside the box -> rejected
    CHECK(!decalProject(identity, he, {5.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, uv));
    CHECK(!decalProject(identity, he, {0.f, 2.f, 0.f}, {0.f, 1.f, 0.f}, uv));
    // vertical wall (normal +Z): dominant axis = Z -> uv from (x, y)
    CHECK(decalProject(identity, he, {0.5f, 0.25f, 1.f}, {0.f, 0.f, 1.f}, uv));
    CHECK_NEAR(uv.x, (0.5f / 4.f) + 0.5f, 1e-6f);
    CHECK_NEAR(uv.y, (0.25f / 1.f) + 0.5f, 1e-6f);
    // rotated decal: world = translate(10, 0, 0); local = world - (10,0,0)
    const fuhgl::math::Mat4 world = fuhgl::math::translate({10.f, 0.f, 0.f});
    CHECK(decalProject(world.inverse(), he, {10.5f, 0.f, 0.f}, {0.f, 1.f, 0.f}, uv));
    CHECK_NEAR(uv.x, (0.5f / 4.f) + 0.5f, 1e-6f);
    CHECK(!decalProject(world.inverse(), he, {13.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, uv)); // outside (local x=3 > 2)
}
