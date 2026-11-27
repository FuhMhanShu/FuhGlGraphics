# Tutorial G38 — Wettersystem (Sonne, Himmel, Wolken, Regen, Gewitter)

> 🇬🇧 **English version:** [docs/en/tutorials/g38_weather.md](../../en/tutorials/g38_weather.md)

**Ziel:** ein `weather::WeatherSystem` steuert das ganze Frame: ein Tageszeit-Sonnenpfad
(Richtung, Farbe, Intensität), ein prozeduraler Himmel mit Wolken (`Effect::Sky`),
wettergetriebene volumetrische Wolken (G21), Regenpartikel (G5) + nasse Böden (G22) und
Blitze (`Effect::Lightning`) — mit weichen, zeitbasierten Übergängen zwischen Wetterzuständen.
Design-Dokument: [docs/WEATHER_SYSTEM.md](../../WEATHER_SYSTEM.md).

## Der CPU-Kern (kein GL)

```cpp
#include <fuhglgraphics/weather/weather.hpp>

fuhglgraphics::weather::WeatherSystem weather(7u);        // geSeeder: deterministische Blitze
weather.setTimeOfDay(10.f);                               // 0..24 h, steuert den Sonnenpfad
weather.setTarget(fuhglgraphics::weather::WeatherType::Rain, 8.f); // weicher 8-s-Übergang
// pro Frame:
weather.update(dtSekunden);                               // Übergänge + Zeit + Nässe-Lag + Blitze
// abgeleitete Ausgaben (reine Funktionen des Zustands):
weather.sunDirection(); weather.sunColor(); weather.sunIntensity();
weather.lightningFlash();   // 0..1 Blitz-Hüllkurve des aktuellen Einschlags
weather.applyTo(sceneView); // Sonne/Ambient/Nebel + Richtungslicht in eine SceneView
```

`WeatherType`-Presets: `Clear`, `PartlyCloudy`, `Overcast`, `Rain`, `Thunderstorm`
(`presetFor()`), jeweils ein `WeatherParams`-Satz (cloudCoverage, cloudDensity, fogDensity,
rainIntensity, stormIntensity, Wind, sunVisibility, wetness). `setTarget(type, sekunden)` mischt
alle Parameter per Smoothstep; `update(dt)` schreitet Mischung, Tageszeit, Nässe-Lag
(wird in ~3 s nass, trocknet in ~45 s) und den geseedeten Blitz-Zeitplan fort.
`setAutoAdvance(true)` wählt in einem Timer zufällige neue Ziele.

## Scene-Wiring (G9 → G3)

```cpp
using namespace fuhglgraphics;
scene::Scene scene;
// ... Nodes aufbauen ...
auto& wc = scene.root().createChild("weather").attach<scene::WeatherComponent>();
wc.system = weather::WeatherSystem(7u);

auto& rain = scene.root().createChild("rain").attach<scene::ParticleEmitterComponent>();
rain.system.create(6000, weather::rainEmitterConfig(wc.system.params())); // braucht GL-Kontext

// pro Frame:
wc.system.update(dt);
weather::makeRainEmitter(rain.system, wc.system.params()); // Spawn-Rate ~ rainIntensity
rain.system.update(dt);

scene::FrameData frame;
scene.fillFrame(frame, &cam, 1280, 720);   // wendet das Wetter auf frame.view + frame.effects.params an
frame.effects.enable(render::Effect::Sky);
frame.effects.enable(render::Effect::VolumetricFog);
frame.effects.enable(render::Effect::Wetness);
frame.effects.enable(render::Effect::Particles);
frame.effects.enable(render::Effect::Lightning);
frame.effects.applyTo(renderer);
renderer.skyTime += dt;                    // scrollt das Wolkenfeld
renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                frame.lightProbes, &frame.decals, frame.water, &frame.skins, &frame.subsurface,
                frame.weather);        // G38: der Wetterzustand treibt die Pässe
```

`Scene::fillFrame()` überträgt den Zustand der ersten `WeatherComponent` in `frame.view`
(Sonne/Ambient/Nebel/Lichter) und `frame.effects.params` (G21-Volumen-Dichte/-Wolkigkeit, G22-Nässe,
Exposure) und exponiert ihn als `frame.weather`. Übergibt man ihn an
`DeferredRenderer::render(..., weather)`, wirkt er zusätzlich:

| Pass | Effect | Was das Wetter steuert |
| --- | --- | --- |
| Lighting (Hintergrundpixel) | `Effect::Sky` | prozeduraler Himmel: Tag/Nacht-Verlauf je Sonnenhöhe, HDR-Sonnenscheibe + Halo, windgescrollte fBm-Wolkenschicht (Coverage/Dichte aus dem Wetter-Block); Sky-Cubemap bleibt Fallback |
| „volumetric"-Postpass | `Effect::VolumetricFog` | `WeatherSystem::applyVolumetricParams()` mappt cloudCoverage/cloudDensity auf Volumen-Dichte + Wolkigkeit — Sturmwolken bauen sich im Volumen auf |
| Lighting | `Effect::Wetness` | `wetnessAmount = max(wetnessAmount, weather.wetness)` — die gelaggte Bodenfeuchte mit Trocknungs-Tail |
| Lighting + temporäres Licht | `Effect::Lightning` | Sky-Flash-Uniform `uLightningFlash` + Punktlicht am Blitz für 1–2 Frames während `lightningFlash() > 0` |
| Tonemap | — | `uExposure *= weather.exposureScale()` (Gewitter dunkeln das Frame ab) |

Der Wetterzustand serialisiert über G19 (`scene::WeatherComponent` → `"weather"`-JSON: Seed,
Ziel-Typ, Parameter, Tageszeit, Tageslänge, Breitengrad). CPU-Zwillinge des prozeduralen Himmels
(Verlauf, Wolkenfeld, `proceduralSkyColor`) liegen in `render/sky.hpp` und sind per Unit-Test
abgedeckt.

## Blitz-Bolt-Mesh (optional)

```cpp
fuhgl::render::Mesh boltMesh;
boltMesh.setMeshData(weather::makeLightningBolt(seed, ws.lightningDirection(), 60.f)); // G26-L-System
// als MeshEntry mit HDR-emissiver Farbe anhängen, solange ws.lightningFlash() > 0 — Bloom (G17) fängt es auf
```

## Demo + Ergebnis

`examples/weather_screenshot` — Platz mit Kisten und emissiver Säule, pro Wetterzustand gerendert
(`--weather clear|partly|overcast|rain|storm`, `--tod`, `--transition`, `--flash`):

- [g38_weather_clear.png](../../images/g38_weather_clear.png) — klarer Tag, wenige Wolken
- [g38_weather_partly.png](../../images/g38_weather_partly.png) — aufgelockerte Wolkendecke
- [g38_weather_overcast.png](../../images/g38_weather_overcast.png) — geschlossene Decke, gedämpfte Sonne
- [g38_weather_rain.png](../../images/g38_weather_rain.png) — Regenstreifen + nasser Boden (G22)
- [g38_weather_storm.png](../../images/g38_weather_storm.png) — Gewitter in der Dämmerung mit
  Blitz + emissivem Bolt (`--flash`)
- [g38_weather_transition.png](../../images/g38_weather_transition.png) — Frame mitten im
  Übergang (`--transition 8 --frames 32`)

CI rendert alle in den `docs-screenshots`-Job (`docs/images/ci/g38_weather_*.png`).
Tests: `tests/test_weather.cpp` (Unit: Übergänge, Sonnenpfad, Nässe-Lag, Blitz-Zeitplan,
Rain-Emitter-Konfig, Bolt-Mesh, Sky-Zwillinge, Serialisierung) und `tests/gl_smoke_weather.cpp`
(Sky an/aus, Wetterzustände, Regen + Nässe, Blitz-Frame-Helligkeit).
