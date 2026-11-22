# FuhGlGraphics — User Guide (descriptive)

The technical part is in [DEVELOPMENT.md](DEVELOPMENT.md); the agreed requirements are in
[REQUIREMENTS.md](REQUIREMENTS.md).

## Concepts

- **FuhGLCore base (G1).** FuhGlGraphics does not load GL functions, open windows or do
  math itself — `fuhgl::gl`, `fuhgl::Window`, `fuhgl::math` and the FuhGLCore pipelines
  come from [FuhGLCore](https://github.com/FuhMhanShu/FuhGLCore). FuhGlGraphics links
  `FuhGLCore::fuhglcore` PUBLIC, so consumers get it transitively.
- **Qt6 for editors/tools (G2).** The library core is Qt-free; Qt6 (`Widgets`,
  `OpenGLWidgets`) is only used by `editor/` — a minimal editor with viewport
  (`QOpenGLWidget`), scene tree, inspector and asset browser. Build headless with
  `-DFUHGLGRAPHICS_WITH_QT=OFF`.
- **Deferred renderer (G3).** `fuhglgraphics::render::DeferredRenderer` builds on FuhGLCore's
  `RenderGraph` + `ShadowMap`/`CascadeShadowMap`/`Environment` with an extended G-buffer
  (albedo+normal, metallic/roughness/AO, emissive, motion vectors, material ID — see
  `render/gbuffer.hpp`), PBR lighting reusing FuhGLCore lights/shadows/IBL, and its own post
  chain on a `render::RenderGraph` (TAA → FSR1-style EASU+RCAS upscaling → ACES tonemap) with
  per-effect toggles (`setEffectEnabled(render::Effect::Taa, ...)`) and F39/F40-style
  capability gating. Quick start: [G3 tutorial](en/tutorials/g3_deferred_renderer.md),
  result: [screenshot](images/g3_deferred_1280x720.png).

  ```cpp
  fuhglgraphics::render::DeferredRenderer renderer;
  renderer.create(1280, 720, /*msaaSamples=*/0);   // needs a current GL >= 3.3 context
  renderer.enableCsmShadows(2048, 4);              // shadows via FuhGLCore CascadeShadowMap
  renderer.setRenderScale(0.75f);                  // internal res 75%, FSR1-style upscale
  // per frame:
  renderer.render(scene);                          // fuhgl::render::SceneView
  // renderer.output() is the LDR frame; renderer.texMotion()/texMaterialId()/... the G-buffer
  ```
- **glTF import (G4).** `fuhglgraphics::io::loadGltf(path)` loads glTF 2.0 files
  (core + `KHR_materials_pbrMetallicRoughness`, embedded base64 and external `.bin`
  buffers, PNG/JPEG via Qt when available) into FuhGLCore `render::MeshData` + materials.
  Errors are reported as `bool` + log, never exceptions.
- **Scene graph (G9).** On top of FuhGLCore's flat `DrawItem` list: `Node` with
  local/world transforms, children and components (mesh, light, camera, decal, particle
  emitter, probe), world AABBs for culling, glTF node mapping.
- **Effects (G5–G8, G10).** GPU particles (GL 4.3, CPU fallback), skeletal animation,
  SH light probes / lightmaps, deferred decals, plus quick upgrades: MSAA, CSM
  hardening, temporal upscaling, bokeh DoF, screenshot diffs in CI, debug HUD.
- **Infrastructure (G11).** On-disk asset cache keyed by content hash, JSON config system
  (same minimal in-repo JSON parser as the glTF loader), minimal Qt editor.

## Error handling

Like FuhGLCore: no exceptions for domain errors — fallible APIs return `bool` plus an
info-log string.

## Platforms

Windows (WGL), Linux (X11 + EGL), macOS (Cocoa) — inherited from FuhGLCore. GL 3.3 core
baseline; GL 4.3 gates GPU particles/GPU skinning; macOS caps at GL 4.1.
