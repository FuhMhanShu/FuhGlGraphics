# Tutorial G13 — Realistic water (`fuhglgraphics::water`)

> 🇩🇪 **Deutsche Version:** [docs/de/tutorials/g13_water.md](../../de/tutorials/g13_water.md)

**Goal:** a realistic water surface — Gerstner waves, planar reflections of the scene and
the sky, refraction with depth absorption, fresnel, wave-crest foam ("sea spray") and
shore foam, sun glint — on the GL 3.3 baseline. Toggle: `render::Effect::Water`.

## Design

- `water::WaterSurface` (`include/fuhglgraphics/water/water_surface.hpp`) — a flat grid
  mesh in the local XZ plane (`create(resolution, size)`, needs a GL context) plus the
  wave list (`std::vector<GerstnerWave> waves`, max 8) and shading parameters (deep/shallow/
  foam colors, `fresnelF0`, `reflectionStrength`, `distortion`, `foamCrestStart`,
  `shoreFoamWidth`, `glintPower`, …). `update(dt)` advances the wave time.
- **Waves:** Gerstner wave sum, evaluated in the water-pass *vertex shader* (displacement +
  analytic normal from the partial derivatives). The identical math exists as the pure CPU
  function `water::evalGerstner()` — unit-tested without GL (`tests/test_water.cpp`).
- **G3 renderer:** `render(scene, &materialIds, &castShadow, &particleSystems,
  lightProbes, water)` with `Effect::Water` on:
  1. **Planar reflection pass** — the scene (without the water mesh) is rendered from the
     mirrored camera (`reflectionMatrix()` about the water plane) into
     `renderer.reflectionTexture()` (RGBA16F, internal size × `waterReflectionScale`),
     clipped below the water plane (clip-space z trick in the reflection vertex shader) and
     with inverted winding. Background = sky cubemap + sun disk; geometry = cheap
     lambert+blinn+fog (no shadows in the reflection).
  2. The water mesh is **kept out of the G-buffer** (so the lighting HDR is the refraction
     background) and never casts shadows.
  3. **Water post pass** (lighting → water → particles → TAA → upscale → tonemap): the mesh
     is drawn into the HDR chain; the fragment shader mixes reflection and refraction by
     fresnel, adds crest foam from the wave height, shore foam from the G-buffer depth
     (water depth below the fragment), a sun glint, and fog. The post FBO has no depth
     attachment, so occlusion is tested manually against the G-buffer depth.
- **G9:** `scene::WaterComponent` owns the surface; `Scene::fillFrame()` puts the mesh into
  the draw items (no shadow) and the surface into `FrameData::water` (first component wins).
  The node's transform places the water plane (local y = 0); its AABB is padded by the max
  wave height for culling.

## Code

```cpp
#include <fuhglgraphics/graphics.hpp>

fuhglgraphics::scene::Scene scene;
fuhglgraphics::scene::Node& waterNode = scene.root().createChild("water");
waterNode.translation = {0.f, 2.f, 0.f};              // water level
auto& wc = waterNode.attach<fuhglgraphics::scene::WaterComponent>();
wc.surface.create(160, 220.f);                        // GL context required
wc.surface.setDefaultWaves();                         // 4 Gerstner waves; or tune wc.surface.waves
wc.surface.deepColor    = {0.01f, 0.08f, 0.13f};
wc.surface.shallowColor = {0.08f, 0.35f, 0.40f};
wc.surface.foamCrestStart = 0.55f;                    // crest foam ("Gicht") from here on
wc.surface.shoreFoamWidth = 2.5f;                     // world units of water depth

scene.root().effects.enable(fuhglgraphics::render::Effect::Water);
// per frame:
fuhglgraphics::scene::FrameData frame;
wc.surface.update(dt);                                // advance the waves
scene.fillFrame(frame, nullptr, 1280, 720);           // frame.water = &wc.surface
frame.effects.applyTo(renderer);
renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                frame.lightProbes, &frame.decals, frame.water);
```

Raw `fuhgl::render::SceneView` without the scene graph: add the surface's mesh as a draw
item and pass the surface as the last `render()` argument.

## Demo + result

`examples/water_screenshot` builds a lake scene (heightmap terrain with a flooded basin,
rocks half-submerged in the water, shore trees, procedural cloud sky cubemap + IBL, low sun,
fog) and renders headless (surfaceless EGL) to PNG:

```sh
fuhglgraphics_water_screenshot --out water.png --width 1280 --height 720 --frames 12 --time 3.0
# comparisons: --cam 1 (close-up over the waves), --no-reflection --no-foam (off),
#              --water-off (effect disabled: plain PBR fallback)
```

Result (reference images in [docs/images/](../../images/)):
[lake, reflections + foam](../../images/g13_water_1280x720.png),
[close-up over the waves](../../images/g13_water_closeup.png),
[without reflection/foam](../../images/g13_water_nofx.png).

## Notes / limits

- One water surface per frame (the first `WaterComponent`); the reflection is screen-space
  planar (mirrored camera shares the projection), distorted by the wave normal — correct
  for a plane, no SSR raymarch.
- The reflection pass has no shadows and runs at `waterReflectionScale` (default 0.5).
- With `Effect::Water` off, the water mesh is rendered as a plain opaque PBR surface.
- TAA treats water pixels with the background motion vectors (the water is not in the
  G-buffer); with a moving camera this can smear the water slightly.
