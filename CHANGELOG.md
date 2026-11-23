# Changelog

All notable changes to FuhGlGraphics are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) (the CMake project version is
`0.1.0` with same-major compatibility).

## [Unreleased]

### Added

- **G21 — volumetric fog / clouds** (`include/fuhglgraphics/render/volumetric.hpp` pure
  helpers, `kVolumetricFrag` + "volumetric" post pass in the G3 renderer, `Effect::VolumetricFog`
  (kEffectCount = 20, off by default), `EffectParams::volumetric*`): after the particles pass
  and before bloom (hdrP -> hdrV -> hdrB), each pixel's view ray is reconstructed from the
  G-buffer depth (sky marches to `volumetricMaxDist`) and raymarched in `volumetricSteps`
  samples through a 3D-noise volume — exponential height falloff (`volumetricHeightFalloff`)
  times fBm clouds (`volumetricCloudiness`, `volumetricNoiseScale`, `volumetricTime` drift) —
  accumulating transmittance and Henyey–Greenstein sun scattering (`volumetricAnisotropy`) plus
  sky ambient; output `hdr * T + inscatter`. Geometry and sky are fogged. Pure CPU twins
  (`volumetricHash/Noise/Fbm/Density`, `henyeyGreenstein`, `volumetricMarch`) are
  unit-tested. Tests: `tests/test_volumetric.cpp` (unit) + `tests/gl_smoke_volumetric.cpp`
  (label `gl_context`; fog on vs off measurably different, no GL error). Demo:
  `examples/volumetric_screenshot` (terrain scene, off/on/dense) headless to PNG; references
  `docs/images/g21_volumetric_{off,on,dense}.png`. Tutorial:
  `docs/en/tutorials/g21_volumetric_fog.md` + German translation.
  Requirement entry G21 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G20 — terrain + LOD integration** (`scene::TerrainComponent`,
  `include/fuhglgraphics/scene/component.hpp`, `src/scene/component.cpp`): owns a FuhGLCore
  F38/F41 `terrainLods()` LodMesh (procedural heightmap + `levels` grid resolutions, switch
  distances from `LodMesh::defaultDistances` or `customDistances`). `create()` needs a GL
  context; `heightAt(x, z)` is a bilinear CPU heightmap sample for placing objects; `fill()`
  appends one draw item with `lod` set, so the G3 renderer resolves the level per frame from
  the camera distance (`resolveLodMesh`; `SceneView::lodEnabled = false` forces level 0) and
  `DeferredRenderer::render()` now propagates the chosen level back to the caller's
  `SceneView::lastLodLevel`. The terrain node also contributes its bounding-sphere AABB to
  frustum culling (`Node::computeAabb`). Any `MeshEntry::lod` works the same way. Tests:
  `tests/test_terrain_lod.cpp` (unit: selectLevel/defaultDistances/resolveLodMesh/heightAt/
  fillFrame path) + `tests/gl_smoke_terrain.cpp` (label `gl_context`; near camera -> level 0,
  far -> level > 0, lod off -> -1). Demo: `examples/terrain_screenshot` (near/far PNGs with
  printed levels) headless to PNG; references `docs/images/g20_terrain_{near,far}.png`.
  Tutorial: `docs/en/tutorials/g20_terrain_lod.md` + German translation.
  Requirement entry G20 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G19 — scene serialization** (`fuhglgraphics::scene::serialize`,
  `include/fuhglgraphics/scene/serialize.hpp`, `src/scene/serialize.cpp`): Scene <-> `io::Json`
  round-trip — node hierarchy (name + TRS), per-node effects (list + params), instancing and the
  pure-data components (MeshComponent entries with material params/materialId/castShadow/
  localOffset, LightComponent, DecalComponent, AnimationComponent clips) — plus
  `saveScene`/`loadScene` for .json files. GL meshes are stored as JSON mesh specs
  (e.g. `{"shape":"box","halfExtents":[..]}`) and re-created on load through a caller-provided
  `MeshResolver` into a mesh arena; `defaultShapeResolver` covers the procedural box/quad
  shapes. `io::writeJson()`/`io::writeJsonFile()` added to `io/json.*` (pretty printer, escapes,
  shortest round-trip numbers). Basis for G12 `.fasset` project files. Tests:
  `tests/test_serialize.cpp` (unit: writeJson/parseJson round-trip incl. escapes/unicode/numbers,
  full scene JSON round-trip equality, save/load file round-trip, malformed input rejected) +
  `tests/gl_smoke_serialize.cpp` (label `gl_context`; save -> patch box spec -> load with
  defaultShapeResolver -> render, checks items/lights + non-dark frame). Demo:
  `examples/serialization_screenshot` (original vs. loaded-from-file PNGs match, round-trip
  JSON equal) headless to PNG; references `docs/images/g19_serialization_{original,loaded}.png`.
  Tutorial: `docs/en/tutorials/g19_serialization.md` + German translation.
  Requirement entry G19 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G18 — picking / ray-casting on the scene graph** (`fuhglgraphics::scene::picking`,
  `include/fuhglgraphics/scene/picking.hpp`, `src/scene/picking.cpp`): pure CPU module (no GL) —
  `rayFromScreen()` (world ray through a pixel), `rayIntersectAabb()` (slab test, culls node
  subtrees by their world AABBs), `rayIntersectTriangle()` (Möller–Trumbore), and
  `scene::pick()` returning the nearest `PickHit` (node, mesh entry, distance, world point,
  triangle). Triangle tests run against `MeshEntry::meshData` — the CPU `MeshData` copy that
  the glTF loader now sets for every primitive (and that apps set for procedural meshes).
  Basis for editor selection/gizmos (G2/G11/G12). Tests: `tests/test_picking.cpp` (unit: ray
  construction, AABB/triangle intersection, full pick near/far/miss/null-camera) +
  `tests/gl_smoke_picking.cpp` (label `gl_context`; center-pixel pick on a rendered scene +
  marker visibility). Demo: `examples/picking_screenshot` (plaza crates, marker at the hit
  point) headless to PNG; references `docs/images/g18_picking_{marker,nomarker}.png`.
  Tutorial: `docs/en/tutorials/g18_picking.md` + German translation.
  Requirement entry G18 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G17 — bloom** (`include/fuhglgraphics/render/bloom.hpp`, `kBloomFrag` in
  `src/render/deferred_glsl.hpp`, "bloom" post-graph pass in the G3 renderer): after the
  particles pass and before TAA, the HDR image gets a per-channel bright-pass above the
  adjustable `bloomThreshold`, a 9-tap golden-angle blur (`bloomRadius` texels) and is added
  with `bloomStrength` (input `hdrP`, output `hdrB` consumed by the TAA pass, so the halo is
  temporally stabilized). Pure helpers `bloomBrightPass()` / `bloomTaps()` (unit-tested). New
  `render::Effect::Bloom` (kEffectCount = 19, off by default), `EffectParams::bloom*`. Tests:
  `tests/test_bloom.cpp` (unit) + `tests/gl_smoke_bloom.cpp` (label `gl_context`; HDR lamp in
  a dark scene: halo ring appears with bloom on, suppressed with threshold 100). Demo:
  `examples/bloom_screenshot` (dusk plaza, HDR lamps + sun disk) headless to PNG; references
  `docs/images/g17_bloom_{on,off,thr4}.png` (re-rendered g15/g16 references with frame-level
  effect params). Tutorial: `docs/en/tutorials/g17_bloom.md` + German translation.
  Requirement entry G17 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G16 — screen-space reflections (SSR)** (`include/fuhglgraphics/render/ssr.hpp`,
  `kSsrFrag` in `src/render/deferred_glsl.hpp`, "ssr" post-graph pass in the G3 renderer):
  after the lighting pass the reflection vector is marched in world space against the
  G-buffer depth (linearized compare, thickness band, binary refine on hit) and the hit HDR
  color is added masked by fresnel × (1−roughness)² × screen-edge fade × distance fade
  (pure helpers `ssrEdgeFade` / `ssrMask`, unit-tested). The pass reads `hdr/gNormal/gDepth/
  gPbr` and writes `hdrS`, which then feeds the water and particles passes. New
  `render::Effect::Ssr` (kEffectCount = 18, off by default), `EffectParams::ssr*`,
  `DeferredRenderer::ssrWetnessBoost` (G22 adds wetness on top). Tests: `tests/test_ssr.cpp`
  (unit) + `tests/gl_smoke_ssr.cpp` (label `gl_context`; mirror floor + emissive box: the
  floor region is brighter with SSR on). Demo: `examples/ssr_screenshot` headless to PNG;
  references `docs/images/g16_ssr_{on,off}.png`. Tutorial: `docs/en/tutorials/g16_ssr.md` +
  German translation. Requirement entry G16 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G15 — SSAO** (`fuhglgraphics::render`, `include/fuhglgraphics/render/ssao.hpp`,
  `src/render/ssao.cpp`): a new post-graph pass `ssao` runs before the lighting pass —
  world-position reconstruction from the G-buffer depth, a deterministic hemisphere kernel
  (`ssaoKernel(samples, seed)`, per-pixel rotation via `ssaoRotation`, `linearizeDepth` for the
  depth compare, range check + bias) written into the `ssao` resource (RG16F, `.r`), sampled
  by the lighting pass on unit 6 (`uSsao`/`uSsaoEnabled`) multiplying the material AO. New
  `render::Effect::Ssao` (kEffectCount = 17, off by default), `EffectParams::ssa*`,
  `DeferredRenderer::texSsao()`. Tests: `tests/test_ssao.cpp` (unit: kernel/rotation/
  linearization) + `tests/gl_smoke_ssao.cpp` (label `gl_context`; SSAO on vs off differ and
  darken). Demo: `examples/ssao_screenshot` headless to PNG; references
  `docs/images/g15_ssao_{on,off}.png`. Tutorial: `docs/en/tutorials/g15_ssao.md` + German
  translation. Requirement entry G15 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G14 — GPU skinning** (`fuhglgraphics::anim::GpuSkinner`, `include/fuhglgraphics/anim/gpu_skinner.hpp`,
  `src/anim/gpu_skinner.cpp`): the G6 "future work" — a PIMPL class owning one skinned mesh's GPU side:
  a VAO with the bind-pose vertices (standard layout, attribs 0..3) plus JOINTS_0/WEIGHTS_0 as float4
  attributes (locations 5/6) and an SSBO with the joint matrix palette (binding 0).
  `DeferredRenderer` compiles a GLSL-430 skinning variant of the G-buffer vertex shader
  (`kGBufferVertSkin`, `skin = Σ wᵢ·uPalette[jᵢ]`) on GL >= 4.3 and draws the skinner instead of the
  plain mesh when the new `render::Effect::GpuSkinning` is enabled (capability-gated, pure
  `effectSupported()`; CPU fallback `skinMeshCpu` below 4.3 / when off). Scene wiring:
  `MeshEntry::gpuSkin` -> `FrameData::skins` (parallel to `view.items`) ->
  `DeferredRenderer::render(..., skins)`; `MeshEntry::meshData` stores the CPU `MeshData`
  (set by the glTF loader for every primitive — also used by G18 picking). glTF:
  `instantiateGltf()` creates one `GpuSkinner` per skinned primitive on GL >= 4.3
  (`GltfGpuResources::gpuSkins`). `render::Effect` gained `GpuSkinning` (kEffectCount = 16) +
  `effectFromName()`. Tests: `tests/test_render.cpp` (unit: registry/capability/effectFromName) +
  `tests/gl_smoke_gpu_skin.cpp` (label `gl_context`, skips with 77 below GL 4.3; two palettes
  differ with the effect on, identical with it off). Demo: `examples/gpu_skinning_screenshot`
  (robot arm bent via the palette on the GPU) headless to PNG; reference images
  `docs/images/g14_gpu_skinning_{on,off}.png`. Tutorial: `docs/en/tutorials/g14_gpu_skinning.md` +
  German translation. Requirement entry G14 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G13 — realistic water** (`fuhglgraphics::water`, `include/fuhglgraphics/water/water_surface.hpp`,
  `src/water/water_surface.cpp`): `water::WaterSurface` — a Gerstner-wave grid mesh
  (`create(resolution, size)`) with shading parameters (deep/shallow/foam colors, fresnel F0,
  reflection strength, distortion, crest + shore foam, sun glint), animated via `update(dt)`.
  The pure CPU twin `evalGerstner()` (displacement + analytic wave normal) and
  `reflectionMatrix()` / `waterPlaneFromModel()` are unit-tested (`tests/test_water.cpp`).
  G3 renderer extension (`render::Effect::Water`, `DeferredRenderer::render(..., water)`):
  a planar-reflection pass renders the scene (without the water) from the mirrored camera,
  clipped at the water plane, into an RGBA16F target (`reflectionTexture()`, resolution =
  internal size * `waterReflectionScale`); the water mesh stays out of the G-buffer and is
  shaded in a new "water" post pass of the HDR chain (lighting → water → particles → TAA →
  upscale → tonemap): Gerstner displacement + analytic normal in the vertex shader,
  fresnel mix of the planar reflection and the refraction (scene HDR, normal-distorted,
  depth-absorbed), wave-crest foam ("Gicht") + G-buffer-depth shore foam, sun glint, fog,
  manual depth test against the G-buffer depth. G9 integration: `scene::WaterComponent` →
  `FrameData::water` → `render(..., frame.water)`; the node transform places the water
  plane and the node AABB is padded by the max wave height. With the effect off, the water
  mesh falls back to plain G-buffer PBR. Tests: `tests/test_water.cpp` (unit) +
  `tests/gl_smoke_water.cpp` (headless GL, label `gl_context`, skips with 77 without GL).
  Demo: `examples/water_screenshot` (lake scene: terrain basin, rocks, shore trees, cloud
  sky + IBL, sun, fog) headless to PNG; reference images `docs/images/g13_water_*.png`.
  Tutorial: `docs/en/tutorials/g13_water.md` + German translation. Requirement entry G13
  in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

### Planned

- **G12 — asset creator Qt tool** — design agreed, implementation in phases after review:
  `tools/asset_creator/` (executable `fuhglgraphics_asset_creator`, built with
  `FUHGLGRAPHICS_WITH_QT=ON` + new option `FUHGLGRAPHICS_BUILD_TOOLS`) to create/load/edit
  assets: data-structures panel (scene tree + inspector + asset browser), viewport with
  editing tools on the G3 `DeferredRenderer`, LLM chat over MCP (stdio JSON-RPC 2.0,
  configurable server command). Project files are own `.fasset` JSON. Design doc:
  [docs/ASSET_CREATOR.md](docs/ASSET_CREATOR.md); requirement entry G12 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

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
- **G6 — animation/skinning** (`fuhglgraphics::anim`, `include/fuhglgraphics/anim/animation.hpp`,
  `src/anim/animation.cpp`): `AnimationClip`/`TrsChannel` (keyframe channels per node
  name; translation/scale lerp, rotation `fuhgl::math::slerp`), `AnimationPlayer`
  (`addClip`, `play(name, LoopMode::Once|Loop|PingPong, speed)`, `update(dt)`, `applyTo(scene)`
  / node-map overload — writes the sampled TRS into the named nodes; PingPong mirrors via
  the speed sign), `Skeleton` (joint node indices + inverse bind matrices,
  `updatePalette(jointNodePtrs)` → palette = jointWorld * IBM) and CPU skinning
  `skinMeshCpu(base, SkinWeights, palette, out)` (the requirement allows "CPU or GPU";
  a GPU matrix-palette SSBO in the G3 G-buffer pass on GL >= 4.3 is future work).
  G4 extension: glTF `animations[]` (channels/samplers → clips) and `skins[]` (joints +
  inverseBindMatrices) plus JOINTS_0/WEIGHTS_0 on primitives are parsed into
  `GltfAsset::{animations, skins, ...}`; `instantiateGltf()` maps them onto
  `GltfGpuResources::{player, skeletons, nodePtrs, skinnedMeshes}`. G9:
  `scene::AnimationComponent` holds an `anim::AnimationPlayer`.
  Tests: `tests/test_anim.cpp` (unit: sampling, loop modes, applyTo, skinMeshCpu) +
  `tests/test_io.cpp` extended (fixture now carries a clip + skin; index winding fixed to
  CCW) + `tests/gl_smoke_anim.cpp` (gl_context: fixture .glb → player/skeletons/
  skinnedMeshes, CPU-skinned vertex check, rendered frames at t=0 vs t=1 differ).
  New tool `examples/animation_screenshot` (`--out shot.png [--time 0.7] [--frames 16]
  [--no-shadows] [--no-rays] ...`): 2-joint skinned robot arm (CPU palette) + waving flag,
  reference images `docs/images/g6_animation_{bend,straight,nofx}.png`. G6 tutorial
  en/de; USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI docs-screenshots renders
  them.
- **G7 — light probes / SH irradiance GI** (`fuhglgraphics::probes`,
  `include/fuhglgraphics/probes/light_probes.hpp`, `src/probes/light_probes.cpp`):
  `LightProbeGrid` — regular grid of SH L2 irradiance probes over an AABB, baked on the
  CPU (`bake(BakeInput)`: fibonacci-sphere rays vs a world-space `ProbeTriangle` soup,
  one bounce = sky via `skyRadiance(dir)` callback + direct lights with shadow-ray
  occlusion; projected with `shProject`; `shIrradiance` evaluates E(n) = sum A_l c_lm
  Y_lm(n), constant radiance L -> E = pi*L, unit-tested). GL side (GL 3.3 baseline): the
  27 floats per probe are packed into seven RGBA16F 3D textures (units 17..23) and
  sampled with hardware trilinear interpolation in the G3 lighting pass; upload is lazy
  in `render()`. With `render::Effect::LightProbes` (kEffectCount = 11) the diffuse
  ambient becomes `kD * albedo * (E(N)/pi) * ao * lightProbeStrength` (IBL keeps its
  specular part) — diffuse indirect light / color bleeding. G9:
  `scene::LightProbeComponent` -> `FrameData::lightProbes` ->
  `DeferredRenderer::render(..., lightProbes)`; `EffectParams::lightProbeStrength`.
  Tests: `tests/test_probes.cpp` (unit) + `tests/gl_smoke_probes.cpp` (gl_context: upload,
  render on vs off differ, frame brighter with probes, no GL error).
  New tool `examples/probes_screenshot` (colored-wall courtyard, `--no-probes/
  --no-shadows/--no-rays`) with reference images `docs/images/g7_probes_{on,off,nofx}.png`
  (pixel-verified: sky unchanged, shadowed floor brighter, per-channel color bleeding).
  G7 tutorial en/de; USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI
  docs-screenshots renders them.
- **G8 — decals / projected textures** (`render::Decal` in
  `include/fuhglgraphics/render/decals.hpp`; decal pass in `src/render/deferred_renderer.cpp`,
  `kDecalVert/kDecalFrag` in `src/render/deferred_glsl.hpp`): oriented box decals projected
  into the G-buffer albedo + normal attachments between the geometry pass and the lighting
  pass. The fragment shader reconstructs the world position from the G-buffer depth, keeps
  pixels inside the box, gates on the G-buffer MaterialId (`materialIdMask`), projects onto
  the dominant face axis of the surface normal and mixes albedo/normal in. Sorted
  back-to-front, capped at `render::kMaxDecals` = 16 (one-time warning), after the MSAA
  resolve when MSAA is on. New `render::Effect::Decals` (kEffectCount = 12);
  `DeferredRenderer::render(..., decals)` trailing parameter. Pure `render::decalProject()`
  helper (unit-tested). G9: `scene::DecalComponent` -> `FrameData::decals`.
  Tests: `tests/test_render.cpp` (decalProject: containment, dominant-axis UV, rotated
  boxes) + `tests/gl_smoke_decals.cpp` (gl_context: mark appears only with the effect on,
  material-ID mask gates it, no GL error).
  New tool `examples/decals_screenshot` (plaza with bullet-hole decals on ground + wall and
  a red ring marking; `--no-decals/--no-shadows/--no-rays`) with reference images
  `docs/images/g8_decals_{on,off,nofx}.png` (pixel-verified: red marking present only with
  decals on). G8 tutorial en/de; USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI
  docs-screenshots renders them.
- **G10 — quick upgrades (roadmap 7)**, the remaining open items (MSAA wiring+test,
  CSM penumbra hardening and FSR1-style temporal upscaling already landed with G3/#4):
  - **DoF bokeh** (`render::Effect::Dof`, kEffectCount = 14): new post-graph pass "dof"
    between upscale and tonemap (`kDofFrag` in `src/render/deferred_glsl.hpp`,
    `DeferredRenderer::drawDof`) — CoC from the linearized G-buffer depth, hexagonal bokeh
    gather (6 full-ring + 6 half-ring taps); plain copy when off. Params
    `EffectParams::dofFocusDist/dofStrength/dofMaxCoC` via `Effects::applyTo()`.
  - **Mini debug HUD** (`render::DebugHud`, `include/fuhglgraphics/render/debug_hud.hpp`,
    `src/render/debug_hud.cpp`, PIMPL over FuhGLCore's TextRenderer with the embedded 8x8
    font): fps (FpsCounter), frame ms, per-effect on/off lines; `render::Effect::DebugHud`;
    drawn by `render()` after the post graph into `output()` (NOT as a graph pass —
    resolvePassOrder is LIFO for independent passes and would run it before tonemap);
    `renderer.hud().fps()` / `renderer.lastFrameMs()`.
  - **CI screenshot perceptual diff**: `docs/screenshot_diff.py` (pure python3, zlib +
    struct, no PIL) compares `docs/images/ci/*.png` against the checked-in references in
    `docs/images/` with a PSNR threshold and fails the `docs-screenshots` job below it.
  Tests: `tests/test_render.cpp` (kEffectCount = 14, effect names) +
  `tests/gl_smoke_g10.cpp` (gl_context: DoF on/off blurs the near crate, HUD changes the
  top-left pixels, hud created, no GL error).
  New tool `examples/dof_screenshot` (`--focus 9 --no-dof --hud ...`) with reference
  images `docs/images/g10_{dof_on,dof_off,hud}.png` (verified: foreground pillar blurred,
  background sharp; HUD panel readable top-left). G10 tutorial en/de;
  USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI renders the G10 images and runs
  the screenshot diff.
