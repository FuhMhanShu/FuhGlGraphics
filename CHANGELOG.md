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
- **G4 — glTF 2.0 asset loading** (`fuhglgraphics::io`, `include/fuhglgraphics/io/`,
  `src/io/`): own minimal JSON parser + base64 (`io/json.hpp`, no nlohmann) and
  `io::loadGltf(path)` for .gltf (JSON) and .glb (binary container) — buffers (external
  .bin, embedded base64 data-URIs, GLB BIN chunk), bufferViews/accessors (all component
  types, normalized, strided, sparse), primitives (TRIANGLES/STRIP/FAN with
  POSITION/NORMAL/TEXCOORD_0/COLOR_0 + indices → FuhGLCore `render::MeshData`),
  `KHR_materials_pbrMetallicRoughness` (factors + baseColor/normal textures, alphaMode,
  doubleSided), images (external/embedded; PNG/JPEG → RGBA8 via Qt QImage when
  `WITH_QT=ON`, else factors only), samplers, nodes (TRS + matrix→TRS), scenes.
  `io::instantiateGltf(asset, scene, gpu, parent)` maps the default scene onto the G9
  scene graph with GL resources (one `render::Mesh` per primitive, preset materials with
  textures, one `MeshEntry` per primitive, `castShadow = true`, running material IDs).
  bool+log errors, no exceptions; no vendored third-party assets (tests use generated
  fixtures in `tests/gltf_fixture.hpp`). Tests: `tests/test_io.cpp` (unit) +
  `tests/gl_smoke_gltf.cpp` (label `gl_context`, skip 77; with `WITH_QT=ON` also a PNG
  decode roundtrip). New tool `examples/gltf_demo` (`--model x.glb --out shot.png ...`)
  renders any glTF model headless to PNG; reference images `docs/images/g4_*.png`
  (Kenney CC0 ship/chest, pinned commit — see the G4 tutorial). G4 tutorial in
  `docs/en/tutorials/g4_gltf_loader.md` + German translation.
- **G3 shadows on the FuhGLCore F45 shadow kit** (requires FuhGLCore >= `9fc991c`, PR #42
  "shadow kit"): `ShadowMap`/`CascadeShadowMap` are now thin wrappers over
  `ShadowTarget`; the G3 `DeferredRenderer` gained `setShadowFilter(ShadowFilter)`
  (Pcf3x3 legacy / PoissonPcf / Pcss / Vsm — VSM renders RGBA32F moments into an extra
  attachment), `shadowSnapToTexels` (texel-snapped cascade VPs, no swimming),
  `shadowCascadeBlend` (cross-fade near the splits), normal-offset bias, and the GLSL
  snippets are composed from `fuhgl/render/shadow_glsl.hpp` (`kSkFuncs`). New:
  `enablePointShadow()` (F46 `CubeShadowMap` for one point light, Effect::PointShadows)
  and `contactShadowsEnabled` (screen-space raymarched contact shadows against the
  G-buffer depth in the lighting pass, Effect::ContactShadows). `render::Effect` gained
  `PointShadows` + `ContactShadows` (kEffectCount = 9); `scene::EffectParams` gained
  shadowFilter/snap/cascadeBlend/lightSize/minVariance/normalOffset/pcfRadius, applied
  by `Effects::applyTo()`. Sky cubemap moved from texture unit 11 to 16 (kit uses 11–15).
  `examples/landscape_screenshot` gained `--shadow-filter/--cascade-blend/--light-size/
  --point-shadows/--contact-shadows/--no-snap`; new reference images
  `docs/images/g9_landscape_shadowkit_{pcss,vsm,pcf3x3,lamp}.png`; gl_smoke_render now
  exercises all filters + point/contact shadows. Docs: USER_GUIDE, REQUIREMENTS §6,
  G9 tutorials (en/de) embed the new screenshots; CI docs-screenshots renders them.
- **G5 — GPU particle system** (`fuhglgraphics::particles`, `include/fuhglgraphics/particles/`,
  `src/particles/`): `ParticleSystem` (PIMPL) with emitter + integrate compute shaders over
  an SSBO on GL >= 4.3 (same pattern as FuhGLCore's GpuCuller — the SSBO doubles as the
  divisor-1 instance-attribute buffer), rendered as instanced camera-facing billboards;
  on GL 3.3–4.2 the identical math runs on the CPU (`CpuSim`, pure/unit-tested) and streams
  into a dynamic VBO. The G3 DeferredRenderer gained a "particles" post pass (hdr -> hdrP,
  before TAA): additive/alpha blend, radial falloff, life fade, soft-particle depth fade
  against the G-buffer depth. New `render::Effect::Particles` (kEffectCount = 10); G9
  integration via `scene::ParticleEmitterComponent` -> `FrameData::particleSystems` ->
  `DeferredRenderer::render(..., &particleSystems)`.
  Tests: `tests/test_particles.cpp` (unit: CpuSim) + `tests/gl_smoke_particles.cpp`
  (gl_context: gpu() vs GL version, aliveCount, brighter emitter region, no GL error).
  New tool `examples/particles_screenshot` (`--kind fire|smoke|pollen|all`) with reference
  images `docs/images/g5_particles_{fire,smoke,pollen,all_nofx}.png`. G5 tutorial en/de;
  USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI docs-screenshots renders them.
