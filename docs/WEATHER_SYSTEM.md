# FuhGlGraphics — Weather System (G38) design

Status: **requirement + task list agreed, implementation planned in phases A2–A9.**
Language of this document: English (docs at this level are English-only, see [README.md](README.md)).

Numbering note: G38 follows the PR/design-doc sequence (G32 creature generation …
G37 vegetation generator). The older table numbering inside [REQUIREMENTS.md](REQUIREMENTS.md)
is stale for G30+; the new row there uses G38 to stay collision-free.

## 0. Scope and relation to existing modules

The library already has every low-level building block; G38 composes them into one
weather system with time-based transitions:

| Visual element | Existing module | G38 adds |
|---|---|---|
| Sun disk + god rays + lens flare in the G3 post chain | G3 `DeferredRenderer` (lighting pass draws the sun disk from `SceneView::sunDirection`/`sunColor`; `godRays`, `lensFlareStrength`) | weather-driven sun position/color/intensity (time of day), sky flash on lightning |
| Sky background | G3 `setSkyCubemap` (background pixels of the lighting pass) | `Effect::Sky`: procedural sky (gradient + sun disc + noise cloud layer) replacing/augmenting the cubemap, modulated by weather uniforms |
| Clouds / fog volume | G21 `Effect::VolumetricFog` (`volumetricCloudiness`, height falloff, HG phase) | weather-driven coverage/density so clouds thicken before rain |
| Rain streaks + splashes | G5 `particles::ParticleSystem` (GPU compute ≥ 4.3, CPU fallback) | rain emitter preset, count/intensity driven by `rainIntensity`, wind vector |
| Wet ground / puddles while raining | G22 `Effect::Wetness` (`wetnessAmount`, puddle mask, SSR boost) | `wetnessAmount` coupled to `rainIntensity` (with its own lag so ground dries after the rain stops) |
| Lightning bolt geometry | G26 `procedural::LSystem` (branching turtle → tapered cylinders) | emissive bolt mesh per strike + point-light flash + sky flash (bloom picks it up) |
| Cloud/rain noise | G24 `procedural::Noise` (fBm, ridged, domain warp, seeded) | shared 2D/3D cloud field, wind-scrolled UVs |
| Scene wiring | G9 `scene::Scene`/`FrameData`, per-node `Effects`/`EffectParams` | `scene::WeatherComponent` → `FrameData::weather` → `DeferredRenderer::render(..., weather)` |
| Persistence | G19 `scene/serialize.hpp` (Scene ↔ JSON) | weather state (type, params, time of day, seed) in the scene JSON |

## 1. Requirement (functional)

**G38 — weather system (sun, sky, clouds, rain, thunderstorms) with time-based
transitions.**

1. A CPU-side weather state machine `weather::WeatherSystem` (pure, unit-testable,
   no GL): `enum class WeatherType { Clear, PartlyCloudy, Overcast, Rain, Thunderstorm }`
   (Snow/Fog as stretch), each with a parameter set `WeatherParams`
   (cloudCoverage 0..1, cloudDensity, fogDensity, rainIntensity 0..1, stormIntensity 0..1,
   wind speed + direction, sunVisibility 0..1, lightningActivity 0..1, timeOfDay 0..24 h).
2. **Time-based transitions**: `setTarget(WeatherType, float transitionSeconds)` starts a
   smooth (smoothstep) blend of all params from the current state to the target over the
   given duration; `update(dt)` advances it. Optional seeded auto-progression
   (`autoAdvance`, next random target every N minutes). Time of day advances via
   `advanceTime(dt, hoursPerSecond)` and drives a sun path (elevation/azimuth from a
   latitude parameter) → directional-light direction, color (sunrise/sunset orange, night
   dark) and intensity.
3. **Derived outputs per frame** (pure functions of the state): `sunDirection()`,
   `sunColor()`, `sunIntensity()`, `ambientColor()/ambientIntensity()`, an
   `fuhgl::render::Light` for `SceneView`, and `applyTo(scene::EffectParams&)` mapping
   weather → renderer params (volumetric fog/cloudiness, wetness, exposure, bloom
   threshold; lens flare/god rays scale with sunVisibility).
4. **Renderer adaptation (G3 deferred renderer, explicitly in scope)**: new
   `render::Effect::Sky` and `render::Effect::Lightning` (kEffectCount 27 → 29), a shared
   weather uniform block (coverage, density, wind, time, sun dir, lightning flash) bound
   to the sky and volumetric passes, a procedural-sky branch in the lighting pass for
   background pixels (G-buffer depth == far) that replaces the sky cubemap when
   `Effect::Sky` is on (cubemap stays as fallback/off), and lightning = flash uniform +
   temporary point light + optional emissive L-system bolt mesh. Rain uses the existing
   G5 particle pass; wetness the existing G22 lighting-pass block.
5. Scene integration: `scene::WeatherComponent` holding a `WeatherSystem`;
   `Scene::fillFrame()` copies it into `FrameData::weather`;
   `DeferredRenderer::render(..., const weather::WeatherSystem*)` applies it; state
   serializes through G19.
6. GL 3.3 baseline like everything else (N5 §5): sky pass is GLSL 330; particles keep the
   G5 CPU fallback; every new effect is a per-effect toggle with F39-style gating.

## 2. Data model (CPU, deterministic per seed)

```
WeatherType  ──preset──▶  WeatherParams (target values)
WeatherSystem: current params, target params, blend t 0..1, transition duration,
               timeOfDay, seed, lightning schedule (seeded RNG)
update(dt)  = advance blend (smoothstep) + time of day + lightning schedule
             (pure; no GL, no allocation after construction — N2)
derived:     sunDirection/sunColor/sunIntensity, ambient*, EffectParams mapping,
             rain emitter descriptor, lightning flash events (t, position, energy)
```

## 3. Renderer integration detail (G3 changes)

- `render::Effect::Sky`, `render::Effect::Lightning` added to `effects.hpp`
  (kEffectCount 29); `effectName`/`effectFromName` entries; `EffectParams` gains
  `weather*` fields only if not derivable from the weather uniform block (prefer the
  uniform block — no per-frame struct growth).
- Lighting pass: background branch → when `Effect::Sky` on, shade the procedural sky
  (Rayleigh-ish gradient by sun elevation, sun disc + halo, 2D fBm cloud layer scrolled
  by wind, coverage/density from the weather block, stars at night optional) instead of
  sampling the sky cubemap.
- Volumetric pass (G21): `volumetricCloudiness`/density additionally scaled by weather
  cloud coverage so storm clouds build up in the volume too.
- Lightning: `weather::WeatherSystem` emits flash events; the renderer adds a temporary
  point light to the light loop for 1–2 frames, sets the sky-flash uniform, and the
  optional bolt is a normal emissive `MeshEntry` (G26-generated) with bloom (G17).
- Rain: `weather::makeRainEmitter(ParticleSystem&, WeatherParams)` configures the G5
  emitter (streak billboards, gravity + wind, spawn rate ∝ rainIntensity); wetness
  (`wetnessAmount`) = rainIntensity with a slow decay tail.

## 4. Tasks (Aufgaben)

- [x] **A1 — Requirement + tasks** (this document + G38 row in
      [REQUIREMENTS.md](REQUIREMENTS.md)).
- [x] **A2 — Weather core**: `include/fuhglgraphics/weather/weather.hpp` +
      `src/weather/weather.cpp`: `WeatherType`, `WeatherParams`, `WeatherSystem`
      (presets per type, `setTarget`/`update` smoothstep transitions, time-of-day sun
      path, seeded lightning schedule, derived outputs incl. `applyTo(EffectParams&)`);
      `tests/test_weather.cpp` (deterministic transitions, param mapping, sun path,
      lightning timing). Pure CPU, no GL.
- [ ] **A3 — Sky pass in the G3 renderer**: `Effect::Sky` + weather uniform block +
      procedural-sky branch in the lighting pass (gradient/sun disc/cloud layer,
      cubemap fallback); `tests/gl_smoke_weather.cpp` (sky pixels differ with/without
      the effect and between weather states).
- [ ] **A4 — Clouds**: 2D cloud layer in the sky pass + G21 volumetric coverage driven
      by `cloudCoverage`/`cloudDensity`; wind scrolling. Unit test on the noise field
      (G24), gl_smoke on coverage 0 vs 1.
- [ ] **A5 — Rain**: `weather::makeRainEmitter(...)` (G5 streaks + optional splashes),
      spawn ∝ `rainIntensity`, wind vector; G22 `wetnessAmount` coupling with decay
      tail; `tests/gl_smoke_weather.cpp` rain on/off, wetness follows intensity.
- [ ] **A6 — Thunderstorm**: `Effect::Lightning`, seeded flash schedule, point-light
      flash + sky flash uniform + optional G26 L-system emissive bolt mesh; unit test
      on the schedule, gl_smoke on a flash frame (brightness spike).
- [ ] **A7 — Scene wiring**: `scene::WeatherComponent` → `FrameData::weather` →
      `DeferredRenderer::render(..., weather)`, G19 serialization of the weather state,
      debug-HUD toggle (G10 HUD lists the new effects automatically via kEffectCount).
- [ ] **A8 — Demo + CI**: `examples/weather_screenshot` (`--weather
      clear|partly|overcast|rain|storm`, `--tod 8`, `--transition 6 --frames N` renders
      a transition sequence), reference screenshots `docs/images/g38_weather_*.png`
      wired into the `docs-screenshots` CI job with the perceptual diff.
- [ ] **A9 — Docs**: tutorials `docs/en/tutorials/g38_weather.md` +
      `docs/de/tutorials/g38_weather.md`, entries in USER_GUIDE.md, README.md /
      README.de.md, CHANGELOG.md and the G38 status line in REQUIREMENTS.md;
      Doxygen comments in the new headers (N3/N6).

## 5. Definition of done (per N5)

Per feature: unit test + gl_smoke + tutorial en/de + CI screenshot + REQUIREMENTS /
USER_GUIDE / README / CHANGELOG entries. No new dependencies (N1), no allocations in
the render path (N2), C++17 + PIMPL for GL-owning classes + bool/log errors (N3),
English code comments (N3), GL 3.3 baseline with per-effect toggles (§5).
