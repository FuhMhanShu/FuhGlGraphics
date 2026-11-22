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

Next: **G11** (engine infrastructure) / **G2** (Qt editor) — see [REQUIREMENTS.md](../../REQUIREMENTS.md).

Prerequisite for all: build the library and link `FuhGlGraphics::fuhglgraphics`
(see [DEVELOPMENT.md](../../DEVELOPMENT.md#build-options)).
