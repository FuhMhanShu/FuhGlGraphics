# Tutorial G13 — Realistisches Wasser (`fuhglgraphics::water`)

> 🇬🇧 **English version:** [docs/en/tutorials/g13_water.md](../../en/tutorials/g13_water.md)

**Ziel:** eine realistisch wirkende Wasseroberfläche — Gerstner-Wellen, planare Reflexionen
von Szene und Himmel, Refraktion mit Tiefenabsorption, Fresnel, Schaum auf den Wellenkämmen
(„Gicht") und Uferschaum, Sonnen-Glint — auf der GL-3.3-Basislinie. Toggle: `render::Effect::Water`.

## Design

- `water::WaterSurface` (`include/fuhglgraphics/water/water_surface.hpp`) — ein flaches
  Grid-Mesh in der lokalen XZ-Ebene (`create(resolution, size)`, braucht GL-Kontext) plus
  die Wellenliste (`std::vector<GerstnerWave> waves`, max. 8) und Shading-Parameter
  (Deep-/Shallow-/Foam-Farben, `fresnelF0`, `reflectionStrength`, `distortion`,
  `foamCrestStart`, `shoreFoamWidth`, `glintPower`, …). `update(dt)` verschiebt die
  Wellenzeit.
- **Wellen:** Gerstner-Wellensumme, ausgewertet im *Vertex-Shader* des Wasser-Passes
  (Verschiebung + analytische Normale aus den partiellen Ableitungen). Dieselbe Mathematik
  existiert als pure CPU-Funktion `water::evalGerstner()` — ohne GL unit-getestet
  (`tests/test_water.cpp`).
- **G3-Renderer:** `render(scene, &materialIds, &castShadow, &particleSystems,
  lightProbes, water)` mit `Effect::Water` an:
  1. **Planarer Reflexions-Pass** — die Szene (ohne das Wasser-Mesh) wird aus der
     gespiegelten Kamera (`reflectionMatrix()` um die Wasserebene) in
     `renderer.reflectionTexture()` (RGBA16F, interne Größe × `waterReflectionScale`)
     gerendert, unterhalb der Wasserebene geclippt (Clip-Space-z-Trick im Reflexions-VS)
     und mit invertierter Winding. Hintergrund = Sky-Cubemap + Sonnenscheibe; Geometrie =
     günstiges Lambert+Blinn+Fog (keine Schatten in der Reflexion).
  2. Das Wasser-Mesh bleibt **außerhalb des G-Buffers** (das Lighting-HDR ist damit der
     Refraktions-Hintergrund) und wirft keine Schatten.
  3. **Wasser-Post-Pass** (Lighting → Water → Partikel → TAA → Upscale → Tonemap): das Mesh
     wird in die HDR-Kette gezeichnet; der Fragment-Shader mischt Reflexion und Refraktion
     per Fresnel, addiert Kamm-Schaum aus der Wellenhöhe, Uferschaum aus der G-Buffer-Tiefe
     (Wassertiefe unter dem Fragment), einen Sonnen-Glint und Nebel. Das Post-FBO hat kein
     Depth-Attachment, die Okklusion wird manuell gegen die G-Buffer-Tiefe getestet.
- **G9:** `scene::WaterComponent` besitzt die Fläche; `Scene::fillFrame()` legt das Mesh in
  die Draw-Items (kein Schatten) und die Fläche in `FrameData::water` (erste Komponente
  gewinnt). Die Node-Transform platziert die Wasserebene (lokal y = 0); ihre AABB wird um
  die maximale Wellenhöhe gepolstert (Culling).

## Code

```cpp
#include <fuhglgraphics/graphics.hpp>

fuhglgraphics::scene::Scene scene;
fuhglgraphics::scene::Node& waterNode = scene.root().createChild("water");
waterNode.translation = {0.f, 2.f, 0.f};              // Wasserstand
auto& wc = waterNode.attach<fuhglgraphics::scene::WaterComponent>();
wc.surface.create(160, 220.f);                        // GL-Kontext nötig
wc.surface.setDefaultWaves();                         // 4 Gerstner-Wellen; oder wc.surface.waves tunen
wc.surface.deepColor    = {0.01f, 0.08f, 0.13f};
wc.surface.shallowColor = {0.08f, 0.35f, 0.40f};
wc.surface.foamCrestStart = 0.55f;                    // Kamm-Schaum („Gicht") ab hier
wc.surface.shoreFoamWidth = 2.5f;                     // Wassertiefe in Welt-Einheiten

scene.root().effects.enable(fuhglgraphics::render::Effect::Water);
// pro Frame:
fuhglgraphics::scene::FrameData frame;
wc.surface.update(dt);                                // Wellen weiterschieben
scene.fillFrame(frame, nullptr, 1280, 720);           // frame.water = &wc.surface
frame.effects.applyTo(renderer);
renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                frame.lightProbes, &frame.decals, frame.water);
```

Ohne Szenengraph (raw `fuhgl::render::SceneView`): Mesh der Fläche als Draw-Item eintragen
und die Fläche als letztes `render()`-Argument übergeben.

## Demo + Ergebnis

`examples/water_screenshot` baut eine See-Szene (Heightmap-Terrain mit überflutetem Becken,
halb versunkene Felsen, Uferbäume, prozedurale Wolken-Sky-Cubemap + IBL, tiefe Sonne, Nebel)
und rendert headless (surfaceless EGL) nach PNG:

```sh
fuhglgraphics_water_screenshot --out water.png --width 1280 --height 720 --frames 12 --time 3.0
# Vergleiche: --cam 1 (Nahaufnahme über den Wellen), --no-reflection --no-foam (aus),
#             --water-off (Effekt deaktiviert: einfacher PBR-Fallback)
```

Ergebnis (Referenzbilder in [docs/images/](../../images/)):
[See, Reflexionen + Schaum](../../images/g13_water_1280x720.png),
[Nahaufnahme über den Wellen](../../images/g13_water_closeup.png),
[ohne Reflexion/Schaum](../../images/g13_water_nofx.png).

## Hinweise / Grenzen

- Eine Wasserfläche pro Frame (erste `WaterComponent`); die Reflexion ist screen-space
  planar (gespiegelte Kamera teilt die Projektion), durch die Wellen-Normale verzerrt —
  korrekt für eine Ebene, kein SSR-Raymarch.
- Der Reflexions-Pass hat keine Schatten und läuft mit `waterReflectionScale` (Default 0.5).
- Mit `Effect::Water` aus wird das Wasser-Mesh als einfache opake PBR-Fläche gerendert.
- TAA behandelt Wasser-Pixel mit den Hintergrund-Motion-Vektoren (Wasser ist nicht im
  G-Buffer); bei bewegter Kamera kann das Wasser leicht schmieren.
