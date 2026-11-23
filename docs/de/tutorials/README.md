# FuhGlGraphics Tutorials (Deutsch)

> 🇬🇧 **English version:** [docs/en/tutorials/README.md](../../en/tutorials/README.md)

Ein kleines Tutorial pro Feature. Jedes ist in sich abgeschlossen: Ziel → Code → Ergebnis.

- [G3 — Deferred-Renderer](g3_deferred_renderer.md) — erweiterter G-Buffer + PBR-Lighting +
  Post-Chain (TAA → FSR1-artiges Upscaling → ACES-Tonemapping), mit Screenshots in
  [docs/images/](../../images/g3_deferred_1280x720.png)
- [G9 — Szenengraph](g9_scene_graph.md) — Nodes mit Komponenten (mehrere Meshes pro Node,
  Albedo- + Normalmap-Texturen pro Mesh, Lichter, Kamera), pro Node Effekte + Instancing,
  `Scene::fillFrame()` → G3-Renderer, mit Effekt-Vergleichsbildern in
  [docs/images/](../../images/g9_scene_graph.png)
- [G4 — glTF-2.0-Loader](g4_gltf_loader.md) — `io::loadGltf()` + `io::instantiateGltf()` auf
  den G9-Szenengraphen, Demo-Asset (Kenney, CC0) + `examples/gltf_demo`, Bilder in
  [docs/images/](../../images/g4_gltf_ship.png)
- [G5 — GPU-Partikel](g5_particles.md) — `particles::ParticleSystem` (Compute ab GL 4.3,
  CPU-Fallback) als G9-`ParticleEmitterComponent`, gerendert im G3-"particles"-Post-Pass;
  Campfire/Rauch/Pollen-Bilder in [docs/images/](../../images/g5_particles_fire.png)
- [G6 — Animation & Skinning](g6_animation.md) — `anim::AnimationPlayer` (TRS-Clips,
  Once/Loop/PingPong) + `anim::Skeleton` mit CPU-Skinning (`skinMeshCpu`), glTF-Animationen/
  Skins über den G4-Loader, G9-`AnimationComponent`; geskinnter Roboterarm + winkende
  Flagge in [docs/images/](../../images/g6_animation_bend.png)
- [G7 — Light-Probes (SH-Irradiance-GI)](g7_light_probes.md) — `probes::LightProbeGrid`
  (CPU-Bake, 3D-Textur-Trilinear-Sampling in der G3-Lighting-Pass) als G9-
  `LightProbeComponent`; Color-Bleeding von farbigen Wänden in die Schatten, Bilder in
  [docs/images/](../../images/g7_probes_on.png)
- [G8 — Deferred-Decals](g8_decals.md) — `render::Decal`-Boxen in den G-Buffer projiziert
  (Albedo + Normal), Material-ID-gegated, als G9-`DecalComponent`; Kugellöcher +
  Markierungen in [docs/images/](../../images/g8_decals_on.png)
- [G10 — Quick-Upgrades](g10_upgrades.md) — DoF mit hexagonalem Bokeh-Kernel
  (`Effect::Dof`), Mini-Debug-HUD auf FuhGLCore's TextRenderer (`Effect::DebugHud`),
  perzeptueller CI-Screenshot-Diff (`docs/screenshot_diff.py`); Bilder in
  [docs/images/](../../images/g10_dof_on.png)

- [G13 — Realistisches Wasser](g13_water.md) — `water::WaterSurface` (Gerstner-Wellen) als
  G9-`WaterComponent` → `FrameData::water` → G3-`render(..., &decals, water)` mit `Effect::Water`:
  planarer Reflexions-Pass (gespiegelte Kamera) + Wasser-Post-Pass (Reflexion/Refraktion
  per Fresnel, Kamm- + Uferschaum, Sonnen-Glint); See-Bilder in
  [docs/images/](../../images/g13_water_1280x720.png)

- [G18 — Picking / Ray-Casting](g18_picking.md) — `scene::pick()`: Kamera-Strahl durch ein
  Pixel + AABB-Culling + Möller–Trumbore-Dreieckstests auf `MeshEntry::meshData`; Marker am
  Trefferpunkt; Bilder in [docs/images/](../../images/g18_picking_marker.png)
- [G19 — Szenen-Serialisierung](g19_serialization.md) — `scene/serialize.hpp`: Scene <->
  `io::Json`-Round-Trip (`io::writeJson`-Pretty-Printer, Mesh-Specs über `MeshResolver`,
  `saveScene`/`loadScene` für .json-Dateien); Original- vs. aus-Datei-geladen-Render stimmen
  überein; Bilder in [docs/images/](../../images/g19_serialization_loaded.png)
- [G20 — Terrain + LOD](g20_terrain_lod.md) — `scene::TerrainComponent`: FuhGLCore-F38/F41
  `terrainLods`-LodMesh im `fillFrame()`-Pfad (`DrawItem::lod` -> pro Frame Level aus der
  Kamera-Distanz, `SceneView::lastLodLevel`), `heightAt()` zum Platzieren von Objekten; Bilder
  in [docs/images/](../../images/g20_terrain_far.png)
- [G21 — Volumetrischer Nebel / Wolken](g21_volumetric_fog.md) — `Effect::VolumetricFog`:
  "volumetric"-Post-Pass nach den Partikeln, vor Bloom; rayge-marchtes 3D-Noise-Volumen
  (Höhen-Falloff + fBm, Henyey–Greenstein-Phase) gegen die G-Buffer-Tiefe, foggt Geometrie und
  Himmel; reine CPU-Zwillinge in `render/volumetric.hpp`; Bilder in
  [docs/images/](../../images/g21_volumetric_on.png)
- [G22 — Wetness / Regen auf PBR](g22_wetness.md) — `Effect::Wetness`: Roughness/Albedo-
  Modulation im Lighting-Pass mit Noise-Pfützen-Maske auf nach oben zeigenden Flächen + SSR-Boost;
  reine CPU-Zwillinge in `render/wetness.hpp`; Bilder in
  [docs/images/](../../images/g22_wetness_wet.png)
- [G23 — Tonemapping-Operatoren](g23_tonemap.md) — `render::TonemapOperator` (ACES / Reinhard /
  Uncharted2 / linear) pro Frame über `uOperator` im Tonemap-Pass + reine CPU-Zwillinge in
  `render/tonemap.hpp`; einstellbarer Bloom-Threshold (`bloomThreshold`, G17); Bilder in
  [docs/images/](../../images/g23_tonemap_uncharted2.png)
- [G24 — Procedural Noise (CPU)](g24_procedural_noise.md) — `procedural::Noise`: seeded value/Perlin/Simplex 2D+3D, fBm/ridged/turbulence, Domain-Warp; Höhenfelder fürs Terrain carven, Sky-Cubemaps, Volumenfelder; Screenshots in [docs/images/](../../images/g24_noise_wide.png)
- [G25 — Marching Cubes (Voxel → Mesh)](g25_marching_cubes.md) — `procedural::marchingCubes`/`marchingCubesChunked`: Isofläche aus Volumen extrahieren (watertight, indexiert), Höhlen-Terrain aus Noise-Volumen; Screenshots in [docs/images/](../../images/g25_marching_cubes_cave.png)
- [G26 — L-Systeme (Turtle → Mesh)](g26_lsystem.md) — `procedural::LSystem`: Regel-Ersetzung (`derive`) + 3D-Turtle (`generate`) → Bäume/Farne/Büsche als `MeshData`; Screenshots in [docs/images/](../../images/g26_lsystem_forest.png)
- [G27 — Wave Function Collapse](g27_wfc.md) — `procedural::Wfc`: Edge-Tag-Tiles + Rotationen, Min-Entropie + Propagation + Backtracking → Dungeon-Layout als Szenen-Knoten; Screenshots in [docs/images/](../../images/g27_wfc_overview.png)
- [G28 — Voronoi-Diagramme](g28_voronoi.md) — `procedural::voronoi2d` + `extrudeCells` (Zellen → Prismen-Meshes), F1/F2-Distanzfelder, gerissene Platte mit glühenden Rissen + Kristalle; Screenshots in [docs/images/](../../images/g28_voronoi_shattered.png)
- [G29 — SDF / 3D-Fraktale (Mandelbulb)](g29_mandelbulb.md) — `procedural::sdf` + `meshFromSdf` (SDF via Marching Cubes meshen), emissive Fraktal-Skulptur mit Bloom; Screenshots in [docs/images/](../../images/g29_mandelbulb_wide.png)
- [G17 — Bloom](g17_bloom.md) — Post-Pass nach den Partikeln, vor TAA: Bright-Pass oberhalb
  eines einstellbaren `bloomThreshold` + 9-Tap-Blur + Add (`hdrP` -> `hdrB`), `Effect::Bloom`;
  Bilder in [docs/images/](../../images/g17_bloom_on.png)
- [G16 — Screen-Space Reflections](g16_ssr.md) — Post-Pass nach dem Lighting, raymarched
  gegen die G-Buffer-Tiefe (Fresnel/Roughness/Edge-Maske), Ausgabe `hdrS`; `Effect::Ssr`;
  Bilder in [docs/images/](../../images/g16_ssr_on.png)
- [G15 — SSAO](g15_ssao.md) — Post-Pass vor dem Lighting aus dem G-Buffer
  (Hemisphären-Kernel + Tiefenvergleich in die Ressource `ssao`, vom Lighting-Pass
  gesampelt, `Effect::Ssao`); Bilder in [docs/images/](../../images/g15_ssao_on.png)
- [G14 — GPU-Skinning (GL ≥ 4.3)](g14_gpu_skinning.md) — `anim::GpuSkinner` (VAO +
  JOINTS/WEIGHTS-Attribute + Matrix-Palette-SSBO), gezeichnet vom Skinning-Vertex-Shader des
  G3-G-Buffer-Passes, wenn `Effect::GpuSkinning` an ist (CPU-Fallback darunter); Bilder in
  [docs/images/](../../images/g14_gpu_skinning_on.png)

Als Nächstes: **G19–G23** (technische Erweiterungen:
Szenen-Serialisierung, Terrain/LOD, volumetrischer Nebel, Wetness, Tonemap-Operatoren) /
**G11** (Engine-Infrastruktur) / **G2** (Qt-Editor) — siehe [REQUIREMENTS.md](../../REQUIREMENTS.md).

Voraussetzung für alle: Bibliothek bauen und `FuhGlGraphics::fuhglgraphics` linken
(siehe [DEVELOPMENT.md](../../DEVELOPMENT.md#build-options)).
