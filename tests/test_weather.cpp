// Unit tests for G38 weather::WeatherSystem (label "unit"): pure CPU state machine —
// presets, time-based smoothstep transitions, wetness lag, time-of-day sun path,
// seeded lightning schedule, applyTo() mappings. No GL context required.

#include "test_framework.hpp"

#include <fuhglgraphics/weather/weather.hpp>

#include <fuhgl/render/scene.hpp>
#include <fuhglgraphics/scene/effects.hpp>

#include <cmath>

namespace {

using fuhglgraphics::weather::WeatherParams;
using fuhglgraphics::weather::WeatherSystem;
using fuhglgraphics::weather::WeatherType;
using fuhglgraphics::weather::presetFor;
using fuhglgraphics::weather::weatherTypeFromName;
using fuhglgraphics::weather::weatherTypeName;
using fuhgl::math::Vec3;

bool inRange01(float v) { return v >= 0.f && v <= 1.f; }

void testPresets() {
    for (int i = 0; i < static_cast<int>(WeatherType::Count); ++i) {
        const WeatherType t = static_cast<WeatherType>(i);
        const WeatherParams p = presetFor(t);
        CHECK(inRange01(p.cloudCoverage));
        CHECK(inRange01(p.cloudDensity));
        CHECK(inRange01(p.rainIntensity));
        CHECK(inRange01(p.stormIntensity));
        CHECK(inRange01(p.sunVisibility));
        CHECK(p.fogDensity >= 0.f);
        CHECK(p.windSpeed >= 0.f);
        // storm is the harshest state, clear the mildest
        CHECK(presetFor(WeatherType::Thunderstorm).rainIntensity > presetFor(WeatherType::Clear).rainIntensity);
        CHECK(presetFor(WeatherType::Thunderstorm).cloudCoverage >= presetFor(WeatherType::Clear).cloudCoverage);
        CHECK(presetFor(WeatherType::Clear).sunVisibility >= presetFor(WeatherType::Overcast).sunVisibility);
        // name round-trip
        WeatherType back;
        CHECK(weatherTypeFromName(weatherTypeName(t), back));
        CHECK(back == t);
    }
    WeatherType dummy;
    CHECK(!weatherTypeFromName("hurricane", dummy));
}

void testTransition() {
    WeatherSystem w(7u);
    w.setParams(presetFor(WeatherType::Clear));
    w.setTimeOfDay(12.f);
    w.setTarget(WeatherType::Thunderstorm, 10.f);
    CHECK(w.isTransitioning());
    CHECK(w.targetType() == WeatherType::Thunderstorm);
    CHECK_NEAR(w.transitionProgress(), 0.f, 1e-6f);
    // mid-transition: params strictly between clear and storm, smoothstep (slow start)
    w.update(2.5f);
    CHECK(w.isTransitioning());
    const WeatherParams& mid = w.params();
    CHECK(mid.rainIntensity > 0.f && mid.rainIntensity < 1.f);
    CHECK(mid.cloudCoverage > presetFor(WeatherType::Clear).cloudCoverage);
    CHECK(mid.cloudCoverage < 1.f);
    // after the full duration the params equal the target preset exactly
    for (int i = 0; i < 20; ++i) w.update(1.f);
    CHECK(!w.isTransitioning());
    CHECK_NEAR(w.params().rainIntensity, 1.f, 1e-5f);
    CHECK_NEAR(w.params().cloudCoverage, 1.f, 1e-5f);
    CHECK_NEAR(w.params().stormIntensity, 1.f, 1e-5f);
    // smoothstep is monotonic in progress: progress after 5 s > progress after 2.5 s
    WeatherSystem a(7u), b(7u);
    a.setParams(presetFor(WeatherType::Clear));
    b.setParams(presetFor(WeatherType::Clear));
    a.setTarget(WeatherType::Rain, 10.f);
    b.setTarget(WeatherType::Rain, 10.f);
    a.update(2.5f);
    b.update(5.f);
    CHECK(b.transitionProgress() > a.transitionProgress());
    CHECK(b.params().rainIntensity > a.params().rainIntensity);
    // instant transition (duration 0)
    WeatherSystem c(7u);
    c.setTarget(WeatherType::Rain, 0.f);
    CHECK(!c.isTransitioning());
    CHECK_NEAR(c.params().rainIntensity, presetFor(WeatherType::Rain).rainIntensity, 1e-6f);
}

void testWetnessLag() {
    WeatherSystem w(3u);
    w.setParams(presetFor(WeatherType::Clear));
    w.setTarget(WeatherType::Rain, 0.f); // instant rain
    // wetness rises quickly toward rainIntensity
    for (int i = 0; i < 30; ++i) w.update(0.5f); // 15 s
    CHECK(w.params().wetness > 0.5f);
    // rain stops: wetness dries slowly but surely
    w.setTarget(WeatherType::Clear, 0.f);
    const float wetAfterRain = w.params().wetness;
    w.update(1.f);
    CHECK(w.params().wetness < wetAfterRain);      // drying started
    CHECK(w.params().wetness > 0.3f);              // ...but still visibly wet after 1 s
    for (int i = 0; i < 200; ++i) w.update(1.f);   // 200 s more
    CHECK(w.params().wetness < 0.05f);             // eventually dry
}

void testSunPath() {
    WeatherSystem w(11u);
    w.setParams(presetFor(WeatherType::Clear));
    // noon: sun high in the south (+Z at latitude 45), full intensity, white-ish
    w.setTimeOfDay(12.f);
    Vec3 sun = w.sunDirection();
    CHECK(std::fabs(fuhgl::math::length(sun) - 1.f) < 1e-4f);
    CHECK(sun.y > 0.6f); // elevation = 45 deg at lat 45
    CHECK(w.sunIntensity() > 2.f);
    CHECK(w.sunColor().x > w.sunColor().z);
    // midnight: sun below the horizon, no light, dark ambient
    w.setTimeOfDay(0.f);
    CHECK(w.sunDirection().y < -0.6f);
    CHECK(w.sunIntensity() == 0.f);
    // sunrise ~6h: low elevation, warm color (red > blue), dimmer than noon
    w.setTimeOfDay(6.f);
    CHECK(w.sunDirection().y > -0.1f && w.sunDirection().y < 0.3f);
    CHECK(w.sunColor().x > w.sunColor().z);
    CHECK(w.sunIntensity() < 2.f);
    // time advances with game time: 600 s day length -> 1 s real = 0.04 h
    w.setTimeOfDay(10.f);
    w.setDayLengthSeconds(600.f);
    w.update(150.f); // 6 in-game hours
    CHECK_NEAR(w.timeOfDay(), 16.f, 1e-3f);
    // wraps past midnight
    w.setTimeOfDay(23.5f);
    w.update(150.f);
    CHECK_NEAR(w.timeOfDay(), 5.5f, 1e-3f);
}

void testLightning() {
    WeatherSystem w(42u);
    w.setParams(presetFor(WeatherType::Thunderstorm));
    bool struck = false;
    float maxFlash = 0.f;
    for (int i = 0; i < 4000; ++i) { // 200 s of storm at frame dt
        w.update(0.05f);
        maxFlash = std::fmax(maxFlash, w.lightningFlash());
        if (w.lightningFlash() > 0.5f) {
            struck = true;
            CHECK(std::fabs(fuhgl::math::length(w.lightningDirection()) - 1.f) < 1e-3f);
            CHECK(w.lightningDirection().y < 0.f); // bolt points downward
        }
        CHECK(w.lightningFlash() >= 0.f && w.lightningFlash() <= 1.f);
    }
    CHECK(struck);
    CHECK(maxFlash > 0.5f);
    // clear weather: never any lightning
    WeatherSystem calm(42u);
    calm.setParams(presetFor(WeatherType::Clear));
    for (int i = 0; i < 500; ++i) {
        calm.update(1.f);
        CHECK(calm.lightningFlash() == 0.f);
    }
    // deterministic per seed: same seed -> same flash schedule
    WeatherSystem x(99u), y(99u);
    x.setParams(presetFor(WeatherType::Thunderstorm));
    y.setParams(presetFor(WeatherType::Thunderstorm));
    for (int i = 0; i < 600; ++i) {
        x.update(0.05f);
        y.update(0.05f);
        CHECK(x.lightningFlash() == y.lightningFlash());
    }
}

void testAutoAdvance() {
    WeatherSystem w(5u);
    w.setParams(presetFor(WeatherType::Clear));
    w.setAutoAdvance(true, 10.f, 10.f, 5.f);
    const WeatherType start = w.targetType();
    for (int i = 0; i < 40; ++i) w.update(1.f); // 40 s > 10 s interval
    CHECK(w.targetType() != start);
    CHECK(w.isTransitioning() || w.transitionProgress() == 1.f);
}

void testApplyTo() {
    WeatherSystem w(13u);
    w.setTimeOfDay(12.f);
    w.setTarget(WeatherType::Rain, 0.f);
    for (int i = 0; i < 60; ++i) w.update(1.f); // let wetness build up

    fuhgl::render::SceneView view;
    view.lights.push_back(fuhgl::render::Light::point({0.f, 5.f, 0.f}, {1.f, 1.f, 1.f}, 5.f));
    w.applyTo(view);
    // sun fields set, normalized; exactly one directional light kept in sync
    CHECK(std::fabs(fuhgl::math::length(view.sunDirection) - 1.f) < 1e-4f);
    CHECK(view.sunColor.x > 0.f); // noon sun disk visible through thin rain
    CHECK(view.fogDensity > 0.f);
    int directional = 0;
    for (const auto& l : view.lights)
        if (l.type == fuhgl::render::LightType::Directional) {
            ++directional;
            CHECK_NEAR(l.intensity, w.sunIntensity(), 1e-5f);
            CHECK_NEAR(l.direction.y, -view.sunDirection.y, 1e-5f);
        }
    CHECK(directional == 1);
    // night: sun disk off, no new directional light pushed when intensity is 0
    WeatherSystem night(13u);
    night.setTimeOfDay(0.f);
    night.setParams(presetFor(WeatherType::Clear));
    fuhgl::render::SceneView dark;
    night.applyTo(dark);
    CHECK(dark.sunColor.x == 0.f && dark.sunColor.y == 0.f && dark.sunColor.z == 0.f);
    CHECK(dark.lights.empty());
    CHECK(dark.ambient.x < 0.05f);

    // effect-param mapping: rain -> wetness + volumetric cloudiness, storm -> darker exposure
    fuhglgraphics::scene::EffectParams ep;
    w.applyTo(ep);
    CHECK_NEAR(ep.wetnessAmount, w.params().wetness, 1e-6f);
    CHECK(ep.wetnessAmount > 0.5f);
    CHECK_NEAR(ep.volumetricCloudiness, 1.f, 1e-5f); // rain preset coverage = 1
    CHECK(ep.volumetricDensity > 0.01f);
    WeatherSystem storm(13u);
    storm.setTarget(WeatherType::Thunderstorm, 0.f);
    fuhglgraphics::scene::EffectParams epStorm;
    storm.applyTo(epStorm);
    CHECK(epStorm.exposure < 0.8f);
    CHECK(epStorm.volumetricCloudiness > ep.volumetricCloudiness - 1e-6f);
}

} // namespace

void runWeatherTests() {
    testPresets();
    testTransition();
    testWetnessLag();
    testSunPath();
    testLightning();
    testAutoAdvance();
    testApplyTo();
}
