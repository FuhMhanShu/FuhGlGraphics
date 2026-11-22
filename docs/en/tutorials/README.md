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

Next: **G4** (glTF 2.0 asset loading) — see [REQUIREMENTS.md](../../REQUIREMENTS.md).

Prerequisite for all: build the library and link `FuhGlGraphics::fuhglgraphics`
(see [DEVELOPMENT.md](../../DEVELOPMENT.md#build-options)).
