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
