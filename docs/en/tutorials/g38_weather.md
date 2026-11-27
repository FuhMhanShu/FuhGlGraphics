# Tutorial G38 — Weather system (sun, sky, clouds, rain, thunderstorms)

> 🇩🇪 **Deutsche Version:** [docs/de/tutorials/g38_weather.md](../../de/tutorials/g38_weather.md)

**Goal:** one `weather::WeatherSystem` drives the whole frame: a time-of-day sun path (direction,
color, intensity), a procedural sky with clouds (`Effect::Sky`), weather-driven volumetric clouds
(G21), rain particles (G5) + wet ground (G22), and lightning strikes (`Effect::Lightning`) —
with smooth time-based transitions between weather states. Design doc:
[docs/WEATHER_SYSTEM.md](../../WEATHER_SYSTEM.md).

## The CPU core (no GL)

```cpp
#include <fuhglgraphics/weather/weather.hpp>

fuhglgraphics::weather::WeatherSystem weather(7u);        // seeded: deterministic lightning
weather.setTimeOfDay(10.f);                               // 0..24 h, drives the sun path
weather.setTarget(fuhglgraphics::weather::WeatherType::Rain, 8.f); // smooth 8 s transition
// per frame:
weather.update(dtSeconds);                                // transitions + time + wetness lag + flashes
// derived outputs (pure functions of the state):
weather.sunDirection(); weather.sunColor(); weather.sunIntensity();
weather.lightningFlash();   // 0..1 flash envelope of the current strike
weather.applyTo(sceneView); // sun/ambient/fog + the sun directional light into a SceneView
```

`WeatherType` presets: `Clear`, `PartlyCloudy`, `Overcast`, `Rain`, `Thunderstorm`
(`presetFor()`), each a `WeatherParams` set (cloudCoverage, cloudDensity, fogDensity,
rainIntensity, stormIntensity, wind, sunVisibility, wetness). `setTarget(type, seconds)` blends
all params with a smoothstep; `update(dt)` advances the blend, the time of day, the wetness lag
(wets in ~3 s, dries in ~45 s) and the seeded lightning schedule. `setAutoAdvance(true)` picks
random targets on a timer.

## Scene wiring (G9 → G3)

```cpp
using namespace fuhglgraphics;
scene::Scene scene;
// ... build nodes ...
auto& wc = scene.root().createChild("weather").attach<scene::WeatherComponent>();
wc.system = weather::WeatherSystem(7u);

auto& rain = scene.root().createChild("rain").attach<scene::ParticleEmitterComponent>();
rain.system.create(6000, weather::rainEmitterConfig(wc.system.params())); // needs a GL context

// per frame:
wc.system.update(dt);
weather::makeRainEmitter(rain.system, wc.system.params()); // spawn rate ~ rainIntensity
rain.system.update(dt);

scene::FrameData frame;
scene.fillFrame(frame, &cam, 1280, 720);   // applies the weather to frame.view + frame.effects.params
frame.effects.enable(render::Effect::Sky);
frame.effects.enable(render::Effect::VolumetricFog);
frame.effects.enable(render::Effect::Wetness);
frame.effects.enable(render::Effect::Particles);
frame.effects.enable(render::Effect::Lightning);
frame.effects.applyTo(renderer);
renderer.skyTime += dt;                    // scrolls the cloud field
renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface,
                frame.weather);        // G38: the weather state drives the passes
```

`Scene::fillFrame()` pushes the state of the first `WeatherComponent` into `frame.view`
(sun/ambient/fog/lights) and `frame.effects.params` (G21 volumetric density/cloudiness, G22
wetness, exposure) and exposes it as `frame.weather`. Passing it to
`DeferredRenderer::render(..., weather)` additionally enables:

| Pass | Effect | What the weather drives |
| --- | --- | --- |
| lighting (background pixels) | `Effect::Sky` | procedural sky: day/night gradient by sun elevation, HDR sun disc + halo, wind-scrolled fBm cloud layer (coverage/density from the weather block); the sky cubemap stays as fallback when off |
| "volumetric" post pass | `Effect::VolumetricFog` | `WeatherSystem::applyVolumetricParams()` maps cloudCoverage/cloudDensity onto the volume density + cloudiness, so storm clouds build up in the volume |
| lighting | `Effect::Wetness` | `wetnessAmount = max(wetnessAmount, weather.wetness)` — the lagged ground wetness with its decay tail |
| lighting + temporary light | `Effect::Lightning` | sky-flash uniform `uLightningFlash` + a point light at the bolt for 1–2 frames while `lightningFlash() > 0` |
| tonemap | — | `uExposure *= weather.exposureScale()` (storms darken the frame) |

The weather state serializes through G19 (`scene::WeatherComponent` → `"weather"` JSON: seed,
target type, params, time of day, day length, latitude). CPU twins of the procedural sky
(gradient, cloud field, `proceduralSkyColor`) live in `render/sky.hpp` and are unit-tested.

## Lightning bolt mesh (optional)

```cpp
fuhgl::render::Mesh boltMesh;
boltMesh.setMeshData(weather::makeLightningBolt(seed, ws.lightningDirection(), 60.f)); // G26 L-system
// attach as a MeshEntry with a HDR emissive color while ws.lightningFlash() > 0 — bloom (G17) picks it up
```

## Demo + result

`examples/weather_screenshot` — plaza with crates and an emissive pillar, rendered per weather
state (`--weather clear|partly|overcast|rain|storm`, `--tod`, `--transition`, `--flash`):

- [g38_weather_clear.png](../../images/g38_weather_clear.png) — clear day, few clouds
- [g38_weather_partly.png](../../images/g38_weather_partly.png) — scattered clouds
- [g38_weather_overcast.png](../../images/g38_weather_overcast.png) — closed deck, dim sun
- [g38_weather_rain.png](../../images/g38_weather_rain.png) — rain streaks + wet ground (G22)
- [g38_weather_storm.png](../../images/g38_weather_storm.png) — thunderstorm at dusk with a
  lightning flash + emissive bolt (`--flash`)
- [g38_weather_transition.png](../../images/g38_weather_transition.png) — mid-transition frame
  (`--transition 8 --frames 32`)

CI renders all of them in the `docs-screenshots` job (`docs/images/ci/g38_weather_*.png`).
Tests: `tests/test_weather.cpp` (unit: transitions, sun path, wetness lag, lightning schedule,
rain-emitter config, bolt mesh, sky twins, serialization) and `tests/gl_smoke_weather.cpp`
(sky on/off, weather states, rain + wetness, flash frame brightness).
