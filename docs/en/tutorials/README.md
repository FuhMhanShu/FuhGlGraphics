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
- [G20 — Terrain + LOD](g20_terrain_lod.md) — `scene::TerrainComponent`: FuhGLCore F38/F41
  `terrainLods` LodMesh in the `fillFrame()` path (`DrawItem::lod` -> per-frame level from the
  camera distance, `SceneView::lastLodLevel`), `heightAt()` for object placement; screenshots
  in [docs/images/](../../images/g20_terrain_far.png)
- [G21 — Volumetric fog / clouds](g21_volumetric_fog.md) — `Effect::VolumetricFog`: "volumetric"
  post pass after particles, before bloom; raymarched 3D-noise volume (height falloff + fBm,
  Henyey–Greenstein phase) against the G-buffer depth, fogs geometry and sky; pure CPU twins in
  `render/volumetric.hpp`; screenshots in [docs/images/](../../images/g21_volumetric_on.png)
- [G22 — Wetness / rain on PBR](g22_wetness.md) — `Effect::Wetness`: lighting-pass
  roughness/albedo modulation with a noise puddle mask on up-facing surfaces + SSR boost; pure
  CPU twins in `render/wetness.hpp`; screenshots in [docs/images/](../../images/g22_wetness_wet.png)
- [G23 — Tonemapping operators](g23_tonemap.md) — `render::TonemapOperator` (ACES / Reinhard /
  Uncharted2 / linear) selected per frame via `uOperator` in the tonemap pass + pure CPU twins in
  `render/tonemap.hpp`; adjustable bloom threshold (`bloomThreshold`, G17); screenshots in
  [docs/images/](../../images/g23_tonemap_uncharted2.png)
- [G24 — Procedural noise (CPU)](g24_procedural_noise.md) — `procedural::Noise`: seeded value/Perlin/Simplex noise 2D+3D, fBm/ridged/turbulence, domain warp; height fields for terrain carving, sky cubemaps, volume fields; screenshots in [docs/images/](../../images/g24_noise_wide.png)
- [G25 — Marching cubes (voxel → mesh)](g25_marching_cubes.md) — `procedural::marchingCubes`/`marchingCubesChunked`: extract iso-surfaces from volumes (watertight, indexed), cave terrain from noise volumes; screenshots in [docs/images/](../../images/g25_marching_cubes_cave.png)
- [G26 — L-systems (turtle → mesh)](g26_lsystem.md) — `procedural::LSystem`: rule rewriting (`derive`) + 3D turtle (`generate`) → trees/ferns/bushes as `MeshData`; screenshots in [docs/images/](../../images/g26_lsystem_forest.png)
- [G27 — Wave function collapse](g27_wfc.md) — `procedural::Wfc`: edge-tag tiles + rotations, min-entropy + propagation + backtracking → dungeon layouts as scene nodes; screenshots in [docs/images/](../../images/g27_wfc_overview.png)
- [G28 — Voronoi diagrams](g28_voronoi.md) — `procedural::voronoi2d` + `extrudeCells` (cells → prism meshes), F1/F2 distance fields, shattered plate with glowing cracks + crystals; screenshots in [docs/images/](../../images/g28_voronoi_shattered.png)
- [G29 — SDF / 3D fractals (Mandelbulb)](g29_mandelbulb.md) — `procedural::sdf` + `meshFromSdf` (mesh any SDF via marching cubes), emissive fractal sculpture with bloom; screenshots in [docs/images/](../../images/g29_mandelbulb_wide.png)
- [G30 — Procedural creatures](g30_creature.md)
- [G31 — World generator helper (landscape or city)](g31_worldgen.md) — `procedural::generateWorld(WorldGenParams)`: one call builds a noise heightmap landscape with L-system trees or a WFC city with voronoi district plates; deterministic per seed, pure CPU; screenshots in [docs/images/](../../images/g31_worldgen_city.png) — `Skeleton` graph + `creatureSdf` (smooth-min capsules/ellipsoids + simplex detail) + `meshCreature` (marching cubes, gradient normals, curvature in the vertex color, cylindrical UVs) + `bakeCreatureMaps` (object-space noise albedo/normal maps); quadruped + reptile showcase; screenshots in [docs/images/](../../images/g30_creature_wide.png)
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
