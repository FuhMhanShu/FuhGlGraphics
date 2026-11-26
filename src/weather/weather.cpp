// G38 weather core implementation — see include/fuhglgraphics/weather/weather.hpp.
// Pure CPU: deterministic per seed (splitmix32), no GL, no allocation in update().

#include <fuhglgraphics/weather/weather.hpp>

#include <fuhgl/render/scene.hpp>
#include <fuhglgraphics/scene/effects.hpp>

#include <cmath>
#include <cstring>

namespace fuhglgraphics::weather {

using fuhgl::math::Vec3;
using fuhgl::math::clamp;
using fuhgl::math::lerp;
using fuhgl::math::radians;
using fuhgl::math::smoothstep;

namespace {
constexpr float kTwoPi = 2.f * fuhgl::math::kPi;

float lerpField(float a, float b, float t) { return a + (b - a) * t; }

WeatherParams lerpParams(const WeatherParams& a, const WeatherParams& b, float t) {
    WeatherParams r;
    r.cloudCoverage = lerpField(a.cloudCoverage, b.cloudCoverage, t);
    r.cloudDensity = lerpField(a.cloudDensity, b.cloudDensity, t);
    r.fogDensity = lerpField(a.fogDensity, b.fogDensity, t);
    r.rainIntensity = lerpField(a.rainIntensity, b.rainIntensity, t);
    r.stormIntensity = lerpField(a.stormIntensity, b.stormIntensity, t);
    r.windSpeed = lerpField(a.windSpeed, b.windSpeed, t);
    r.windDirection = lerpField(a.windDirection, b.windDirection, t);
    r.sunVisibility = lerpField(a.sunVisibility, b.sunVisibility, t);
    r.wetness = lerpField(a.wetness, b.wetness, t);
    return r;
}
} // namespace

const char* weatherTypeName(WeatherType t) {
    switch (t) {
        case WeatherType::Clear: return "clear";
        case WeatherType::PartlyCloudy: return "partly_cloudy";
        case WeatherType::Overcast: return "overcast";
        case WeatherType::Rain: return "rain";
        case WeatherType::Thunderstorm: return "thunderstorm";
        default: return "unknown";
    }
}

bool weatherTypeFromName(const char* name, WeatherType& out) {
    for (int i = 0; i < static_cast<int>(WeatherType::Count); ++i) {
        const WeatherType t = static_cast<WeatherType>(i);
        if (std::strcmp(name, weatherTypeName(t)) == 0) {
            out = t;
            return true;
        }
    }
    return false;
}

WeatherParams presetFor(WeatherType type) {
    WeatherParams p;
    switch (type) {
        case WeatherType::Clear:
            p.cloudCoverage = 0.05f; p.cloudDensity = 0.30f; p.fogDensity = 0.0015f;
            p.windSpeed = 2.f; p.sunVisibility = 1.f;
            break;
        case WeatherType::PartlyCloudy:
            p.cloudCoverage = 0.40f; p.cloudDensity = 0.45f; p.fogDensity = 0.003f;
            p.windSpeed = 3.5f; p.sunVisibility = 0.85f;
            break;
        case WeatherType::Overcast:
            p.cloudCoverage = 0.90f; p.cloudDensity = 0.75f; p.fogDensity = 0.008f;
            p.windSpeed = 5.f; p.sunVisibility = 0.35f;
            break;
        case WeatherType::Rain:
            p.cloudCoverage = 1.00f; p.cloudDensity = 0.85f; p.fogDensity = 0.020f;
            p.rainIntensity = 0.70f; p.stormIntensity = 0.10f;
            p.windSpeed = 7.f; p.sunVisibility = 0.15f;
            break;
        case WeatherType::Thunderstorm:
            p.cloudCoverage = 1.00f; p.cloudDensity = 1.00f; p.fogDensity = 0.035f;
            p.rainIntensity = 1.00f; p.stormIntensity = 1.00f;
            p.windSpeed = 12.f; p.sunVisibility = 0.08f;
            break;
        default: break;
    }
    return p;
}

WeatherSystem::WeatherSystem(std::uint32_t seed) : rng_(seed ? seed : 1u) {
    targetParams_ = params_ = presetFor(targetType_);
    scheduleNextFlash();
}

float WeatherSystem::rand() {
    // splitmix32 — same stream as procedural::Noise's seeded permutation
    rng_ += 0x9E3779B9u;
    std::uint32_t z = rng_;
    z = (z ^ (z >> 16)) * 0x21F0AAADu;
    z = (z ^ (z >> 15)) * 0x735A2D97u;
    z ^= z >> 15;
    return static_cast<float>(z >> 8) * (1.f / 16777216.f); // [0,1)
}

void WeatherSystem::setTarget(WeatherType type, float transitionSeconds) {
    targetType_ = type;
    targetParams_ = presetFor(type);
    // keep the *current* wetness as blend start; everything else restarts from params_
    from_ = params_;
    targetParams_.wetness = params_.wetness;
    blendDur_ = transitionSeconds > 0.f ? transitionSeconds : 1e-4f;
    blendT_ = transitionSeconds <= 0.f ? 1.f : 0.f;
    if (blendT_ >= 1.f) params_ = targetParams_;
}

void WeatherSystem::setParams(const WeatherParams& p) {
    params_ = p;
    targetParams_ = p;
    blendT_ = 1.f;
}

void WeatherSystem::setTimeOfDay(float hours) {
    timeOfDay_ = std::fmod(hours, 24.f);
    if (timeOfDay_ < 0.f) timeOfDay_ += 24.f;
}

void WeatherSystem::setDayLengthSeconds(float seconds) { dayLength_ = seconds > 0.f ? seconds : 600.f; }

void WeatherSystem::setLatitudeDeg(float latitudeDeg) { latitude_ = clamp(latitudeDeg, -89.f, 89.f); }

void WeatherSystem::setAutoAdvance(bool on, float minSeconds, float maxSeconds, float transitionSeconds) {
    auto_ = on;
    autoMin_ = minSeconds;
    autoMax_ = maxSeconds > minSeconds ? maxSeconds : minSeconds + 1.f;
    autoTransition_ = transitionSeconds;
    autoTimer_ = 0.f;
    autoNext_ = lerpField(autoMin_, autoMax_, rand());
}

void WeatherSystem::pickNextAutoTarget() {
    const int count = static_cast<int>(WeatherType::Count);
    int next = static_cast<int>(targetType_);
    while (next == static_cast<int>(targetType_))
        next = static_cast<int>(rand() * count) % count;
    setTarget(static_cast<WeatherType>(next), autoTransition_);
    autoTimer_ = 0.f;
    autoNext_ = lerpField(autoMin_, autoMax_, rand());
}

void WeatherSystem::scheduleNextFlash() {
    const float storm = params_.stormIntensity;
    // stormy: a strike every ~2.5..10 s scaled by activity; calm: none
    nextFlashIn_ = storm > 0.05f ? lerpField(10.f, 2.5f, storm) * (0.5f + rand()) : 1e9f;
}

void WeatherSystem::update(float dtSeconds) {
    if (dtSeconds <= 0.f) return;
    const float dt = dtSeconds;

    // time-based smoothstep transition of all params (wetness handled separately below)
    if (blendT_ < 1.f) {
        blendT_ = blendT_ + dt / blendDur_;
        if (blendT_ >= 1.f) {
            blendT_ = 1.f;
            params_ = targetParams_;
        } else {
            params_ = lerpParams(from_, targetParams_, smoothstep(0.f, 1.f, blendT_));
        }
    }

    // time of day advances with game time
    timeOfDay_ = std::fmod(timeOfDay_ + dt / dayLength_ * 24.f, 24.f);

    // wetness lags the rain: wets fast (~3 s), dries slowly (~45 s)
    {
        const float target = params_.rainIntensity;
        const float tau = target > params_.wetness ? 3.f : 45.f;
        params_.wetness += (target - params_.wetness) * (1.f - std::exp(-dt / tau));
        params_.wetness = clamp(params_.wetness, 0.f, 1.f);
    }

    // seeded auto-progression to a new weather state
    if (auto_ && blendT_ >= 1.f) {
        autoTimer_ += dt;
        if (autoTimer_ >= autoNext_) pickNextAutoTarget();
    }

    // lightning schedule (only while stormy); flash envelope = fast attack, two pulses
    if (params_.stormIntensity > 0.05f) {
        if (nextFlashIn_ >= 1e8f) scheduleNextFlash(); // became stormy: arm the schedule
        nextFlashIn_ -= dt;
        if (nextFlashIn_ <= 0.f) {
            flashT_ = 0.f;
            flashEnergy_ = lerpField(0.6f, 1.f, rand());
            // bolt direction: random azimuth, steep downward tilt
            const float az = rand() * kTwoPi;
            const float tilt = radians(lerpField(70.f, 88.f, rand()));
            flashDir_ = Vec3{std::sin(az) * std::cos(tilt), -std::sin(tilt), std::cos(az) * std::cos(tilt)};
            scheduleNextFlash();
        }
    } else {
        nextFlashIn_ = 1e9f;
    }
    if (flashT_ < 1e9f) {
        flashT_ += dt;
        const float pulse = std::exp(-flashT_ * 9.f) + 0.6f * std::exp(-std::fabs(flashT_ - 0.09f) * 30.f);
        flash_ = clamp(flashEnergy_ * pulse, 0.f, 1.f);
        if (flashT_ > 0.6f) {
            flash_ = 0.f;
            flashT_ = 1e9f;
        }
    }
}

Vec3 WeatherSystem::sunDirection() const {
    // hour angle: 0 at noon, +pi at midnight; declination 0 (equinox) — latitude tilts the path
    const float hourAngle = (timeOfDay_ - 12.f) / 12.f * fuhgl::math::kPi;
    const float lat = radians(latitude_);
    const float sinEl = std::cos(lat) * std::cos(hourAngle); // declination 0
    const float el = std::asin(clamp(sinEl, -1.f, 1.f));
    const float az = std::atan2(std::sin(hourAngle), std::cos(hourAngle) * std::sin(lat));
    return Vec3{std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};
}

Vec3 WeatherSystem::sunColor() const {
    const float sinEl = sunDirection().y;
    // white at noon, orange-red at the horizon, dark blue at night
    const float day = smoothstep(-0.05f, 0.15f, sinEl);
    const Vec3 warm{1.f, 0.45f, 0.20f};
    const Vec3 noon{1.f, 0.96f, 0.90f};
    const Vec3 night{0.02f, 0.03f, 0.07f};
    return lerp(lerp(night, warm, clamp(sinEl / 0.25f + 0.5f, 0.f, 1.f)), noon, clamp(sinEl / 0.35f, 0.f, 1.f)) * day +
           night * (1.f - day);
}

float WeatherSystem::sunIntensity() const {
    const float day = smoothstep(-0.05f, 0.15f, sunDirection().y);
    return 3.f * day * params_.sunVisibility; // directional irradiance scale
}

Vec3 WeatherSystem::ambientColor() const {
    const float day = smoothstep(-0.05f, 0.15f, sunDirection().y);
    const Vec3 daySky{0.25f, 0.28f, 0.35f};
    const Vec3 nightSky{0.010f, 0.015f, 0.030f};
    const float shade = (1.f - 0.70f * params_.cloudCoverage) * (1.f - 0.45f * params_.stormIntensity);
    return lerp(nightSky, daySky, day) * shade;
}

Vec3 WeatherSystem::fogColor() const {
    const float day = smoothstep(-0.05f, 0.15f, sunDirection().y);
    const Vec3 dayHorizon{0.60f, 0.70f, 0.85f};
    const Vec3 nightHorizon{0.02f, 0.025f, 0.05f};
    const float shade = 1.f - 0.80f * params_.cloudCoverage * params_.cloudDensity;
    return lerp(nightHorizon, dayHorizon, day) * shade;
}

fuhgl::render::Light WeatherSystem::sunLight() const {
    // Light::directional takes the travel direction (opposite of "toward the sun")
    return fuhgl::render::Light::directional(-sunDirection(), sunColor(), sunIntensity());
}

void WeatherSystem::applyTo(fuhgl::render::SceneView& view) const {
    view.sunDirection = sunDirection();
    const float inten = sunIntensity();
    // HDR brightness of the sun disk (bloom/god rays pick it up); 0 at night
    view.sunColor = inten > 0.f ? sunColor() * (inten * 2.f) : Vec3{0.f, 0.f, 0.f};
    view.ambient = ambientColor();
    view.fogColor = fogColor();
    view.fogDensity = params_.fogDensity;
    // keep exactly one directional light in sync with the sun
    bool found = false;
    for (auto& light : view.lights) {
        if (light.type == fuhgl::render::LightType::Directional) {
            light.direction = -view.sunDirection;
            light.color = sunColor();
            light.intensity = inten;
            found = true;
        }
    }
    if (!found && inten > 0.f) view.lights.push_back(sunLight());
}

void WeatherSystem::applyTo(fuhglgraphics::scene::EffectParams& p) const {
    const WeatherParams& w = params_;
    // G21 volumetric fog/clouds: coverage drives cloudiness, density*coverage drives density
    p.volumetricCloudiness = w.cloudCoverage;
    p.volumetricDensity = 0.004f + 0.04f * w.cloudDensity * (0.3f + 0.7f * w.cloudCoverage) + w.fogDensity;
    p.volumetricStrength = 0.7f + 0.5f * w.cloudCoverage;
    // G22 wetness follows the lagged ground wetness
    p.wetnessAmount = w.wetness;
    p.wetnessPuddle = clamp(0.4f + 0.6f * w.rainIntensity, 0.f, 1.f);
    // storms darken the frame slightly
    p.exposure = 1.f - 0.08f * w.cloudCoverage - 0.22f * w.stormIntensity;
}

} // namespace fuhglgraphics::weather
