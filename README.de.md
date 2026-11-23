# FuhGlGraphics

**Ein plattformübergreifendes 2D/3D-Grafik-Framework mit Asset-Importer, aufgebaut auf
[FuhGLCore](https://github.com/FuhMhanShu/FuhGLCore).** FuhGlGraphics ist die
Anwendungsschicht über dem GL-/Fenster-/Math-Kern von FuhGLCore: darauf kommen ein neuer
Deferred-Renderer, glTF-2.0-Asset-Loading, ein Szenengraph, GPU-Partikel, Animation,
Light-Probes, Decals und Engine-Infrastruktur (Asset-Cache, JSON-Konfig, minimaler
Qt6-Editor) — siehe die vereinbarten Anforderungen [G1–G11](docs/REQUIREMENTS.md).

🇬🇧 [English version of this README](README.md)

## Abhängigkeiten

- **FuhGLCore (direkt)** — GL-Loading, Windowing, Mathe, Pipelines. CMake-Auflösung in
  dieser Reihenfolge: `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<Pfad>` (lokales Checkout,
  `add_subdirectory`) → `find_package(FuhGLCore)` (installiert) → FetchContent-Fallback
  von `github.com/FuhMhanShu/FuhGLCore` (`FUHGLGRAPHICS_FUHGLCORE_GIT_TAG`, default
  `main`). Wird als `FuhGLCore::fuhglcore` (PUBLIC) gelinkt. In qmake-Projekten:
  `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" ...` + `include($$FUHGLCORE_DIR/fuhglcore.pri)`.
- **Qt6 (direkt, optional)** — nur für Editoren/Tools (`editor/`, gebaut mit
  `FUHGLGRAPHICS_WITH_QT=ON`, default). CMake: `find_package(Qt6 COMPONENTS Widgets
  OpenGLWidgets)`; qmake: `QT += widgets openglwidgets`. Der Bibliothekskern bleibt
  Qt-frei; mit `-DFUHGLGRAPHICS_WITH_QT=OFF` headless bauen.
- Sonst nur die C++17-Standardbibliothek (eigener minimaler JSON-Parser, eigene
  Test-Fixtures — kein nlohmann, keine vendored CC-BY-Sample-Assets).

## Status

Requirements G1–G11 liegen vor ([docs/REQUIREMENTS.md](docs/REQUIREMENTS.md)).
Bereits umgesetzt: **G1** (FuhGLCore-Anbindung), **G3** — der Deferred-Renderer
`fuhglgraphics::render::DeferredRenderer` mit erweitertem G-Buffer (Albedo+Normal,
Metallic/Roughness/AO, Emissive, Motion-Vectors, Material-ID), PBR-Lighting über die
FuhGLCore-Lights/Shadows/IBL und einer Post-Chain (TAA → FSR1-artiges Upscaling →
ACES-Tonemapping) mit Einzelschaltern — siehe [G3-Tutorial](docs/de/tutorials/g3_deferred_renderer.md)
und Screenshots in [docs/images/](docs/images/g3_deferred_1280x720.png) — sowie **G9** — der
Szenengraph `fuhglgraphics::scene::Scene` (Nodes mit polymorpher Komponentenliste, mehrere
Meshes + Albedo-/Normalmap-Texturen pro Node, Licht-/Kamera-Komponenten, Effektliste +
Parameter pro Node, Instancing pro Node, World-AABBs + Frustum-Culling; `Scene::fillFrame()`
füttert den G3-Renderer) — siehe [G9-Tutorial](docs/de/tutorials/g9_scene_graph.md) und
Effekt-Vergleichsbilder in [docs/images/](docs/images/g9_scene_graph.png) — und **G4** —
glTF-2.0-Asset-Loading: `fuhglgraphics::io::loadGltf()` (.gltf/.glb, eigener JSON-Parser,
Puffer/Accessoren/Primitive/Materialien/Bilder) + `io::instantiateGltf()` auf den
G9-Szenengraphen — siehe [G4-Tutorial](docs/de/tutorials/g4_gltf_loader.md) und Bilder in
[docs/images/](docs/images/g4_gltf_ship.png) — und **G5** — GPU-Partikel
(`particles::ParticleSystem`, Compute ab GL 4.3 mit CPU-Fallback) als
G9-`ParticleEmitterComponent`, gerendert im G3-"particles"-Post-Pass — siehe
[G5-Tutorial](docs/de/tutorials/g5_particles.md) und Bilder in
[docs/images/](docs/images/g5_particles_fire.png) — und **G6** — Animation/Skinning:
`fuhglgraphics::anim::AnimationPlayer` (TRS-Clips, Once/Loop/PingPong) +
`anim::Skeleton` mit CPU-Skinning (`skinMeshCpu`), glTF-Animationen/Skins über den
G4-Loader, als G9-`AnimationComponent` — siehe
[G6-Tutorial](docs/de/tutorials/g6_animation.md) und Bilder in
[docs/images/](docs/images/g6_animation_bend.png) — und **G7** — SH-Light-Probes
(`probes::LightProbeGrid`, CPU-Bake + 3D-Textur-Sampling in der G3-Lighting-Pass) für
diffuses indirektes Licht / Color-Bleeding, als G9-`LightProbeComponent` — siehe
[G7-Tutorial](docs/de/tutorials/g7_light_probes.md) und Bilder in
[docs/images/](docs/images/g7_probes_on.png) — und **G8** — Deferred-Decals
(`render::Decal`-Boxen in den G-Buffer projiziert, Material-ID-gegated) als G9-
`DecalComponent` — siehe [G8-Tutorial](docs/de/tutorials/g8_decals.md) und Bilder in
[docs/images/](docs/images/g8_decals_on.png) — und **G10** — Quick-Upgrades: DoF mit
hexagonalem Bokeh-Kernel (`Effect::Dof`), Mini-Debug-HUD auf FuhGLCore's TextRenderer
(`Effect::DebugHud`) und perzeptueller PSNR-Screenshot-Diff in der CI — siehe
[G10-Tutorial](docs/de/tutorials/g10_upgrades.md) und Bilder in
[docs/images/](docs/images/g10_dof_on.png).
und **G14** — GPU-Skinning (`anim::GpuSkinner`: VAO + JOINTS_0/WEIGHTS_0-Attribute +
Matrix-Palette-SSBO, gezeichnet vom GLSL-430-Skinning-Shader des G3-G-Buffer-Passes, wenn
`Effect::GpuSkinning` an ist, GL >= 4.3; CPU-Fallback darunter) — siehe
[G14-Tutorial](docs/de/tutorials/g14_gpu_skinning.md) und Bilder in
[docs/images/](docs/images/g14_gpu_skinning_on.png).
Als Nächstes: **G15–G23** (technische Erweiterungen: SSAO, SSR, Bloom, Picking,
Szenen-Serialisierung, Terrain/LOD, volumetrischer Nebel, Wetness, Tonemap-Operatoren), dann
**G11** (Engine-Infrastruktur: Asset-Cache, JSON-Config, minimaler Qt-Editor) oder **G2** (Qt-Editor).
Baseline: OpenGL 3.3 Core (GL 4.3 gatet GPU-Partikel/Skinning, macOS ≤ 4.1), übernommen
von FuhGLCore.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFUHGLGRAPHICS_WITH_QT=OFF \
    -DFUHGLGRAPHICS_FUHGLCORE_DIR=/path/to/FuhGLCore   # optional, siehe oben
cmake --build build
ctest --test-dir build --output-on-failure -L unit
cmake --install build --prefix <prefix>   # stellt find_package(FuhGlGraphics) bereit
```

Optionen: `-DFUHGLGRAPHICS_WITH_QT=ON|OFF` (default ON),
`-DFUHGLGRAPHICS_BUILD_TESTS=ON` (default), `-DFUHGLGRAPHICS_BUILD_EXAMPLES=ON` (default;
Screenshot-Tool in `examples/`), `-DFUHGLGRAPHICS_ENABLE_HARDENING=ON` (default;
Compiler-/Linker-Härtung, siehe [DEVELOPMENT.md](docs/DEVELOPMENT.md)),
`-DFUHGLGRAPHICS_FUHGLCORE_DIR=<Pfad>`, `-DFUHGLGRAPHICS_FUHGLCORE_GIT_TAG=<Ref>`.

Installiertes Paket konsumieren:

```cmake
find_package(FuhGlGraphics REQUIRED)
target_link_libraries(your_app PRIVATE FuhGlGraphics::fuhglgraphics)
```

**qmake-Alternative** (Qt-Buildsystem): `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore"
FuhGlGraphics.pro && make` baut die statische Bibliothek `libfuhglgraphics.a`.
`FUHGLGRAPHICS_WITH_QT=0` (qmake-Variable) baut den Qt-freien Headless-Kern.

## Dokumentation

- [Dokumentationsübersicht / Language picker](docs/README.md)
- [User Guide (Englisch)](docs/USER_GUIDE.md) — Konzepte (beschreibender Teil)
- [Tutorials (Deutsch)](docs/de/tutorials/README.md) · [Tutorials (English)](docs/en/tutorials/README.md)
- [Development (Englisch)](docs/DEVELOPMENT.md) — Layout, Konventionen, Build, Tests, CI (technischer Teil)
- [Requirements (Englisch)](docs/REQUIREMENTS.md) — die vereinbarten Anforderungen G1–G11
- API-Referenz — Doxygen-HTML (lokal: `cd docs && doxygen Doxyfile`)

## Security

Sicherheitslücke gefunden? Bitte privat melden (keine öffentlichen Issues) —
siehe [SECURITY.md](SECURITY.md).

## Lizenz

MIT — siehe [LICENSE](LICENSE).
