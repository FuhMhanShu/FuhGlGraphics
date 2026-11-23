// Unit tests for the parallax occlusion mapping CPU twin (no GL context needed):
// parallaxOffsetUV mirrors the POM loop of kGBufferFrag — straight-on view gives no
// offset, grazing view gives a larger offset, flat height field gives no offset,
// deep height field marches through all layers, deterministic.

#include "test_framework.hpp"

#include <fuhglgraphics/render/parallax.hpp>

#include <cmath>

using namespace fuhglgraphics::render;

void runParallaxTests() {
    const auto flat = [](fuhgl::math::Vec2) { return 0.f; };          // no relief
    const auto deep = [](fuhgl::math::Vec2) { return 1.f; };          // fully deep
    const auto ramp = [](fuhgl::math::Vec2 uv) { return uv.x; };      // height = u

    // --- straight-on view (viewTS = +Z): no parallax offset ---
    {
        const auto off = parallaxOffset({0.f, 0.f, 1.f}, 0.05f, 12, deep, {0.5f, 0.5f});
        CHECK_NEAR(off.x, 0.f, 1e-6f);
        CHECK_NEAR(off.y, 0.f, 1e-6f);
    }
    // --- flat height field (h = 0): the first check layerDepth(0) >= h(0) breaks -> no offset ---
    {
        const auto off = parallaxOffset({0.7f, 0.0f, 0.7f}, 0.05f, 12, flat, {0.5f, 0.5f});
        CHECK_NEAR(off.x, 0.f, 1e-6f);
        CHECK_NEAR(off.y, 0.f, 1e-6f);
    }
    // --- deep field: marches through all layers -> offset = -viewTS.xy/viewTS.z * scale ---
    {
        const fuhgl::math::Vec3 v{0.6f, -0.3f, 0.72f};
        const auto off = parallaxOffset(v, 0.05f, 12, deep, {0.5f, 0.5f});
        const float z = v.z > 0.05f ? v.z : 0.05f;
        CHECK_NEAR(off.x, -(v.x / z) * 0.05f, 1e-5f);
        CHECK_NEAR(off.y, -(v.y / z) * 0.05f, 1e-5f);
    }
    // --- grazing view (clamped z) -> bigger offset than steep view, same direction ---
    {
        const auto steep = parallaxOffset({0.1f, 0.f, 0.995f}, 0.05f, 12, deep, {0.5f, 0.5f});
        const auto grazing = parallaxOffset({0.995f, 0.f, 0.1f}, 0.05f, 12, deep, {0.5f, 0.5f});
        CHECK(std::fabs(grazing.x) > std::fabs(steep.x));
        CHECK(grazing.x < 0.f && steep.x < 0.f); // offset against the view x
    }
    // --- ramp field: offset stops at the layer where the sampled height drops below layerDepth ---
    {
        // height(uv) = uv.x; starting at uv.x = 0.9, viewTS = (1,0,1) -> p = (scale, 0)
        // layer i samples uv.x = 0.9 - scale*i/layers; stop when layerDepth >= h
        const auto out = parallaxOffsetUV({1.f, 0.f, 1.f}, 0.2f, 10, ramp, {0.9f, 0.5f});
        CHECK(out.x <= 0.9f);            // never moves forward
        CHECK(out.x > 0.9f - 0.2f - 1e-6f); // but marches at least one full p
        // determinism
        const auto out2 = parallaxOffsetUV({1.f, 0.f, 1.f}, 0.2f, 10, ramp, {0.9f, 0.5f});
        CHECK(out.x == out2.x && out.y == out2.y);
    }
    // --- layers clamped to 64, layers <= 0 treated as 1 ---
    {
        const auto a = parallaxOffset({0.5f, 0.f, 0.87f}, 0.05f, 1000, deep, {0.5f, 0.5f});
        const auto b = parallaxOffset({0.5f, 0.f, 0.87f}, 0.05f, 64, deep, {0.5f, 0.5f});
        CHECK_NEAR(a.x, b.x, 1e-6f);
        CHECK_NEAR(a.y, b.y, 1e-6f);
        const auto c = parallaxOffset({0.5f, 0.f, 0.87f}, 0.05f, 0, deep, {0.5f, 0.5f});
        CHECK(std::fabs(c.x) > 0.f); // still one step
    }
}
