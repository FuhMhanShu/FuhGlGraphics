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
- G2, G5–G8, G10, G11 — planned.
