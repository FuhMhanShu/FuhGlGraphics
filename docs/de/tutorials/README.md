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

- [G13 — Realistisches Wasser](g13_water.md) — `water::WaterSurface` (Gerstner-Wellen) als
  G9-`WaterComponent` → `FrameData::water` → G3-`render(..., water)` mit `Effect::Water`:
  planarer Reflexions-Pass (gespiegelte Kamera) + Wasser-Post-Pass (Reflexion/Refraktion
  per Fresnel, Kamm- + Uferschaum, Sonnen-Glint); See-Bilder in
  [docs/images/](../../images/g13_water_1280x720.png)

Als Nächstes: **G8** (Decals) — siehe [REQUIREMENTS.md](../../REQUIREMENTS.md).

Voraussetzung für alle: Bibliothek bauen und `FuhGlGraphics::fuhglgraphics` linken
(siehe [DEVELOPMENT.md](../../DEVELOPMENT.md#build-options)).
