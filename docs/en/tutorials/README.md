# FuhGlGraphics Tutorials

> 🇩🇪 **Deutsche Version:** [docs/de/tutorials/README.md](../../de/tutorials/README.md)

One small tutorial per feature. Each is self-contained: goal → code → result.

- [G3 — Deferred renderer](g3_deferred_renderer.md) — extended G-buffer + PBR lighting +
  post chain (TAA → FSR1-style upscale → ACES tonemap), with screenshots in
  [docs/images/](../../images/g3_deferred_1280x720.png)
- [G9 — Scene graph](g9_scene_graph.md) — nodes with components (several meshes per node,
  albedo + normal-map textures per mesh, lights, camera), per-node effects + instancing,
  `Scene::fillFrame()` → G3 renderer, with effect-comparison screenshots in
  [docs/images/](../../images/g9_scene_graph.png)
- [G4 — glTF 2.0 loader](g4_gltf_loader.md) — `io::loadGltf()` + `io::instantiateGltf()` onto
  the G9 scene graph, demo asset (Kenney, CC0) + `examples/gltf_demo`, screenshots in
  [docs/images/](../../images/g4_gltf_ship.png)
- [G5 — GPU particles](g5_particles.md) — `particles::ParticleSystem` (compute on
  GL >= 4.3, CPU fallback) as a G9 `ParticleEmitterComponent`, rendered in the G3
  "particles" post pass; campfire/smoke/pollen screenshots in
  [docs/images/](../../images/g5_particles_fire.png)
- [G6 — Animation & skinning](g6_animation.md) — `anim::AnimationPlayer` (TRS clips,
  Once/Loop/PingPong) + `anim::Skeleton` with CPU skinning (`skinMeshCpu`), glTF
  animations/skins via the G4 loader, G9 `AnimationComponent`; skinned robot-arm +
  waving-flag screenshots in [docs/images/](../../images/g6_animation_bend.png)
- [G7 — Light probes (SH irradiance GI)](g7_light_probes.md) — `probes::LightProbeGrid`
  (CPU bake, 3D-texture trilinear sampling in the G3 lighting pass) as a G9
  `LightProbeComponent`; color bleeding from colored walls into shadows, screenshots in
  [docs/images/](../../images/g7_probes_on.png)
- [G8 — Deferred decals](g8_decals.md) — `render::Decal` boxes projected into the G-buffer
  (albedo + normal), material-ID gated, as a G9 `DecalComponent`; bullet holes + markings
  screenshots in [docs/images/](../../images/g8_decals_on.png)
- [G10 — Quick upgrades](g10_upgrades.md) — DoF with a hexagonal bokeh kernel
  (`Effect::Dof`), mini debug HUD on FuhGLCore's TextRenderer (`Effect::DebugHud`), CI
  perceptual screenshot diff (`docs/screenshot_diff.py`); screenshots in
  [docs/images/](../../images/g10_dof_on.png)

- [G13 — Realistic water](g13_water.md) — `water::WaterSurface` (Gerstner waves) as a G9
  `WaterComponent` → `FrameData::water` → G3 `render(..., &decals, water)` with `Effect::Water`:
  planar-reflection pass (mirrored camera) + water post pass (reflection/refraction
  fresnel mix, crest + shore foam, sun glint); lake screenshots in
  [docs/images/](../../images/g13_water_1280x720.png)

- [G18 — Picking / ray-casting](g18_picking.md) — `scene::pick()`: camera ray through a
  pixel + AABB culling + Möller–Trumbore triangle tests on `MeshEntry::meshData`; marker at
  the hit point; screenshots in [docs/images/](../../images/g18_picking_marker.png)
- [G19 — Scene serialization](g19_serialization.md) — `scene/serialize.hpp`: Scene <->
  `io::Json` round-trip (`io::writeJson` pretty-printer, mesh specs via `MeshResolver`,
  `saveScene`/`loadScene` for .json files); original vs. loaded-from-file renders match;
  screenshots in [docs/images/](../../images/g19_serialization_loaded.png)
- [G17 — Bloom](g17_bloom.md) — post pass after particles, before TAA: bright-pass above
  an adjustable `bloomThreshold` + 9-tap blur + add (`hdrP` -> `hdrB`), `Effect::Bloom`;
  screenshots in [docs/images/](../../images/g17_bloom_on.png)
- [G16 — Screen-space reflections](g16_ssr.md) — post pass after lighting raymarching the
  G-buffer depth (fresnel/roughness/edge mask), output `hdrS`; `Effect::Ssr`; screenshots in
  [docs/images/](../../images/g16_ssr_on.png)
- [G15 — SSAO](g15_ssao.md) — post pass before lighting from the G-buffer (hemisphere
  kernel + depth compare into an `ssao` resource, sampled by the lighting pass,
  `Effect::Ssao`); screenshots in [docs/images/](../../images/g15_ssao_on.png)
- [G14 — GPU skinning (GL ≥ 4.3)](g14_gpu_skinning.md) — `anim::GpuSkinner` (VAO +
  JOINTS/WEIGHTS attributes + matrix-palette SSBO) drawn by the G3 G-buffer pass's skinning
  vertex shader when `Effect::GpuSkinning` is on (CPU fallback below); screenshots in
  [docs/images/](../../images/g14_gpu_skinning_on.png)

Next: **G19–G23** (technical extensions: scene serialization,
terrain/LOD, volumetric fog, wetness, tonemap operators) / **G11** (engine infrastructure) /
**G2** (Qt editor) — see [REQUIREMENTS.md](../../REQUIREMENTS.md).

Prerequisite for all: build the library and link `FuhGlGraphics::fuhglgraphics`
(see [DEVELOPMENT.md](../../DEVELOPMENT.md#build-options)).
