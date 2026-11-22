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

Als Nächstes: **G5** (GPU-Partikel) — siehe [REQUIREMENTS.md](../../REQUIREMENTS.md).

Voraussetzung für alle: Bibliothek bauen und `FuhGlGraphics::fuhglgraphics` linken
(siehe [DEVELOPMENT.md](../../DEVELOPMENT.md#build-options)).
