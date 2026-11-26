#pragma once

/// @file weather.hpp
/// @brief G38: weather system core — a pure CPU weather state machine (no GL, no I/O,
/// deterministic per seed): weather states (clear / clouds / overcast / rain /
/// thunderstorm) with time-based smooth transitions, a time-of-day sun path and a
/// seeded lightning schedule. Per frame it derives the sun light, sky/ambient/fog
/// colors and the renderer effect parameters (volumetric clouds G21, wetness G22,
/// exposure) via applyTo(). The GL side (sky pass, rain particles, lightning flash)
/// consumes these values — see docs/WEATHER_SYSTEM.md.

#include <fuhgl/math.hpp>
#include <fuhgl/render/light.hpp>
#include <fuhgl/render/mesh.hpp>
#include <fuhglgraphics/particles/particle_system.hpp>

#include <cstdint>

namespace fuhgl::render { struct SceneView; }
namespace fuhglgraphics::scene { struct EffectParams; }

namespace fuhglgraphics::weather {

/// @brief Weather states. presetFor() maps each to its target WeatherParams.
enum class WeatherType {
    Clear = 0,        ///< blue sky, few clouds, full sun
    PartlyCloudy,     ///< scattered clouds
    Overcast,         ///< closed cloud deck, dim sun
    Rain,             ///< rain + wet ground
    Thunderstorm,     ///< heavy rain + lightning
    Count
};

/// @brief Human-readable name of a WeatherType (logs, serialization).
const char* weatherTypeName(WeatherType t);
/// @brief Inverse of weatherTypeName(). False when unknown.
bool weatherTypeFromName(const char* name, WeatherType& out);

/// @brief All continuously blended weather parameters. Ranges: coverage/density/rain/
/// storm/sunVisibility/wetness in [0,1], windSpeed in [0, 30] world units/s,
/// windDirection in radians (XZ plane, direction the wind blows toward), fogDensity >= 0.
struct WeatherParams {
    float cloudCoverage = 0.2f;  ///< fraction of the sky covered by clouds
    float cloudDensity = 0.5f;   ///< cloud thickness/darkness (also drives volumetric density)
    float fogDensity = 0.002f;   ///< exponential distance fog (SceneView::fogDensity, F16)
    float rainIntensity = 0.f;   ///< 0 = dry, 1 = downpour (rain particles + wetness)
    float stormIntensity = 0.f;  ///< 0..1 lightning activity
    float windSpeed = 2.f;       ///< world units/s, scrolled cloud/rain direction magnitude
    float windDirection = 0.f;   ///< radians in the XZ plane (0 = +Z)
    float sunVisibility = 1.f;   ///< attenuates sun disk, god rays, lens flare (behind clouds)
    float wetness = 0.f;         ///< ground wetness 0..1 (lags rainIntensity: fast wetting, slow drying)
};

/// @brief Target parameter set of a weather state (the blend goal of setTarget()).
WeatherParams presetFor(WeatherType type);

/// @brief Weather state machine. update(dt) advances the smoothstep transition between
/// the current and the target WeatherParams, the time of day, the wetness lag and the
/// seeded lightning schedule; the derived outputs (sunDirection/sunColor/sunIntensity,
/// ambient/fog, applyTo()) are pure functions of the state. No GL, no allocation after
/// construction, same seed -> same lightning schedule on every platform.
class WeatherSystem {
public:
    explicit WeatherSystem(std::uint32_t seed = 1u);

    // --- state + time-based transitions -------------------------------------------
    /// @brief Starts a smooth (smoothstep) blend of all params to the preset of `type`
    /// over `transitionSeconds` (0 = instant). Cancels a running transition.
    void setTarget(WeatherType type, float transitionSeconds);
    /// @brief Sets the params instantly (cancels any running transition).
    void setParams(const WeatherParams& p);
    WeatherType targetType() const { return targetType_; }
    bool isTransitioning() const { return blendT_ < 1.f; }
    float transitionProgress() const { return blendT_; } ///< 0..1
    const WeatherParams& params() const { return params_; }
    const WeatherParams& targetParams() const { return targetParams_; }

    // --- time of day ----------------------------------------------------------------
    void setTimeOfDay(float hours);            ///< 0..24, 12 = noon
    float timeOfDay() const { return timeOfDay_; }
    void setDayLengthSeconds(float seconds);    ///< real seconds per in-game 24 h (default 600)
    float dayLengthSeconds() const { return dayLength_; }
    void setLatitudeDeg(float latitudeDeg);    ///< tilts the sun path (default 45)
    float latitudeDeg() const { return latitude_; }
    /// @brief Optional seeded auto-progression: every [minSeconds, maxSeconds] of game
    /// time a new random target state is chosen and blended over `transitionSeconds`.
    void setAutoAdvance(bool on, float minSeconds = 60.f, float maxSeconds = 180.f,
                        float transitionSeconds = 20.f);

    /// @brief Advances transitions, time of day, wetness lag and lightning. dt in seconds.
    void update(float dtSeconds);

    // --- derived outputs (pure functions of the state) -------------------------------
    fuhgl::math::Vec3 sunDirection() const;  ///< direction TO the sun, normalized (y = sin elevation)
    fuhgl::math::Vec3 sunColor() const;      ///< linear RGB (warm at sunrise/sunset, dark at night)
    float sunIntensity() const;              ///< directional irradiance scale (0 at night)
    fuhgl::math::Vec3 ambientColor() const;  ///< sky ambient for SceneView::ambient
    fuhgl::math::Vec3 fogColor() const;      ///< horizon color for SceneView::fogColor
    fuhgl::render::Light sunLight() const;   ///< directional light for SceneView::lights
    /// @brief Current lightning flash envelope 0..1 (0 = no flash); stormIntensity drives
    /// the seeded flash schedule. Consumed by the renderer's Effect::Lightning pass.
    float lightningFlash() const { return flash_; }
    /// @brief Direction of the current lightning bolt (from cloud toward ground), normalized.
    fuhgl::math::Vec3 lightningDirection() const { return flashDir_; }

    /// @brief Pushes sun/fog/ambient + the sun directional light into a SceneView.
    void applyTo(fuhgl::render::SceneView& view) const;
    /// @brief Maps the weather onto renderer effect params: volumetric clouds (G21),
    /// wetness (G22), exposure. Call per frame after update().
    void applyTo(fuhglgraphics::scene::EffectParams& params) const;

    /// @brief Exposure scale of the current state (storms darken the frame) — used by
    /// applyTo(EffectParams&) and by the G3 tonemap pass when a WeatherSystem is active.
    float exposureScale() const;
    /// @brief Maps the weather onto the G21 volumetric params (single source of truth for
    /// applyTo(EffectParams&) and DeferredRenderer::drawVolumetric): cloud coverage drives
    /// the cloudiness, density*coverage drives the volume density, plus the distance fog.
    void applyVolumetricParams(float& density, float& cloudiness, float& strength) const;
    /// @brief Construction seed (serialization round-trip).
    std::uint32_t seed() const { return seed_; }

private:
    float rand();                 ///< splitmix32, deterministic per seed
    void scheduleNextFlash();     ///< seeded time until the next lightning strike
    void pickNextAutoTarget();    ///< seeded random next WeatherType (auto-advance)

    WeatherParams params_, from_, targetParams_;
    WeatherType targetType_ = WeatherType::Clear;
    float blendT_ = 1.f;          ///< transition progress 0..1
    float blendDur_ = 1.f;        ///< transition duration in seconds
    float timeOfDay_ = 10.f;      ///< hours 0..24
    float dayLength_ = 600.f;     ///< real seconds per 24 h
    float latitude_ = 45.f;       ///< degrees
    bool auto_ = false;
    float autoMin_ = 60.f, autoMax_ = 180.f, autoTransition_ = 20.f;
    float autoTimer_ = 0.f, autoNext_ = 60.f;
    std::uint32_t rng_;
    float nextFlashIn_ = 1e9f;    ///< seconds until the next strike (large = none scheduled)
    float flashT_ = 1e9f;         ///< seconds since the current flash started
    float flash_ = 0.f;           ///< current flash envelope 0..1
    float flashEnergy_ = 0.f;
    fuhgl::math::Vec3 flashDir_{0.f, -1.f, 0.f};
    std::uint32_t seed_;          ///< construction seed (serialization round-trip)
};

// --- GL-side helpers (consumed by the G3 renderer / examples) -------------------------

/// @brief G5 rain-emitter preset derived from the weather state (A5): streaks falling
/// along -Y + wind, spawn rate proportional to rainIntensity, alpha-blended bluish-white
/// streaks, short lifetime, strong drag. Deterministic per params; no GL.
particles::EmitterConfig rainEmitterConfig(const WeatherParams& w);

/// @brief Configures an existing G5 particle system as the rain emitter of `w`
/// (calls system.setEmitter(rainEmitterConfig(w))). The system still needs create() once
/// with a GL context; call system.update(dt) per frame.
inline void makeRainEmitter(particles::ParticleSystem& system, const WeatherParams& w) {
    system.setEmitter(rainEmitterConfig(w));
}

/// @brief G26-based lightning bolt mesh (A6): a branching L-system grown along +Y and
/// rotated onto `dir` (normalized, usually steeply downward), `length` world units long.
/// Tapered cylinders, 11 floats/vertex (pos/normal/color/uv). Deterministic per seed;
/// render it as an emissive MeshEntry during a flash (bloom G17 picks it up).
fuhgl::render::MeshData makeLightningBolt(std::uint32_t seed, const fuhgl::math::Vec3& dir,
                                          float length);

} // namespace fuhglgraphics::weather
