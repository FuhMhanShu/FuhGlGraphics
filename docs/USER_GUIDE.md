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
  capability gating. Extras: `setSkyCubemap(&cube)` renders a real sky cubemap for
  background pixels (instead of the skyTop/skyBottom gradient), and `renderer.godRays`
  adds occlusion-tested god rays toward the sun in the tonemap pass (G-buffer depth as
  occluder, sun disk from the first directional light). Quick start:
  [G3 tutorial](en/tutorials/g3_deferred_renderer.md),
  result: [screenshot](images/g3_deferred_1280x720.png),
  landscape with cloud sky + sun + fog + god rays: [screenshot](images/g9_landscape_sky_sun_fog_rays.png).

  ```cpp
  fuhglgraphics::render::DeferredRenderer renderer;
  renderer.create(1280, 720, /*msaaSamples=*/0);   // needs a current GL >= 3.3 context
  renderer.enableCsmShadows(2048, 4);              // shadows via FuhGLCore CascadeShadowMap
  renderer.setShadowFilter(fuhgl::render::ShadowFilter::Pcss); // F45 shadow kit: pcf3x3|poisson|pcss|vsm
  // renderer.shadowSnapToTexels = true;            // texel-snapped cascade VPs (less swimming)
  // renderer.shadowCascadeBlend = 0.25f;           // cross-fade cascades near the splits
  // renderer.enablePointShadow(-1, 1024);          // cube shadow map for the first point light
  // renderer.contactShadowsEnabled = true;         // screen-space contact shadows (F46)
  renderer.setRenderScale(0.75f);                  // internal res 75%, FSR1-style upscale
  // per frame:
  renderer.render(scene);                          // fuhgl::render::SceneView
  // renderer.output() is the LDR frame; renderer.texMotion()/texMaterialId()/... the G-buffer
  ```
- **glTF import (G4).** `fuhglgraphics::io::loadGltf(path)` loads glTF 2.0 files (.gltf/.glb;
  core + `KHR_materials_pbrMetallicRoughness`, embedded base64 / external `.bin` buffers /
  GLB BIN chunk, PNG/JPEG decoded via Qt QImage when built `WITH_QT`) into plain data
  (`GltfAsset`: meshes as FuhGLCore `render::MeshData`, materials, images, nodes, scenes).
  `io::instantiateGltf(asset, scene, gpu)` maps the default scene onto the G9 scene graph
  with GL meshes/materials/textures (needs a current GL context). Errors are `bool` + log,
  never exceptions. Quick start: [G4 tutorial](en/tutorials/g4_gltf_loader.md), demo tool
  `examples/gltf_demo`, screenshots: [ship](images/g4_gltf_ship.png),
  [no shadows](images/g4_gltf_ship_no_shadows.png), [no IBL](images/g4_gltf_ship_no_ibl.png),
  [chest](images/g4_gltf_chest.png).

  ```cpp
  fuhglgraphics::io::GltfAsset asset;
  if (!fuhglgraphics::io::loadGltf("ship.glb", asset)) { /* err string, bool+log */ }
  fuhglgraphics::scene::Scene scene;
  fuhglgraphics::io::GltfGpuResources gpu;          // needs a current GL context
  fuhglgraphics::io::instantiateGltf(asset, scene, gpu);   // TRS of the glTF nodes preserved
  fuhglgraphics::scene::FrameData frame;
  scene.fillFrame(frame, nullptr, 1280, 720);
  frame.effects.applyTo(renderer);
  renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
  ```
- **Scene graph (G9).** On top of FuhGLCore's flat `DrawItem` list: `scene::Node` with local
  TRS + world transform, children and a polymorphic component list — `MeshComponent` (several
  `MeshEntry` meshes per node, each with its own local offset, material parameters, albedo +
  normal-map textures, `castShadow` flag and material ID), `LightComponent`, `CameraComponent`
  (decal / particle-emitter / probe components attach the same way once G8/G5/G7 land) — plus
  per-node `Effects` (a list of `render::Effect` + `EffectParams`, merged as a union over all
  nodes per frame, parameters of the last-visited node win) and an `instancing` property
  (grid/random layouts, one instanced draw call per node). `Scene::fillFrame()` updates world
  transforms + world AABBs, frustum-culls and fills a `FrameData` (SceneView + parallel
  `materialIds`/`castShadow` + merged `Effects`) for the G3 renderer. Quick start:
  [G9 tutorial](en/tutorials/g9_scene_graph.md), screenshots:
  [docs/images/](images/g9_scene_graph.png).

  ```cpp
  fuhglgraphics::scene::Scene scene;
  auto& mc = scene.root().createChild("hero").attach<fuhglgraphics::scene::MeshComponent>();
  auto& e = mc.addMesh(&mesh);                 // several meshes per node: mc.addMesh(&other)
  e.albedo = {0.9f, 0.4f, 0.2f}; e.albedoTexture = &tex; e.normalMap = &nrm;
  e.materialId = 7; e.castShadow = true;
  scene.root().effects.enable(fuhglgraphics::render::Effect::Taa);
  fuhglgraphics::scene::FrameData frame;
  scene.fillFrame(frame, nullptr, 1280, 720);  // camera from a CameraComponent
  frame.effects.applyTo(renderer);             // merged effects -> G3 DeferredRenderer
  renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
  ```
- **Particles (G5).** `particles::ParticleSystem` (PIMPL): emitter + integrate compute
  shaders over an SSBO on GL >= 4.3 (`system.gpu()`), CPU fallback (same math in `CpuSim`)
  below; rendered as instanced camera-facing billboards in the G3 renderer's "particles"
  post pass (additive/alpha blend, soft-particle depth fade against the G-buffer depth).
  As a G9 component: attach `scene::ParticleEmitterComponent` to a node — `fillFrame()`
  collects the systems into `FrameData::particleSystems`, pass them to
  `renderer.render(view, &materialIds, &castShadow, &frame.particleSystems)` (toggle:
  `render::Effect::Particles`). Quick start: [G5 tutorial](en/tutorials/g5_particles.md),
  demo `examples/particles_screenshot`, screenshots:
  [fire](images/g5_particles_fire.png), [smoke](images/g5_particles_smoke.png),
  [pollen](images/g5_particles_pollen.png).

  ```cpp
  auto& em = node.attach<fuhglgraphics::scene::ParticleEmitterComponent>();
  em.config.position = {0.f, 0.35f, 0.f}; em.config.rate = 350.f; // ...
  em.system.create(8192, em.config);              // needs a current GL context
  // per frame:
  em.system.update(dt);
  renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems);
  ```
- **Animation & skinning (G6).** `fuhglgraphics::anim` (new module, `anim/animation.hpp`):
  `AnimationClip` (TRS keyframe channels per node name, sampled with lerp + slerp),
  `AnimationPlayer` (`play(name, LoopMode::Once|Loop|PingPong, speed)`, `update(dt)`,
  `applyTo(scene)` writes the sampled TRS into the named nodes — call it before
  `Scene::fillFrame()`), and skeletal skinning on the CPU (the requirement allows
  "CPU or GPU"): `Skeleton` (joint nodes + inverse bind matrices,
  `updatePalette(jointNodePtrs)` → palette = jointWorld * inverseBind) plus
  `skinMeshCpu()` deforms a bind-pose mesh (+ `SkinWeights`) into a dynamic mesh per
  frame. The G4 glTF loader parses `animations[]`/`skins[]` (incl. JOINTS_0/WEIGHTS_0)
  and `io::instantiateGltf()` maps them onto `GltfGpuResources::{player, skeletons,
  nodePtrs, skinnedMeshes}`. G9 integration: `scene::AnimationComponent` holds a player.
  Quick start: [G6 tutorial](en/tutorials/g6_animation.md), demo
  `examples/animation_screenshot`, screenshots: [arm bent](images/g6_animation_bend.png),
  [arm straight](images/g6_animation_straight.png),
  [no effects](images/g6_animation_nofx.png).

  ```cpp
  fuhglgraphics::anim::AnimationPlayer player;
  player.addClip(clip);
  player.play("bend", fuhglgraphics::anim::LoopMode::PingPong);
  // per frame:
  player.update(dt);
  player.applyTo(scene);                    // TRS into the named nodes
  scene.fillFrame(frame, nullptr, 1280, 720);
  skeleton.updatePalette(jointNodePtrs);    // palette = jointWorld * inverseBind
  fuhglgraphics::anim::skinMeshCpu(base, weights, skeleton.palette, skinned);
  skinnedMesh.setMeshData(skinned);         // dynamic mesh, rendered by G3
  renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
  ```
- **Light probes (G7).** `fuhglgraphics::probes::LightProbeGrid`: a regular grid of SH (L2)
  irradiance probes over an AABB, baked on the CPU (`bake()` — fibonacci-sphere rays against
  a world-space triangle soup, one bounce: sky radiance + direct lights with shadow-ray
  occlusion) and sampled with hardware-trilinear interpolation in the G3 lighting pass
  (seven RGBA16F 3D textures on units 17–23, GL 3.3-capable). The diffuse ambient term is
  replaced by `albedo/π * E(n)` from the probes (IBL keeps its specular part) — color
  bleeding from colored walls into shadows. Toggle: `render::Effect::LightProbes`, scale:
  `EffectParams::lightProbeStrength`. As a G9 component: attach
  `scene::LightProbeComponent` (owns the grid) — `fillFrame()` puts it into
  `FrameData::lightProbes`, pass it to
  `renderer.render(view, &materialIds, &castShadow, &particleSystems, frame.lightProbes)`.
  Quick start: [G7 tutorial](en/tutorials/g7_light_probes.md), demo
  `examples/probes_screenshot`, screenshots: [probes on](images/g7_probes_on.png),
  [off](images/g7_probes_off.png), [no effects](images/g7_probes_nofx.png).

  ```cpp
  fuhglgraphics::probes::BakeInput bake;
  bake.triangles = /* world-space soup */; bake.lights = frame.view.lights;
  bake.skyRadiance = [](const fuhgl::math::Vec3& d) { return skyColor(d); };
  auto& pc = node.attach<fuhglgraphics::scene::LightProbeComponent>();
  pc.grid.create(9, 4, 9, {-11.5f, 0.05f, -11.5f}, {11.5f, 3.8f, 11.5f});
  pc.grid.bake(bake);                       // CPU; GL textures upload lazily in render()
  // per frame:
  scene.fillFrame(frame, nullptr, 1280, 720);   // frame.lightProbes = &pc.grid
  frame.effects.enable(fuhglgraphics::render::Effect::LightProbes);
  frame.effects.applyTo(renderer);
  renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                  frame.lightProbes);
  ```
- **Decals (G8).** `fuhglgraphics::render::Decal` (plain data): oriented box decals projected
  into the G-buffer's albedo + normal attachments by a dedicated pass between the G-buffer
  geometry pass and the lighting pass — the G-buffer MaterialId attachment gates which
  surfaces receive a decal (`materialIdMask`). Box projection onto the dominant face axis,
  optional albedo texture + normal map, `opacity`, sorted back-to-front, capped at
  `render::kMaxDecals` (16). Toggle: `render::Effect::Decals`. G9:
  `scene::DecalComponent` → `FrameData::decals` →
  `renderer.render(..., &frame.decals)`. Quick start: [G8 tutorial](en/tutorials/g8_decals.md),
  demo `examples/decals_screenshot`, screenshots: [on](images/g8_decals_on.png),
  [off](images/g8_decals_off.png), [no effects](images/g8_decals_nofx.png).
- **Quick upgrades (G10).** `render::Effect::Dof`: depth of field with a hexagonal bokeh
  kernel — a post pass between upscale and tonemap, CoC from the linearized G-buffer depth
  (`dofFocusDist` / `dofStrength` / `dofMaxCoC` in `EffectParams`). `render::Effect::DebugHud`:
  `render::DebugHud` (PIMPL over FuhGLCore's TextRenderer, embedded 8x8 font) draws fps,
  frame ms and the on/off state of every effect into the output after the post graph —
  `renderer.hud().fps()`, `renderer.lastFrameMs()`. CI: `docs/screenshot_diff.py` does a
  perceptual PSNR diff of the rendered `docs/images/ci/*.png` against the checked-in
  references in the `docs-screenshots` job. Quick start:
  [G10 tutorial](en/tutorials/g10_upgrades.md), demo `examples/dof_screenshot`,
  screenshots: [DoF on](images/g10_dof_on.png), [off](images/g10_dof_off.png),
  [HUD](images/g10_hud.png).
- **Water (G13).** `water::WaterSurface` — a Gerstner-wave grid mesh with shading
  parameters (colors, fresnel, reflection strength, crest/shore foam, glint), animated via
  `surface.update(dt)`. As a G9 component (`scene::WaterComponent` → `FrameData::water`),
  rendered by the G3 renderer with `render::Effect::Water`: a planar-reflection pass
  (mirrored camera, clipped at the water plane) into `renderer.reflectionTexture()`, then a
  "water" post pass in the HDR chain (waves + fresnel mix of reflection/refraction + foam +
  sun glint + fog). Quick start: [G13 tutorial](en/tutorials/g13_water.md), demo
  `examples/water_screenshot`, screenshots: [lake](images/g13_water_1280x720.png),
  [close-up](images/g13_water_closeup.png),
  [no reflection/foam](images/g13_water_nofx.png).

  ```cpp
  auto& wc = waterNode.attach<fuhglgraphics::scene::WaterComponent>();
  wc.surface.create(160, 220.f);        // needs a current GL context; node translation = water level
  wc.surface.setDefaultWaves();          // or tune wc.surface.waves / colors / foam yourself
  // per frame:
  wc.surface.update(dt);                 // advance the waves
  scene.fillFrame(frame, nullptr, 1280, 720);   // frame.water = &wc.surface
  frame.effects.enable(fuhglgraphics::render::Effect::Water);
  frame.effects.applyTo(renderer);
  renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                  frame.lightProbes, &frame.decals, frame.water);
  ```
- **Picking (G18).** `scene/picking.hpp` (pure CPU, no GL): `scene::rayFromScreen()` builds
  the world ray through a pixel, `rayIntersectAabb()` / `rayIntersectTriangle()`
  (Möller–Trumbore) do the intersection math, and `scene::pick(scene, camera, w, h, px, py,
  hit)` returns the nearest `PickHit` {node, meshEntry, distance, point, triangle} — node
  world AABBs cull subtrees, triangles come from `MeshEntry::meshData` (set by the glTF
  loader for every primitive). Basis for editor selection/gizmos. Quick start:
  [G18 tutorial](en/tutorials/g18_picking.md), demo `examples/picking_screenshot`,
  screenshots: [marker at hit](images/g18_picking_marker.png).
- **Scene serialization (G19).** `scene/serialize.hpp`: `sceneToJson()`/`sceneFromJson()` for an
  in-memory Scene <-> `io::Json` round-trip and `saveScene()`/`loadScene()` for .json files
  (format `{"format":"fuhglgraphics-scene","version":1,"root":{node}}`: node TRS, effects +
  params, instancing, mesh/light/decal/animation components). GL meshes are stored as JSON mesh
  specs and re-created through a `MeshResolver` into a mesh arena — `defaultShapeResolver`
  handles `{"shape":"box"|"quad",...}`; `io::writeJson()`/`writeJsonFile()` are the JSON
  pretty-printer. Quick start: [G19 tutorial](en/tutorials/g19_serialization.md), demo
  `examples/serialization_screenshot`, screenshots: [original](images/g19_serialization_original.png),
  [loaded from JSON](images/g19_serialization_loaded.png).
- **Terrain + LOD (G20).** `scene::TerrainComponent` (needs a GL context for `create()`):
  builds a procedural heightmap + `levels` LOD grids (`terrainLods`, switch distances from
  `LodMesh::defaultDistances` or `customDistances`), `heightAt(x, z)` places objects on the
  terrain. `fillFrame()` emits one draw item with `lod` set — the renderer picks the level per
  frame from the camera distance (`resolveLodMesh`) and reports it in `view.lastLodLevel`;
  `view.lodEnabled = false` forces level 0. Any `MeshEntry::lod` works the same way. Quick
  start: [G20 tutorial](en/tutorials/g20_terrain_lod.md), demo `examples/terrain_screenshot`,
  screenshots: [near, LOD 0](images/g20_terrain_near.png), [far, LOD 3](images/g20_terrain_far.png).
- **Bloom (G17).** `render::Effect::Bloom` runs a post-graph pass `bloom` after particles and
  before TAA: per-channel bright-pass above `bloomThreshold`, 9-tap golden-angle blur at
  `bloomRadius` texels, added with `bloomStrength` (input `hdrP`, output `hdrB`, so TAA
  stabilizes the halo). Params in `EffectParams`: `bloomThreshold`, `bloomStrength`,
  `bloomRadius`; pure helpers `bloomBrightPass()` / `bloomTaps()` in `render/bloom.hpp`. Quick
  start: [G17 tutorial](en/tutorials/g17_bloom.md), demo `examples/bloom_screenshot`,
  screenshots: [on](images/g17_bloom_on.png), [off](images/g17_bloom_off.png),
  [threshold 4](images/g17_bloom_thr4.png).
- **Screen-space reflections (G16).** `render::Effect::Ssr` runs a post-graph pass `ssr`
  after lighting: the reflection vector is marched against the G-buffer depth and the hit HDR
  color is added with a fresnel/roughness/edge/distance mask (pure helpers `ssrEdgeFade()` /
  `ssrMask()` in `render/ssr.hpp`). The pass writes `hdrS`, which the water/particles passes
  consume. Params in `EffectParams`: `ssrMaxSteps`, `ssrThickness`, `ssrMaxDist`,
  `ssrStrength`; `renderer.ssrWetnessBoost` adds wetness on top (G22). Quick start:
  [G16 tutorial](en/tutorials/g16_ssr.md), demo `examples/ssr_screenshot`,
  screenshots: [on](images/g16_ssr_on.png), [off](images/g16_ssr_off.png).
- **SSAO (G15).** `render::Effect::Ssao` runs a post-graph pass `ssao` before lighting:
  hemisphere-kernel sampling of the G-buffer (normal + depth, world-space reconstruction,
  per-pixel kernel rotation, linearized-depth compare with range check) into the `ssao`
  resource; the lighting pass multiplies the material AO by the factor on unit 6
  (`renderer.texSsao()` for inspection). Params in `EffectParams`: `ssaoRadius`,
  `ssaoIntensity`, `ssaoBias`, `ssaoSamples`. Pure helpers `render::ssaoKernel()` /
  `ssaoRotation()` / `linearizeDepth()` in `render/ssao.hpp`. Quick start:
  [G15 tutorial](en/tutorials/g15_ssao.md), demo `examples/ssao_screenshot`,
  screenshots: [on](images/g15_ssao_on.png), [off](images/g15_ssao_off.png).
- **GPU skinning (G14).** `anim::GpuSkinner` owns one skinned mesh's GPU side: VAO with the
  bind-pose vertices plus `JOINTS_0`/`WEIGHTS_0` as float4 attributes (locations 5/6) and an
  SSBO with the joint matrix palette (binding 0). On GL >= 4.3 the G3 G-buffer pass draws it
  with a GLSL-430 skinning vertex shader when `render::Effect::GpuSkinning` is enabled;
  below 4.3 / with the effect off the plain mesh is drawn (CPU skinning via
  `anim::skinMeshCpu`, G6). Wire it via `MeshEntry::gpuSkin` (-> `FrameData::skins`,
  parallel to `view.items`) and call `skinner.uploadPalette(skeleton.palette)` per frame
  after `Skeleton::updatePalette()`; pass `&frame.skins` as the last `render(...)` argument.
  glTF assets get skinners automatically in `io::instantiateGltf()` on GL >= 4.3. Quick
  start: [G14 tutorial](en/tutorials/g14_gpu_skinning.md), demo
  `examples/gpu_skinning_screenshot`, screenshots: [GPU on](images/g14_gpu_skinning_on.png),
  [CPU fallback](images/g14_gpu_skinning_off.png).
  hardening, temporal upscaling, bokeh DoF, screenshot diffs in CI, debug HUD.
- **Infrastructure (G11).** On-disk asset cache keyed by content hash, JSON config system
  (same minimal in-repo JSON parser as the glTF loader), minimal Qt editor.

## Error handling

Like FuhGLCore: no exceptions for domain errors — fallible APIs return `bool` plus an
info-log string.

## Platforms

Windows (WGL), Linux (X11 + EGL), macOS (Cocoa) — inherited from FuhGLCore. GL 3.3 core
baseline; GL 4.3 gates GPU particles/GPU skinning; macOS caps at GL 4.1.
