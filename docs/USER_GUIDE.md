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
