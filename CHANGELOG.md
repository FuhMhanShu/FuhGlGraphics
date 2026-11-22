# Changelog

All notable changes to FuhGlGraphics are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) (the CMake project version is
`0.1.0` with same-major compatibility).

## [Unreleased]

### Added

- **G3 — deferred renderer** (`fuhglgraphics::render::DeferredRenderer`,
  `include/fuhglgraphics/render/deferred_renderer.hpp`, `src/render/`): extended G-buffer
  (albedo+normal, metallic/roughness/AO, HDR emissive, motion vectors, material ID — layout
  in `render/gbuffer.hpp`; optional MSAA with per-attachment resolve), PBR lighting reusing
  FuhGLCore lights (`LightBlock` UBO), `ShadowMap`/`CascadeShadowMap` shadows and IBL
  (`render::Environment`), plus its own post chain on `fuhgl::render::RenderGraph`:
  TAA (Halton jitter + motion-vector history) → FSR1-style EASU+RCAS upscaling
  (`setRenderScale`) → ACES tonemap, with per-effect toggles and F39/F40-style capability
  gating (`render/effects.hpp`). Per-item material IDs via `render(scene, &materialIds)`
  land in the G-buffer MaterialId attachment (preparation for G8 decals).
  Tests: unit tests for the pure parts (`tests/test_render.cpp`, label `unit`) + headless
  GL smoke test (`tests/gl_smoke_render.cpp`, label `gl_context`, skips with exit code 77
  without GL). Screenshot tool `examples/screenshot` renders the demo scene headless
  (surfaceless EGL) to PNG; reference images in `docs/images/`
  (`g3_deferred_1280x720.png`, `g3_deferred_upscale_0.6.png`, `g3_deferred_nofx.png`).
  G3 tutorial in `docs/en/tutorials/g3_deferred_renderer.md` + German translation.
  New CMake option `FUHGLGRAPHICS_BUILD_EXAMPLES` (default ON).
- **G9 — scene graph** (`fuhglgraphics::scene`, `include/fuhglgraphics/scene/`,
  `src/scene/`): `Scene` owns the root `Node`; nodes have a local TRS transform, children
  and a polymorphic component list (`attach<T>` / `find<T>` / `findAll<T>` / `detach`).
  `MeshComponent` holds **several `MeshEntry` meshes per node** — each with its own local
  offset, LOD, `castShadow` flag, material ID and material parameters incl. **albedo +
  normal-map textures per mesh** (explicit preset material or inline fields synced into an
  owned preset). `LightComponent` places a `fuhgl::render::Light` at the node's world
  transform; `CameraComponent` provides the camera for `fillFrame(frame, nullptr, w, h)`.
  Every node carries an **`Effects` object** (`std::vector<render::Effect>` +
  `EffectParams`): `Scene::fillFrame()` merges them per frame (union of the lists, the
  last-visited node's parameters win) and `Effects::applyTo(renderer)` pushes them onto the
  G3 `DeferredRenderer`. **Instancing is a node property** (`Node::instancing`:
  grid/random layouts via `makeLocalInstances()`, one instanced draw call per mesh entry).
  World matrices + world AABBs are maintained for **frustum culling**. `Scene::fillFrame()`
  produces a `FrameData` (SceneView + parallel `materialIds` / `castShadow` + merged
  `Effects`) that is rendered via `renderer.render(frame.view, &frame.materialIds,
  &frame.castShadow)` — the G3 renderer gained the per-item `castShadow` filter for the
  shadow depth pass and normal-map support in the G-buffer pass (derivative-based tangent
  frame, unit `kTextureUnitNormalMap`).
  Tests: `tests/test_scene.cpp` (unit: TRS/world matrices, effects merge, instancing
  layouts, components, fillFrame incl. culling) + `tests/gl_smoke_scene.cpp` (label
  `gl_context`, skips with 77). New tool `examples/scene_screenshot` renders the G9 demo
  scene headless to PNG with CLI effect switches; reference images `docs/images/g9_*.png`
  (`g9_scene_graph.png` all effects, `g9_no_shadows.png`, `g9_no_ibl.png`,
  `g9_upscale_scale0.6.png`, `g9_no_effects.png`). G9 tutorial in
  `docs/en/tutorials/g9_scene_graph.md` + German translation.
- **G3 extensions — sky cubemap + god rays**: `DeferredRenderer::setSkyCubemap(&cube)`
  renders a sky cubemap for background pixels in the lighting pass (unit
  `kTextureUnitSkyCube` = 11) instead of the gradient; `renderer.godRays` adds god rays
  in the tonemap pass (radial blur toward the sun's screen position, weighted toward the
  sun end, occlusion-tested against the G-buffer depth passed as a second pass input —
  geometry casts real light shafts; the sun disk comes from the first directional light).
  Covered by `tests/gl_smoke_scene.cpp` (sky cubemap sampled in the background; god rays
  strictly brighten the frame). New tool `examples/landscape_screenshot`: G9 landscape
  (heightmap terrain + ~170 two-mesh tree nodes, procedural fBm cloud sky cubemap also
  baked as IBL env, low warm sun, distance fog, CSM shadows, god rays) with CLI switches
  (`--no-rays/--no-fog/--no-clouds/--no-shadows/--no-ibl`); reference images
  `docs/images/g9_landscape_sky_sun_fog_rays.png` and `g9_landscape_no_rays.png`,
  rendered by the CI `docs-screenshots` job too.
- Initial skeleton with agreed requirements G1–G11 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) plus the dependency wiring:
  CMake ≥ 3.20 static library `fuhglgraphics` (alias/export
  `FuhGlGraphics::fuhglgraphics`, install + `find_package(FuhGlGraphics)`),
  direct FuhGLCore dependency resolved via
  `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>` (add_subdirectory) →
  `find_package(FuhGLCore)` → FetchContent fallback from
  `github.com/FuhMhanShu/FuhGLCore` (`FUHGLGRAPHICS_FUHGLCORE_GIT_TAG`, default
  `main`); optional Qt6 (`FUHGLGRAPHICS_WITH_QT=ON` default, OFF = Qt-free
  headless core); options `FUHGLGRAPHICS_BUILD_TESTS` /
  `FUHGLGRAPHICS_ENABLE_HARDENING`; version API
  (`include/fuhglgraphics/version.hpp`, `src/version.cpp`) + umbrella header
  `graphics.hpp`; mini test runner with unit tests (`ctest -L unit`);
  docs (README.md/README.de.md, docs/ with language picker, USER_GUIDE.md,
  DEVELOPMENT.md, Doxyfile, en/de tutorial placeholders), CI matrix
  (Linux with Qt6, win/mac Qt-free), SECURITY.md, .gitignore.
- qmake support: `FuhGlGraphics.pro` builds the standalone static library
  (`qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" FuhGlGraphics.pro && make` →
  `libfuhglgraphics.a`), pulling in FuhGLCore via `fuhglcore.pri`
  (`FUHGLCORE_DIR` as qmake var or environment, `error()` when unset);
  `FUHGLGRAPHICS_WITH_QT=1` (default) adds `QT += widgets openglwidgets` +
  `automoc` + `FUHGLGRAPHICS_WITH_QT=1`, `0` builds the Qt-free headless core.
  Source lists use per-directory globs (no `**`, which expands to nothing on
  some Qt6 qmake versions).
