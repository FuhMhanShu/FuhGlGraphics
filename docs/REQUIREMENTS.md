# FuhGlGraphics — Requirements

Status: final (agreed with maintainer). Language of this document: English.
Code, comments and API docs are English.

## 1. Goal

FuhGlGraphics is a cross-platform 2D/3D graphics framework with an asset
importer, implemented as an application layer **on top of FuhGLCore**. Direct
dependencies: FuhGLCore (G1) and Qt6 for editors/tools (G2). On top of that it
adds a new deferred renderer (G3) plus the roadmap items 1–8 (G4–G11). It does
**not** implement its own GL loading, windowing or math — those come from
FuhGLCore.

## 2. Non-goals

- No own GL loader, window system or math library — all of that comes from
  FuhGLCore.
- No audio, physics or networking.
- No vendored third-party sample assets (the glTF format itself is
  royalty-free, but most sample models are CC-BY → own generated fixtures
  instead; the JSON parser has no external dependency).
- The Qt editor stays minimal (viewport, scene tree, inspector, asset browser)
  — not an IDE. The library core remains Qt-free
  (`-DFUHGLGRAPHICS_WITH_QT=OFF` for headless use).

## 3. Functional requirements

| ID | Requirement | Details |
|----|-------------|---------|
| G1 | Direct FuhGLCore dependency | CMake resolution order: -DFUHGLGRAPHICS_FUHGLCORE_DIR=<path> (add_subdirectory) → find_package(FuhGLCore) → FetchContent fallback from github.com/FuhMhanShu/FuhGLCore (FUHGLGRAPHICS_FUHGLCORE_GIT_TAG, default main); link FuhGLCore::fuhglcore PUBLIC; installed config re-resolves via find_dependency |
| G2 | Qt6 direct dependency (editors/tools) | option FUHGLGRAPHICS_WITH_QT=ON (default): find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets), AUTOMOC, define FUHGLGRAPHICS_WITH_QT=1; used by editor/ (QOpenGLWidget viewport, asset browser, inspector, scene tree, gizmos); core stays Qt-free, headless = OFF; CI: Linux with Qt, win/mac OFF |
| G3 | New deferred renderer | fuhglgraphics::render::DeferredRenderer on FuhGLCore (DeferredPipeline/RenderGraph/PostChain): extended G-buffer (albedo+normal, rough/metal/AO, emissive, motion vectors, material ID for decals), lighting reusing FuhGLCore lights/shadows/IBL, own post chain (TAA→upscaling→tonemap) with per-effect toggles; capability gating like FuhGLCore F39/F40 |
| G4 | glTF 2.0 asset loading (roadmap 1) | new io/ layer: fuhglgraphics::io::loadGltf(path) → list of FuhGLCore render::MeshData + materials (+nodes/skins for G6); glTF 2.0 core + textures + KHR_materials_pbrMetallicRoughness mapped onto FuhGLCore materials; accessors/bufferViews/buffers (embedded base64 data-URI + external .bin); images PNG/JPEG via Qt QImage when WITH_QT; samplers; primitives POSITION/NORMAL/UV/color/indices; own minimal JSON parser (no nlohmann); no vendored sample assets — own generated fixtures; composes with FuhGLCore F38 terrain + F41 LOD (loaded meshes as LOD stages; external models in showcase/examples instead of only procedural CSG); bool+log errors, no exceptions |
| G5 | GPU particle system (roadmap 2) | compute-based (GL 4.3, same pattern as FuhGLCore GpuCuller): emitter+integrate+render as ParticleSystem (PIMPL); SSBOs + instanced billboards/meshes; F39-gated ≥4.3 + toggle, CPU fallback; upgrades vol-fog/DoF shots (smoke, sparks, pollen) |
| G6 | Animation/skinning (roadmap 3) | skeletal animation (CPU or GPU skinning via SSBO/vertex texture ≥4.3) + simple keyframe AnimationPlayer (clips, time, loop/wrap, TRS channels); depends on G4 (glTF skins/animations); matrix palette in G3 with forward fallback |
| G7 | Light probes / lightmapping GI (roadmap 4) | next GI step after IBL (F29): SH-irradiance light-probe grid with shader interpolation (GL 3.3-capable) or CPU lightmap bake for terrain (F38); diffuse indirect light; toggleable |
| G8 | Decals / projected textures (roadmap 5) | deferred decals via G-buffer position+normal: decal box projects albedo/normal into G-buffer (material-ID/stencil from G3); footprints/bullet holes/markings on terrain; limited count, front-to-back, toggleable |
| G9 | Scene graph + components (roadmap 6) | FuhGLCore scene.hpp is a flat DrawItem list; add lightweight graph: Node with local/world transform, children, components (mesh, light, camera, decal, particle emitter, probe), optional LodMesh; world AABBs for culling (F34 at nodes); glTF node mapping (G4) |
| G10 | Smaller quickly-visible upgrades (roadmap 7) | MSAA wiring+test in G3 (DeferredRenderer has msaaSamples already), CSM contact hardening (penumbra), temporal upscaling (FSR1-style EASU+RCAS) after TAA, DoF bokeh shape instead of circular, CI screenshot perceptual diff (reference PNGs in docs/images/, PSNR/perceptual threshold), mini debug HUD (ImGui-like panel on FuhGLCore TextRenderer: fps, frame-time, effect toggles) |
| G11 | Engine infrastructure (roadmap 8) | on-disk asset cache keyed by content hash (imported meshes/textures + baked IBL/env persist); JSON config system for F40 effect toggles/window/renderer (same minimal JSON parser as G4); minimal Qt editor (G2): gizmo+camera+scene tree+viewport |
| G12 | Asset creator Qt tool | `tools/asset_creator/` executable `fuhglgraphics_asset_creator` (built with `FUHGLGRAPHICS_WITH_QT=ON` + new option `FUHGLGRAPHICS_BUILD_TOOLS`): create/load/edit assets with a data-structures panel (scene tree + inspector + asset browser), a viewport with editing tools on the G3 `DeferredRenderer`, and an LLM chat over MCP (stdio JSON-RPC 2.0, configurable server command). Project file = own `.fasset` JSON (`io::Json` + new `io::writeJson`, scene serialization in `scene/serialize.hpp`, MCP message helpers in `mcp/jsonrpc.hpp` — all Qt-free + unit tests). MVC, PIMPL, UI multilingual (English default, German first translation). Design: [docs/ASSET_CREATOR.md](ASSET_CREATOR.md) |
| G14 | GPU skinning (GL >= 4.3) | `anim::GpuSkinner` (PIMPL): VAO with bind-pose vertices + JOINTS_0/WEIGHTS_0 float4 attributes (locations 5/6) + SSBO matrix palette (binding 0); G3 G-buffer pass skinning vertex shader (GLSL 430, `render::Effect::GpuSkinning`, capability-gated >= 4.3) draws the skinner instead of the plain mesh; CPU fallback (`skinMeshCpu`, G6) below 4.3 / when off; `FrameData::skins` parallel to `view.items` via `MeshEntry::gpuSkin`; glTF `instantiateGltf()` creates skinners for skinned primitives on GL >= 4.3 |
| G15 | SSAO | post pass before lighting from the G-buffer (normal+depth), hemisphere kernel + depth compare into an R16F target sampled by the lighting pass (`Effect::Ssao`, unit 6) |
| G16 | Screen-space reflections | post pass after lighting: raymarch the reflection vector against the G-buffer depth, sample the HDR scene, add with fresnel/roughness mask (`Effect::Ssr`) |
| G17 | Bloom | bright-pass (adjustable threshold) + blur + add in the HDR chain before TAA (`Effect::Bloom`) |
| G18 | Picking / ray-casting on the scene graph | pure math ray (camera -> pixel), AABB + Möller–Trumbore triangle tests against `MeshEntry::meshData`, nearest hit (node, entry, distance, point) |
| G19 | Scene serialization | `scene/serialize.hpp`: Scene <-> `io::Json` round-trip (nodes TRS, effects, instancing, mesh/light/decal/animation components, mesh resolver callback for GL meshes) + `io::writeJson`; `saveScene`/`loadScene` (.json files) |
| G20 | Terrain + LOD integration | `scene::TerrainComponent` (FuhGLCore F38 `HeightmapTerrain` + F41 `terrainLods`, `DrawItem::lod` in `fillFrame()`), LOD level selection by camera distance |
| G21 | Volumetric fog / clouds | post pass: raymarched 3D-noise volume (height falloff, henyey-greenstein phase, scene depth as march end) applied to the HDR chain (`Effect::VolumetricFog`) |
| G22 | Wetness / rain on PBR | lighting-pass modulation: roughness/albedo down, puddle mask from a noise texture, SSR boost (`Effect::Wetness`) |
| G23 | Tonemapping operators + bloom threshold | `render::TonemapOperator` (ACES / Reinhard / Uncharted2 / linear) selected per frame in the tonemap pass + pure CPU twins; bloom threshold in G17 |
| G13 | Realistic water (sea/lake) | new `water/` module: `water::WaterSurface` (Gerstner-wave grid mesh + shading params) as a G9 `WaterComponent` -> `FrameData::water` -> G3 `DeferredRenderer::render(..., water)` with `render::Effect::Water`: planar reflection pass (mirrored camera + clip plane at the water plane into an RGBA16F target at `waterReflectionScale`), water post pass in the HDR chain (Gerstner displacement + analytic wave normal in the vertex shader, fresnel mix of reflection/refraction, depth-based shore foam + crest foam, sun glint, fog); GL 3.3-capable, toggleable |

## 4. Non-functional requirements

| ID | Requirement | Details |
|----|-------------|---------|
| N1 | Dependencies | FuhGLCore (direct, G1) + Qt6 (optional, opt-out via FUHGLGRAPHICS_WITH_QT=OFF, G2); otherwise only the C++17 standard library; the JSON parser and test fixtures live in-repo |
| N2 | Performance | no allocations in the render path; GPU particles/skinning at GL 4.3 with CPU fallback (G5/G6); the asset cache avoids re-imports (G11) |
| N3 | Code style | like FuhGLCore: C++17, compact code, English comments/Doxygen, PIMPL by default for GL/Qt resource classes, no exceptions for domain errors (bool + log), namespace fuhglgraphics |
| N4 | Build | CMake ≥ 3.20, static library fuhglgraphics, install + export for find_package(FuhGlGraphics); options FUHGLGRAPHICS_WITH_QT / FUHGLGRAPHICS_BUILD_TESTS / FUHGLGRAPHICS_ENABLE_HARDENING / FUHGLGRAPHICS_FUHGLCORE_DIR / FUHGLGRAPHICS_FUHGLCORE_GIT_TAG; plus qmake via FuhGlGraphics.pro |
| N5 | Tests | mini test runner (no framework), labels unit + gl_context (headless EGL smoke); per feature: unit test + gl_smoke + tutorial en/de + CI screenshot + REQUIREMENTS/USER_GUIDE/README/CHANGELOG entries |
| N6 | Documentation | README.md + README.de.md, docs/ with language picker, REQUIREMENTS / USER_GUIDE / DEVELOPMENT / Doxyfile, tutorials en+de, images/ |
| N7 | Licensing | MIT repo; glTF royalty-free; no CC-BY assets vendored |

## 5. Platforms / GL baseline

Same as FuhGLCore: OpenGL 3.3 core baseline (macOS ≤ 4.1); GL 4.3 gates G5 and
GPU skinning (G6); GL 4.5 DSA/bindless used opportunistically. Windows WGL /
Linux X11 + EGL / macOS Cocoa. The Qt6 editor runs on all three.

## 6. Implementation status

- **G1** — implemented (CMake/qmake dependency wiring, skeleton commit).
- **G3** — implemented: `fuhglgraphics::render::DeferredRenderer` (extended G-buffer with
  motion vectors + material ID, PBR lighting on FuhGLCore lights/shadows/IBL, post chain
  TAA → FSR1-style upscale → ACES tonemap on `render::RenderGraph`, per-effect toggles with
  F39/F40-style gating). See `include/fuhglgraphics/render/`, `src/render/`,
  `tests/gl_smoke_render.cpp`, `examples/screenshot`, `docs/images/` and the
  [G3 tutorial](en/tutorials/g3_deferred_renderer.md). Extensions: sky cubemap for
  background pixels (`setSkyCubemap`, unit 11) and occlusion-tested god rays in the
  tonemap pass (`renderer.godRays`, G-buffer depth as occluder) — demoed by
  `examples/landscape_screenshot` (`docs/images/g9_landscape_sky_sun_fog_rays.png`).
  Shadows run on the **FuhGLCore F45 shadow kit** (FuhGLCore >= `9fc991c`, PR #42):
  `ShadowMap`/`CascadeShadowMap` are thin wrappers over `ShadowTarget` there;
  `DeferredRenderer::setShadowFilter(ShadowFilter)` selects the sampling filter
  (legacy 3x3 PCF / poisson PCF / PCSS / VSM with RGBA32F moments attachment), the GLSL
  comes from `fuhgl/render/shadow_glsl.hpp` (`kSkFuncs`); cascade VPs are texel-snapped
  (`shadowSnapToTexels`, `snapLightVPToTexels`) with optional cross-fade at the splits
  (`shadowCascadeBlend`); `enablePointShadow()` adds a `CubeShadowMap` for one point light
  (Effect::PointShadows); `contactShadowsEnabled` raymarches the G-buffer depth in the
  lighting pass (Effect::ContactShadows). Sky cubemap moved to texture unit 16 (the kit
  occupies 11–15). New effects in `render::Effect`: `PointShadows`, `ContactShadows`;
  new `EffectParams` fields: `shadowFilter`, `shadowSnapToTexels`, `shadowCascadeBlend`,
  `shadowLightSize`, `shadowMinVariance`, `shadowNormalOffset`, `shadowPcfRadius`.
  Screenshots: `docs/images/g9_landscape_shadowkit_{pcss,vsm,pcf3x3,lamp}.png`.
- **G9** — implemented: `fuhglgraphics::scene::Scene` / `Node` / components
  (`include/fuhglgraphics/scene/`, `src/scene/`): local TRS + world matrices, children,
  polymorphic component list (`MeshComponent` with several `MeshEntry` meshes per node —
  each with local offset, material params, albedo/normal-map textures, `castShadow`,
  material ID; `LightComponent`; `CameraComponent`), per-node `Effects` (list of
  `render::Effect` + `EffectParams`, union-merged per frame, `applyTo(DeferredRenderer)`),
  per-node `instancing` property (grid/random), world AABBs + frustum culling in
  `Scene::fillFrame()` → `FrameData` → G3 `render()` (with per-item `castShadow`).
  G-buffer normal-map support added in G3's G-buffer pass (unit `kTextureUnitNormalMap`).
  See `tests/test_scene.cpp`, `tests/gl_smoke_scene.cpp`, `examples/scene_screenshot`,
  `docs/images/g9_*.png` and the [G9 tutorial](en/tutorials/g9_scene_graph.md).
- **G4 — glTF 2.0 asset loading** (`fuhglgraphics::io`, `include/fuhglgraphics/io/`,
  `src/io/`): own minimal JSON parser + base64 (`io/json.hpp`), `io::loadGltf(path)` parses
  .gltf (JSON) and .glb (binary container) — buffers (external .bin, embedded base64
  data-URIs, GLB BIN chunk), bufferViews + accessors (all component types, normalized,
  strided, sparse), primitives (TRIANGLES/STRIP/FAN; POSITION/NORMAL/TEXCOORD_0/COLOR_0 +
  indices → FuhGLCore `render::MeshData`), materials via
  `KHR_materials_pbrMetallicRoughness` (factors + baseColor/normal textures, alphaMode,
  doubleSided), images (external/embedded, PNG/JPEG decoded to RGBA8 via Qt QImage when
  `WITH_QT`, otherwise factors only), samplers, nodes (TRS + matrix decomposed to TRS),
  scenes. `io::instantiateGltf(asset, scene, gpu, parent)` maps the default scene onto the
  G9 scene graph with GL resources (one `render::Mesh` per primitive, preset materials with
  albedo/normal textures, one `MeshEntry` per primitive, `castShadow = true`, running
  material IDs). No vendored third-party assets — tests use generated fixtures
  (`tests/gltf_fixture.hpp`); demo asset is Kenney CC0 (see the
  [G4 tutorial](en/tutorials/g4_gltf_loader.md)). See `tests/test_io.cpp`,
  `tests/gl_smoke_gltf.cpp`, `examples/gltf_demo`, `docs/images/g4_*.png` and the
  [G4 tutorial](en/tutorials/g4_gltf_loader.md).
- **G5 — GPU particle system** (`fuhglgraphics::particles`, `include/fuhglgraphics/particles/`,
  `src/particles/`): `ParticleSystem` (PIMPL) with emitter + integrate compute shaders over
  an SSBO (GL >= 4.3, same pattern as FuhGLCore's GpuCuller — the SSBO is simultaneously the
  divisor-1 instance-attribute buffer) rendered as instanced camera-facing billboards in the
  G3 renderer's "particles" post pass (additive/alpha blend, soft-particle depth fade vs the
  G-buffer depth); on GL 3.3–4.2 the identical math runs on the CPU (`CpuSim`, unit-tested)
  and is streamed as instance data — F39-style fallback, toggle `render::Effect::Particles`.
  G9 integration: `scene::ParticleEmitterComponent` → `FrameData::particleSystems` →
  `DeferredRenderer::render(..., &particleSystems)`. See `tests/test_particles.cpp`,
  `tests/gl_smoke_particles.cpp`, `examples/particles_screenshot`,
  `docs/images/g5_particles_*.png` and the [G5 tutorial](en/tutorials/g5_particles.md).
- **G6 — animation/skinning** (`fuhglgraphics::anim`, `include/fuhglgraphics/anim/`,
  `src/anim/`): `AnimationClip`/`TrsChannel` (keyframe channels per node name, lerp +
  slerp sampling via `fuhgl::math::slerp`), `AnimationPlayer`
  (`play(name, LoopMode::Once|Loop|PingPong, speed)`, `update(dt)`, `applyTo(scene)` /
  node-map overload — writes TRS into the named nodes before `fillFrame()`), `Skeleton`
  (joint nodes + inverse bind matrices, `updatePalette()` → palette = jointWorld * IBM)
  and CPU skinning `skinMeshCpu()` (the requirement allows "CPU or GPU" — the CPU path
  is implemented; a GPU matrix-palette SSBO in the G3 G-buffer pass on GL >= 4.3 is future
  work). G4 extension: glTF `animations[]`/`skins[]` (incl. JOINTS_0/WEIGHTS_0) parsed into
  `GltfAsset::{animations, skins}` and mapped by `instantiateGltf()` onto
  `GltfGpuResources::{player, skeletons, nodePtrs, skinnedMeshes}`. G9:
  `scene::AnimationComponent` holds a player. See `tests/test_anim.cpp`,
  `tests/gl_smoke_anim.cpp` (fixture .glb with clip+skin; rendered frames at t=0 vs t=1
  differ), `examples/animation_screenshot`, `docs/images/g6_animation_*.png` and the
  [G6 tutorial](en/tutorials/g6_animation.md).
- **G7 — light probes / SH irradiance GI** (`fuhglgraphics::probes`,
  `include/fuhglgraphics/probes/light_probes.hpp`, `src/probes/light_probes.cpp`):
  `LightProbeGrid` — a regular grid of SH L2 irradiance probes over an AABB
  (GL 3.3-capable: seven RGBA16F 3D textures on units 17–23, hardware-trilinear
  interpolation in the G3 lighting pass). CPU bake (`bake()`): fibonacci-sphere rays
  against a world-space triangle soup, one bounce (sky radiance via a
  `skyRadiance(dir)` callback + direct lights with shadow-ray occlusion), projected onto
  SH L2 (`shProject`/`shBasis`/`shIrradiance`, pure + unit-tested). The lighting pass
  replaces the diffuse ambient with `albedo/π * E(n)` from the probes when
  `Effect::LightProbes` is on (IBL keeps its specular part; `lightProbeStrength` scales
  it) — diffuse indirect light / color bleeding. G9: `scene::LightProbeComponent` →
  `FrameData::lightProbes` → `DeferredRenderer::render(..., lightProbes)` (lazy texture
  upload in render). See `tests/test_probes.cpp`, `tests/gl_smoke_probes.cpp`,
  `examples/probes_screenshot`, `docs/images/g7_probes_*.png` and the
  [G7 tutorial](en/tutorials/g7_light_probes.md).
- **G8 — decals / projected textures** (`render::Decal` in
  `include/fuhglgraphics/render/decals.hpp`, decal pass in `src/render/deferred_renderer.cpp`
  + `kDecalVert/kDecalFrag` in `src/render/deferred_glsl.hpp`): oriented box decals
  projected into the G-buffer albedo + normal attachments between the geometry pass and
  the lighting pass — the MaterialId attachment gates the target surfaces
  (`materialIdMask`, "material-ID/stencil from G3"); box projection onto the dominant face
  axis of the surface normal, optional albedo texture + normal map, `opacity`, sorted
  back-to-front, limited to `render::kMaxDecals` = 16 (farthest dropped with a one-time
  warning); with MSAA the pass runs after the resolve. New `render::Effect::Decals`
  (kEffectCount = 12). Pure projection helper `render::decalProject()` (unit-tested).
  G9: `scene::DecalComponent` → `FrameData::decals` →
  `DeferredRenderer::render(..., &decals)`. See `tests/test_render.cpp` (decalProject),
  `tests/gl_smoke_decals.cpp` (mark appears only when enabled, material-ID mask gates it),
  `examples/decals_screenshot`, `docs/images/g8_decals_*.png` and the
  [G8 tutorial](en/tutorials/g8_decals.md).
- **G10 — quick upgrades (roadmap 7)** — implemented: MSAA wiring+test in G3
  (`create(w, h, msaaSamples)`, MSAA G-buffer + resolve, gl_smoke_render), CSM contact
  hardening via the F45 shadow kit (PCSS penumbra, texel snap, cascade blend — PR #4),
  temporal upscaling (FSR1-style EASU+RCAS after TAA — G3), and now: **DoF bokeh**
  (`Effect::Dof`, post pass between upscale and tonemap, hexagonal bokeh gather, CoC from
  the G-buffer depth; `EffectParams::dofFocusDist/dofStrength/dofMaxCoC`), **mini debug
  HUD** (`render::DebugHud` on FuhGLCore's TextRenderer — fps, frame ms, per-effect
  on/off; `Effect::DebugHud`, drawn after the post graph; `hud().fps()` /
  `lastFrameMs()`), **CI screenshot perceptual diff** (`docs/screenshot_diff.py`, pure
  python3, PSNR threshold, run in the `docs-screenshots` job over `docs/images/ci/` vs
  `docs/images/`). kEffectCount = 14. See `tests/gl_smoke_g10.cpp`,
  `examples/dof_screenshot`, `docs/images/g10_*.png` and the
  [G10 tutorial](en/tutorials/g10_upgrades.md).
- G2, G11 — planned.
- **G13 — water rendering** (`fuhglgraphics::water`, `include/fuhglgraphics/water/`, `src/water/`, G3 renderer extension in `src/render/`): implemented — see `tests/test_water.cpp`, `tests/gl_smoke_water.cpp`, `examples/water_screenshot`, `docs/images/g13_water_*.png` and the [G13 tutorial](en/tutorials/g13_water.md).
- **G14 — GPU skinning** (`fuhglgraphics::anim::GpuSkinner`, `include/fuhglgraphics/anim/gpu_skinner.hpp`,
  `src/anim/gpu_skinner.cpp`): implemented — see `tests/gl_smoke_gpu_skin.cpp`,
  `examples/gpu_skinning_screenshot`, `docs/images/g14_gpu_skinning_*.png` and the
  [G14 tutorial](en/tutorials/g14_gpu_skinning.md).
- **G15 — SSAO** (`fuhglgraphics::render::ssao` pure helpers in `include/fuhglgraphics/render/ssao.hpp`
  + `src/render/ssao.cpp`, "ssao" post-graph pass + `Effect::Ssao` in the G3 renderer, AO
  sampled by the lighting pass on unit 6): implemented — see `tests/test_ssao.cpp`,
  `tests/gl_smoke_ssao.cpp`, `examples/ssao_screenshot`, `docs/images/g15_ssao_*.png` and the
  [G15 tutorial](en/tutorials/g15_ssao.md).
- G2, G11 — planned.
- **G12 — asset creator Qt tool** — planned; design agreed in
  [docs/ASSET_CREATOR.md](ASSET_CREATOR.md) (`tools/asset_creator/`, MVC, `.fasset` JSON
  project files, MCP stdio chat). Implementation in phases P1–P5 after review.
