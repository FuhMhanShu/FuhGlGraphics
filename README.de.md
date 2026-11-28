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

Requirements G1–G11 liegen vor ([docs/REQUIREMENTS.md](docs/REQUIREMENTS.md)). Bereits
umgesetzt — eine Zeile pro Anforderung, mit Tutorial und Screenshot (alle Screenshots in
[docs/images/](docs/images/)):

| Req | Feature | Tutorial | Screenshot |
| --- | ------- | -------- | ---------- |
| G1 | FuhGLCore-Anbindung (CMake/qmake) | — | — |
| G3 | Deferred-Renderer `render::DeferredRenderer`: erweiterter G-Buffer (Albedo+Normal, Metallic/Roughness/AO, Emissive, Motion-Vectors, Material-ID), PBR-Lighting über FuhGLCore-Lights/Shadows/IBL, Post-Chain (TAA → FSR1-artiges Upscaling → ACES-Tonemapping) mit Einzelschaltern | [G3](docs/de/tutorials/g3_deferred_renderer.md) | [g3_deferred_1280x720.png](docs/images/g3_deferred_1280x720.png) |
| G4 | glTF-2.0-Asset-Loading: `io::loadGltf()` (.gltf/.glb, eigener JSON-Parser) + `io::instantiateGltf()` auf den G9-Szenengraphen | [G4](docs/de/tutorials/g4_gltf_loader.md) | [g4_gltf_ship.png](docs/images/g4_gltf_ship.png) |
| G5 | GPU-Partikel `particles::ParticleSystem` (Compute ab GL 4.3, CPU-Fallback) als G9-`ParticleEmitterComponent`, gerendert im G3-"particles"-Post-Pass | [G5](docs/de/tutorials/g5_particles.md) | [g5_particles_fire.png](docs/images/g5_particles_fire.png) |
| G6 | Animation/Skinning: `anim::AnimationPlayer` (TRS-Clips) + `anim::Skeleton` mit CPU-Skinning, glTF-Animationen/Skins über den G4-Loader, als G9-`AnimationComponent` | [G6](docs/de/tutorials/g6_animation.md) | [g6_animation_bend.png](docs/images/g6_animation_bend.png) |
| G7 | SH-Light-Probes `probes::LightProbeGrid` (CPU-Bake + 3D-Textur-Sampling in der G3-Lighting-Pass) für diffuses indirektes Licht / Color-Bleeding, als G9-`LightProbeComponent` | [G7](docs/de/tutorials/g7_light_probes.md) | [g7_probes_on.png](docs/images/g7_probes_on.png) |
| G8 | Deferred-Decals: `render::Decal`-Boxen in den G-Buffer projiziert (Material-ID-gegated) als G9-`DecalComponent` | [G8](docs/de/tutorials/g8_decals.md) | [g8_decals_on.png](docs/images/g8_decals_on.png) |
| G9 | Szenengraph `scene::Scene`: Nodes mit polymorpher Komponentenliste, mehrere Meshes + Albedo-/Normalmap-Texturen pro Node, Licht-/Kamera-Komponenten, Effektliste + Parameter pro Node, Instancing pro Node, World-AABBs + Frustum-Culling; `Scene::fillFrame()` füttert den G3-Renderer | [G9](docs/de/tutorials/g9_scene_graph.md) | [g9_scene_graph.png](docs/images/g9_scene_graph.png) |
| G10 | Quick-Upgrades: DoF mit hexagonalem Bokeh-Kernel (`Effect::Dof`), Mini-Debug-HUD (`Effect::DebugHud`), perzeptueller PSNR-Screenshot-Diff in der CI | [G10](docs/de/tutorials/g10_upgrades.md) | [g10_dof_on.png](docs/images/g10_dof_on.png) |
| G13 | Realistisches Wasser `water::WaterSurface` (Gerstner-Wellen) als G9-`WaterComponent`: planarer Reflexions-Pass + Wasser-Post-Pass (Fresnel-Mix, Schaum, Sonnen-Glint), `Effect::Water` | [G13](docs/de/tutorials/g13_water.md) | [g13_water_1280x720.png](docs/images/g13_water_1280x720.png) |
| G14 | GPU-Skinning `anim::GpuSkinner` (VAO + JOINTS_0/WEIGHTS_0-Attribute + Matrix-Palette-SSBO, GL >= 4.3; CPU-Fallback darunter), gezeichnet vom G3-G-Buffer-Pass wenn `Effect::GpuSkinning` an ist | [G14](docs/de/tutorials/g14_gpu_skinning.md) | [g14_gpu_skinning_on.png](docs/images/g14_gpu_skinning_on.png) |
| G15 | SSAO `Effect::Ssao`: G-Buffer-basierter Hemisphären-Kernel-Pass vor dem Lighting, vom Lighting-Pass gesampelt | [G15](docs/de/tutorials/g15_ssao.md) | [g15_ssao_on.png](docs/images/g15_ssao_on.png) |
| G16 | Screen-Space Reflections `Effect::Ssr`: Post-Pass nach dem Lighting, raymarched gegen die G-Buffer-Tiefe (Fresnel/Roughness/Edge-Maske) | [G16](docs/de/tutorials/g16_ssr.md) | [g16_ssr_on.png](docs/images/g16_ssr_on.png) |
| G17 | Bloom `Effect::Bloom`: Post-Pass nach den Partikeln, vor TAA; Bright-Pass oberhalb des einstellbaren `bloomThreshold` + 9-Tap-Blur | [G17](docs/de/tutorials/g17_bloom.md) | [g17_bloom_on.png](docs/images/g17_bloom_on.png) |
| G18 | Picking / Ray-Casting auf dem Szenengraphen `scene::pick()`: Strahl von der Kamera durch ein Pixel, World-AABB-Culling, Müller–Trumbore-Dreieckstests | [G18](docs/de/tutorials/g18_picking.md) | [g18_picking_marker.png](docs/images/g18_picking_marker.png) |
| G19 | Szenen-Serialisierung `scene/serialize.hpp`: Scene <-> JSON-Round-Trip über `io::writeJson`, `saveScene`/`loadScene` für .json-Dateien | [G19](docs/de/tutorials/g19_serialization.md) | [g19_serialization_loaded.png](docs/images/g19_serialization_loaded.png) |
| G20 | Terrain + LOD `scene::TerrainComponent`: LodMesh im `fillFrame()`-Pfad, pro Frame LOD-Level aus der Kamera-Distanz, `heightAt()` zum Platzieren von Objekten | [G20](docs/de/tutorials/g20_terrain_lod.md) | [g20_terrain_far.png](docs/images/g20_terrain_far.png) |
| G21 | Volumetrischer Nebel/Wolken `Effect::VolumetricFog`: rayge-marchtes 3D-Noise-Volumen mit Henyey–Greenstein-Sonnenstreuung gegen die G-Buffer-Tiefe (reine CPU-Zwillinge in `render/volumetric.hpp`) | [G21](docs/de/tutorials/g21_volumetric_fog.md) | [g21_volumetric_on.png](docs/images/g21_volumetric_on.png) |
| G22 | Wetness/Regen auf PBR `Effect::Wetness`: Roughness/Albedo-Modulation mit Noise-Pfützen-Maske auf nach oben zeigenden Flächen + SSR-Boost (reine CPU-Zwillinge in `render/wetness.hpp`) | [G22](docs/de/tutorials/g22_wetness.md) | [g22_wetness_wet.png](docs/images/g22_wetness_wet.png) |
| G23 | Tonemapping-Operatoren `render::TonemapOperator` (ACES / Reinhard / Uncharted2 / linear, pro Frame) + einstellbarer Bloom-Threshold (`bloomThreshold`, G17) | [G23](docs/de/tutorials/g23_tonemap.md) | [g23_tonemap_uncharted2.png](docs/images/g23_tonemap_uncharted2.png) |
| G24 | Prozedurales Noise `procedural::Noise`: seeded, deterministischer Value-/Perlin-/Simplex-Noise 2D+3D mit fBm/Ridged/Turbulence und Domain-Warp; gemeinsame Noise-Quelle für die G25–G29-Generatoren | [G24](docs/de/tutorials/g24_procedural_noise.md) | [g24_noise_wide.png](docs/images/g24_noise_wide.png) |
| G25 | Marching Cubes `procedural::marchingCubes` / `marchingCubesChunked`: Voxel-Volumen → wasserdichtes indexiertes `MeshData` (Höhlen-Terrain, SDF-Meshing) | [G25](docs/de/tutorials/g25_marching_cubes.md) | [g25_marching_cubes_cave.png](docs/images/g25_marching_cubes_cave.png) |
| G26 | L-Systeme `procedural::LSystem`: Regel-Ersetzung + 3D-Turtle → `MeshData` für Bäume/Farne/Büsche (Presets `makeTree`/`makeFern`/`makeBush`) | [G26](docs/de/tutorials/g26_lsystem.md) | [g26_lsystem_forest.png](docs/images/g26_lsystem_forest.png) |
| G27 | Wave Function Collapse `procedural::Wfc`: Edge-Tag-Tiles + Rotationen, Min-Entropie + Propagation + Backtracking → Dungeon-/Stadt-Layouts als Szenen-Knoten | [G27](docs/de/tutorials/g27_wfc.md) | [g27_wfc_overview.png](docs/images/g27_wfc_overview.png) |
| G28 | Voronoi-Diagramme `procedural::voronoi2d` Zellen + F1/F2 + `extrudeCells`-Prismen → gerissene Platten, Kristall-Splitter | [G28](docs/de/tutorials/g28_voronoi.md) | [g28_voronoi_shattered.png](docs/images/g28_voronoi_shattered.png) |
| G29 | SDF / 3D-Fraktale: `procedural::sdf`-Primitive + `smoothMin` + `mandelbulb` DE; `procedural::meshFromSdf` mesht jedes SDF via G25 Marching Cubes | [G29](docs/de/tutorials/g29_mandelbulb.md) | [g29_mandelbulb_wide.png](docs/images/g29_mandelbulb_wide.png) |
| G30 | Prozedurale Kreaturen: `procedural::Skeleton`-Graph + `creatureSdf` smooth-min SDF + Simplex-Detail; `meshCreature` via G25 mit Krümmung im Vertex-Color; `bakeCreatureMaps` Objektraum-Noise-Haut | [G30](docs/de/tutorials/g30_creature.md) | [g30_creature_wide.png](docs/images/g30_creature_wide.png) |
| G31 | World-Generator-Hilfsfunktion `procedural::generateWorld(WorldGenParams)`: Landschaft (Spline-Relief, Terracing, Flüsse, Whittaker-Biome, G37-Vegetation) oder WFC-Stadt (prozedurale Häuser, wählbare/nasse Straßen); deterministisch pro Seed | [G31](docs/de/tutorials/g31_worldgen.md) | [g31_worldgen_city.png](docs/images/g31_worldgen_city.png) |
| G12 | Asset-Creator-Qt-Tool `fuhglgraphics_asset_creator` + MCP: Szenenbaum/Inspektor/Asset-Browser, G3-Viewport mit Picking, LLM-Chat über stdio JSON-RPC 2.0; der MCP-Katalog (`mcp/`) macht die prozeduralen Funktionen G24–G31, die Deferred-Render-Einstellungen (`Effect`/`EffectParams`) und die G13-Wasser-Parameter zugreifbar; Qt-freier stdio-MCP-Server `fuhglgraphics_mcp_server`; `.fasset`-JSON-Projekte, G11-Asset-Cache | [G12](docs/de/tutorials/g12_asset_creator.md) | — |
| G37 | Ökophysiologischer Vegetations-Generator `procedural::VegetationGenerator`: Space Colonization zu Licht-Attraktoren (freier Baum → breite Krone, dichter Wald → Self-Pruning, hoher Stamm, kleine Krone oben), Klima→Geometrie-Regeln (Träufelspitzen / Nadeln / Dornen / Sukkulenten / Windflüchter), Da-Vinci-Pipe-Model-Radien, Blatt- & Rinden-Shader-Parameter (Alter, Herbst-Rampe, G33-SSS, Moos auf der Wetterseite); G31-Landschaftsbäume nutzen ihn | [G37](docs/de/tutorials/g37_vegetation.md) | [g37_vegetation_wide.png](docs/images/g37_vegetation_wide.png) |
| G38 | Wettersystem `weather::WeatherSystem` (geseedeter CPU-Zustandsautomat: weiche Übergänge clear→storm, Tageszeit-Sonnenpfad, Nässe-Lag, Blitz-Zeitplan) als `scene::WeatherComponent` → `FrameData::weather` → `DeferredRenderer::render(..., weather)`: `Effect::Sky` prozeduraler Himmel + Wolken, `Effect::Lightning` Flash + Bolt, G21-Volumen-Coverage + G22-Nässe- + Exposure-Kopplung, Regen über `weather::makeRainEmitter` (G5); G19-Serialisierung | [G38](docs/de/tutorials/g38_weather.md) | [g38_weather_rain.png](docs/images/g38_weather_rain.png) |
| G40 | Flexible eigene Post-Passes `render::DeferredRenderer::PostPass` (`addPostPass`/`removePostPass`/`setPostPassEnabled`, `"@chain"`-Ketten-Input) nach dem Tonemap (`tonemap` → eigene → `comic` → `present`), alle Stage-Ressourcen als Pass-Inputs wiederverwendbar; eingebauter `Effect::ComicLook` Comic-/Cel-Look (Posterisieren + Sobel-Ink-Kanten) als Beispiel, `examples/comic_screenshot` | [G40](docs/de/tutorials/g40_custom_post_passes.md) | [g40_comic_on.png](docs/images/g40_comic_on.png) |
| G41 | 2D-Engine / UI-Layer `ui::UiRoot` + Widgets (Label, Button, Checkbox, RadioButton, Slider, ProgressBar, LineEdit, ListBox, Panel, Image): `ui::Batch2D` Batch-Renderer (eingebetteter 8x8-Font-Atlas, Rounded Rects, Scissor), Qt-freies `Property<T>` Zwei-Wege-Binding Backend↔GUI, Layouts (Free/HBox/VBox/Grid), `ui::Theme`; G3-Integration: `ui::makeOverlayPass` (Screen-Space-Overlay als Post-Pass nach dem Tonemap) + `ui::WorldSurface`/`pickUv` (Menü auf einem 3D-Bildschirm-Modell, Klicks über G18-Picking); `examples/ui_screenshot` | [G41](docs/de/tutorials/g41_ui_2d.md) | [g41_ui.png](docs/images/g41_ui.png) |

Als Nächstes: **G12 P5 Polish** (3D-Gizmo, Thumbnails) — siehe [REQUIREMENTS.md](docs/REQUIREMENTS.md)
und [ASSET_CREATOR.md](docs/ASSET_CREATOR.md); **G11** ist teilweise umgesetzt
(`io::AssetCache` Content-Hash-Cache, `scene::effectsToJson` JSON-Render-Config,
`editor::GlViewportWidget`).
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
