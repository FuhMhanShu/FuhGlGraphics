// Unit tests for the G13 water module (no GL context needed): the pure Gerstner wave
// evaluation (CPU twin of the water vertex shader), the reflection matrix, the water
// plane from a model matrix, WaterSurface defaults, and the G9 WaterComponent::fill
// (draw item + castShadow=false + FrameData::water).

#include "test_framework.hpp"

#include <fuhglgraphics/scene/scene.hpp>
#include <fuhglgraphics/water/water_surface.hpp>

#include <cmath>

using namespace fuhglgraphics;
using water::evalGerstner;
using fuhgl::math::Vec2;
using fuhgl::math::Vec3;
using fuhgl::math::Vec4;

namespace {

bool vecNear(const Vec3& a, const Vec3& b, float eps = 1e-5f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

} // namespace

void runWaterTests() {
    // --- evalGerstner: zero waves leave the plane untouched, normal stays up ---
    {
        Vec3 p, n;
        evalGerstner(nullptr, 0, 1.5f, {3.f, -2.f}, p, &n);
        CHECK(vecNear(p, {3.f, 0.f, -2.f}));
        CHECK(vecNear(n, {0.f, 1.f, 0.f}));
    }

    // --- evalGerstner: height is bounded by the amplitude sum, normal stays unit/up-ish ---
    {
        water::WaterSurface w;
        w.setDefaultWaves();
        CHECK(w.waves.size() == 4);
        CHECK(w.waves.size() <= water::WaterSurface::kMaxWaves);
        const float maxH = w.maxWaveHeight();
        CHECK(maxH > 0.f);
        float sumA = 0.f;
        for (const auto& g : w.waves) sumA += std::fabs(g.amplitude);
        CHECK_NEAR(maxH, sumA, 1e-6f);
        for (int i = 0; i < 50; ++i) {
            const Vec2 xz{std::sin(i * 1.7f) * 20.f, std::cos(i * 0.9f) * 20.f};
            Vec3 p, n;
            evalGerstner(w.waves.data(), static_cast<int>(w.waves.size()), 0.37f * i, xz, p, &n);
            CHECK(std::fabs(p.y) <= maxH + 1e-4f);
            CHECK_NEAR(fuhgl::math::length(n), 1.f, 1e-4f);
            CHECK(n.y > 0.f); // waves never flip the normal
            CHECK_NEAR(p.x, p.x, 0.f); // finite
        }
    }

    // --- evalGerstner: time advances the phase (surface moves), steepness=0 keeps xz ---
    {
        const water::GerstnerWave waves[1] = {{{1.f, 0.f}, 0.5f, 8.f, 2.f, 0.8f}};
        Vec3 p0, p1, n;
        evalGerstner(waves, 1, 0.f, {1.3f, 2.1f}, p0, &n);
        evalGerstner(waves, 1, 0.5f, {1.3f, 2.1f}, p1, &n);
        CHECK(std::fabs(p0.y - p1.y) > 1e-3f); // phase moved
        const water::GerstnerWave flat[1] = {{{1.f, 0.f}, 0.5f, 8.f, 2.f, 0.f}};
        Vec3 pf;
        evalGerstner(flat, 1, 0.f, {1.3f, 2.1f}, pf, &n);
        CHECK_NEAR(pf.x, 1.3f, 1e-5f); // Q = 0: no horizontal displacement
        CHECK_NEAR(pf.z, 2.1f, 1e-5f);
        CHECK(std::fabs(pf.y) <= 0.5f + 1e-5f);
    }

    // --- reflectionMatrix: reflects points about the plane dot(n,x)+d = 0 ---
    {
        // plane y = 0 (n = +Y, d = 0)
        const fuhgl::math::Mat4 r = water::reflectionMatrix({0.f, 1.f, 0.f}, 0.f);
        const Vec4 p = r * Vec4{1.f, 5.f, 2.f, 1.f};
        CHECK_NEAR(p.x, 1.f, 1e-5f);
        CHECK_NEAR(p.y, -5.f, 1e-5f);
        CHECK_NEAR(p.z, 2.f, 1e-5f);
        // plane y = 2 (n = +Y, d = -2): y' = 4 - y
        const fuhgl::math::Mat4 r2 = water::reflectionMatrix({0.f, 1.f, 0.f}, -2.f);
        const Vec4 q = r2 * Vec4{0.f, 6.f, 0.f, 1.f};
        CHECK_NEAR(q.y, -2.f, 1e-5f);
        // reflection twice = identity
        const fuhgl::math::Mat4 id = r * r;
        const Vec4 p2 = id * Vec4{3.f, -7.f, 1.f, 1.f};
        CHECK_NEAR(p2.x, 3.f, 1e-4f);
        CHECK_NEAR(p2.y, -7.f, 1e-4f);
        CHECK_NEAR(p2.z, 1.f, 1e-4f);
    }

    // --- waterPlaneFromModel: identity model -> plane y = 0; translated -> y = level ---
    {
        Vec3 n;
        float d = 1.f;
        water::waterPlaneFromModel(fuhgl::math::Mat4{}, n, d);
        CHECK(vecNear(n, {0.f, 1.f, 0.f}));
        CHECK_NEAR(d, 0.f, 1e-5f);
        water::waterPlaneFromModel(fuhgl::math::translate({5.f, 1.5f, -3.f}), n, d);
        CHECK(vecNear(n, {0.f, 1.f, 0.f}));
        CHECK_NEAR(d, -1.5f, 1e-5f); // dot(n, x) + d = 0 -> y - 1.5 = 0
    }

    // --- WaterComponent::fill: draw item at the node's world matrix, no shadow, frame.water ---
    {
        scene::Scene scene;
        scene::Node& waterNode = scene.root().createChild("water");
        waterNode.translation = {0.f, 1.5f, 0.f};
        scene::WaterComponent& wc = waterNode.attach<scene::WaterComponent>();
        wc.surface.setDefaultWaves();
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, 64, 64, false);
        CHECK(frame.water == &wc.surface);
        CHECK(frame.view.items.size() == 1);
        CHECK(frame.castShadow.size() == 1 && frame.castShadow[0] == false);
        CHECK(frame.materialIds.size() == 1);
        CHECK(frame.view.items[0].mesh == &wc.surface.mesh());
        const Vec4 origin = frame.view.items[0].model * Vec4{0.f, 0.f, 0.f, 1.f};
        CHECK_NEAR(origin.y, 1.5f, 1e-5f);
        // the node AABB covers the grid padded by the wave height
        CHECK(waterNode.hasAabb());
        Vec3 mn, mx;
        waterNode.worldAabb(mn, mx);
        CHECK(mx.y - mn.y > 2.f * wc.surface.maxWaveHeight());
    }
}
