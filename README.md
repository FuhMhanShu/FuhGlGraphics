# FuhGlGraphics

**A cross-platform 2D/3D graphics framework with an asset importer, built on top of
[FuhGLCore](https://github.com/FuhMhanShu/FuhGLCore).** FuhGlGraphics is the application
layer above FuhGLCore's GL/window/math core: it adds a new deferred renderer, glTF 2.0
asset loading, a scene graph, GPU particles, animation, light probes, decals and engine
infrastructure (asset cache, JSON config, minimal Qt6 editor) — see the agreed
requirements [G1–G11](docs/REQUIREMENTS.md).

> 🇩🇪 **Deutsche Version:** [README.de.md](README.de.md) · Deutsche Doku: [docs/de/](docs/de/tutorials/README.md)

## Dependencies

- **FuhGLCore (direct)** — GL loading, windowing, math, pipelines. Needs `main` at
  `9fc991c` or newer (the F45 shadow kit the G3 renderer's shadows are built on).
  Resolved by CMake in
  this order: `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>` (local checkout, `add_subdirectory`)
  → `find_package(FuhGLCore)` (installed) → FetchContent fallback from
  `github.com/FuhMhanShu/FuhGLCore` (`FUHGLGRAPHICS_FUHGLCORE_GIT_TAG`, default `main`).
  Linked as `FuhGLCore::fuhglcore` (PUBLIC). In qmake projects:
  `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" ...` + `include($$FUHGLCORE_DIR/fuhglcore.pri)`.
- **Qt6 (direct, optional)** — only for editors/tools (`editor/`, built with
  `FUHGLGRAPHICS_WITH_QT=ON`, default). CMake: `find_package(Qt6 COMPONENTS Widgets
  OpenGLWidgets)`; qmake: `QT += widgets openglwidgets`. The library core stays Qt-free;
  build with `-DFUHGLGRAPHICS_WITH_QT=OFF` for headless use.
- Otherwise only the C++17 standard library (own minimal JSON parser, own test fixtures —
  no nlohmann, no vendored CC-BY sample assets).

## Status

Requirements G1–G11 are in place ([docs/REQUIREMENTS.md](docs/REQUIREMENTS.md)). Implemented
so far — one row per requirement, with tutorial and screenshot (all screenshots in
[docs/images/](docs/images/)):

| Req | Feature | Tutorial | Screenshot |
| --- | ------- | -------- | ---------- |
| G1 | FuhGLCore dependency wiring (CMake/qmake) | — | — |
| G3 | Deferred renderer `render::DeferredRenderer`: extended G-buffer (albedo+normal, metallic/roughness/AO, emissive, motion vectors, material ID), PBR lighting on FuhGLCore lights/shadows/IBL, post chain (TAA → FSR1-style upscale → ACES tonemap) with per-effect toggles | [G3](docs/en/tutorials/g3_deferred_renderer.md) | [g3_deferred_1280x720.png](docs/images/g3_deferred_1280x720.png) |
| G4 | glTF 2.0 asset loading: `io::loadGltf()` (.gltf/.glb, own JSON parser) + `io::instantiateGltf()` onto the G9 scene graph | [G4](docs/en/tutorials/g4_gltf_loader.md) | [g4_gltf_ship.png](docs/images/g4_gltf_ship.png) |
| G5 | GPU particles `particles::ParticleSystem` (compute on GL >= 4.3, CPU fallback) as G9 `ParticleEmitterComponent`, rendered in the G3 "particles" post pass | [G5](docs/en/tutorials/g5_particles.md) | [g5_particles_fire.png](docs/images/g5_particles_fire.png) |
| G6 | Animation/skinning: `anim::AnimationPlayer` (TRS clips) + `anim::Skeleton` with CPU skinning, glTF animations/skins via the G4 loader, as G9 `AnimationComponent` | [G6](docs/en/tutorials/g6_animation.md) | [g6_animation_bend.png](docs/images/g6_animation_bend.png) |
| G7 | SH light probes `probes::LightProbeGrid` (CPU bake + 3D-texture sampling in the G3 lighting pass) for diffuse indirect light / color bleeding, as G9 `LightProbeComponent` | [G7](docs/en/tutorials/g7_light_probes.md) | [g7_probes_on.png](docs/images/g7_probes_on.png) |
| G8 | Deferred decals `render::Decal` boxes projected into the G-buffer (material-ID gated) as G9 `DecalComponent` | [G8](docs/en/tutorials/g8_decals.md) | [g8_decals_on.png](docs/images/g8_decals_on.png) |
| G9 | Scene graph `scene::Scene`: nodes with polymorphic component list, several meshes + albedo/normal-map textures per node, light/camera components, per-node effects + instancing, world AABBs + frustum culling; `Scene::fillFrame()` feeds the G3 renderer | [G9](docs/en/tutorials/g9_scene_graph.md) | [g9_scene_graph.png](docs/images/g9_scene_graph.png) |
| G10 | Quick upgrades: DoF with hexagonal bokeh kernel (`Effect::Dof`), mini debug HUD (`Effect::DebugHud`), perceptual PSNR screenshot diff in CI | [G10](docs/en/tutorials/g10_upgrades.md) | [g10_dof_on.png](docs/images/g10_dof_on.png) |
| G13 | Realistic water `water::WaterSurface` (Gerstner waves) as G9 `WaterComponent`: planar-reflection pass + water post pass (fresnel mix, foam, sun glint), `Effect::Water` | [G13](docs/en/tutorials/g13_water.md) | [g13_water_1280x720.png](docs/images/g13_water_1280x720.png) |
| G14 | GPU skinning `anim::GpuSkinner` (VAO + JOINTS_0/WEIGHTS_0 + matrix-palette SSBO, GL >= 4.3; CPU fallback below) drawn by the G3 G-buffer pass when `Effect::GpuSkinning` is on | [G14](docs/en/tutorials/g14_gpu_skinning.md) | [g14_gpu_skinning_on.png](docs/images/g14_gpu_skinning_on.png) |
| G15 | SSAO `Effect::Ssao`: G-buffer-based hemisphere-kernel pass before lighting, sampled by the lighting pass | [G15](docs/en/tutorials/g15_ssao.md) | [g15_ssao_on.png](docs/images/g15_ssao_on.png) |
| G16 | Screen-space reflections `Effect::Ssr`: post pass after lighting raymarching the G-buffer depth (fresnel/roughness/edge mask) | [G16](docs/en/tutorials/g16_ssr.md) | [g16_ssr_on.png](docs/images/g16_ssr_on.png) |
| G17 | Bloom `Effect::Bloom`: post pass after particles, before TAA; bright-pass above the adjustable `bloomThreshold` + 9-tap blur | [G17](docs/en/tutorials/g17_bloom.md) | [g17_bloom_on.png](docs/images/g17_bloom_on.png) |
| G18 | Picking / ray-casting on the scene graph `scene::pick()`: ray from the camera through a pixel, world-AABB culling, Möller–Trumbore triangle tests | [G18](docs/en/tutorials/g18_picking.md) | [g18_picking_marker.png](docs/images/g18_picking_marker.png) |
| G19 | Scene serialization `scene/serialize.hpp`: Scene <-> JSON round-trip via `io::writeJson`, `saveScene`/`loadScene` for .json files | [G19](docs/en/tutorials/g19_serialization.md) | [g19_serialization_loaded.png](docs/images/g19_serialization_loaded.png) |
| G20 | Terrain + LOD `scene::TerrainComponent`: LodMesh in the `fillFrame()` path, per-frame LOD level from the camera distance, `heightAt()` for object placement | [G20](docs/en/tutorials/g20_terrain_lod.md) | [g20_terrain_far.png](docs/images/g20_terrain_far.png) |
| G21 | Volumetric fog/clouds `Effect::VolumetricFog`: raymarched 3D-noise volume with Henyey–Greenstein sun scattering against the G-buffer depth (pure CPU twins in `render/volumetric.hpp`) | [G21](docs/en/tutorials/g21_volumetric_fog.md) | [g21_volumetric_on.png](docs/images/g21_volumetric_on.png) |
| G22 | Wetness/rain on PBR `Effect::Wetness`: roughness/albedo modulation with a noise puddle mask on up-facing surfaces + SSR boost (pure CPU twins in `render/wetness.hpp`) | [G22](docs/en/tutorials/g22_wetness.md) | [g22_wetness_wet.png](docs/images/g22_wetness_wet.png) |
| G23 | Tonemapping operators `render::TonemapOperator` (ACES / Reinhard / Uncharted2 / linear, per frame) + adjustable bloom threshold (`bloomThreshold`, G17) | [G23](docs/en/tutorials/g23_tonemap.md) | [g23_tonemap_uncharted2.png](docs/images/g23_tonemap_uncharted2.png) |
| G24 | Procedural noise `procedural::Noise`: seeded, deterministic value/Perlin/Simplex 2D+3D with fBm/ridged/turbulence and domain warp; shared noise source for the G25–G29 generators | [G24](docs/en/tutorials/g24_procedural_noise.md) | [g24_noise_wide.png](docs/images/g24_noise_wide.png) |
| G25 | Marching cubes `procedural::marchingCubes` / `marchingCubesChunked`: voxel volume → watertight indexed `MeshData` (cave terrain, SDF meshing) | [G25](docs/en/tutorials/g25_marching_cubes.md) | [g25_marching_cubes_cave.png](docs/images/g25_marching_cubes_cave.png) |
| G26 | L-systems `procedural::LSystem`: rule rewriting + 3D turtle → `MeshData` trees/ferns/bushes (presets `makeTree`/`makeFern`/`makeBush`) | [G26](docs/en/tutorials/g26_lsystem.md) | [g26_lsystem_forest.png](docs/images/g26_lsystem_forest.png) |
| G27 | Wave function collapse `procedural::Wfc`: edge-tag tiles + rotations, min-entropy + propagation + backtracking → dungeon/city layouts as scene nodes | [G27](docs/en/tutorials/g27_wfc.md) | [g27_wfc_overview.png](docs/images/g27_wfc_overview.png) |
| G28 | Voronoi diagrams `procedural::voronoi2d` cells + F1/F2 + `extrudeCells` prisms → cracked plates, crystal shards | [G28](docs/en/tutorials/g28_voronoi.md) | [g28_voronoi_shattered.png](docs/images/g28_voronoi_shattered.png) |
| G29 | SDF / 3D fractals: `procedural::sdf` primitives + `smoothMin` + `mandelbulb` DE; `procedural::meshFromSdf` meshes any SDF via G25 marching cubes | [G29](docs/en/tutorials/g29_mandelbulb.md) | [g29_mandelbulb_wide.png](docs/images/g29_mandelbulb_wide.png) |
| G30 | Procedural creatures: `procedural::Skeleton` graph + `creatureSdf` smooth-min SDF + simplex detail; `meshCreature` via G25 with curvature in the vertex color; `bakeCreatureMaps` object-space noise skin maps | [G30](docs/en/tutorials/g30_creature.md) | [g30_creature_wide.png](docs/images/g30_creature_wide.png) |
| G31 | World generator helper `procedural::generateWorld(WorldGenParams)`: landscape (spline relief, terracing, rivers, Whittaker biomes, PlantGenerator) or WFC city (procedural houses, selectable/wet roads); deterministic per seed | [G31](docs/en/tutorials/g31_worldgen.md) | [g31_worldgen_city.png](docs/images/g31_worldgen_city.png) |
| G12 | Asset creator Qt tool `fuhglgraphics_asset_creator` + MCP: scene tree/inspector/asset browser, G3 viewport with picking, LLM chat over stdio JSON-RPC 2.0; the MCP catalog (`mcp/`) exposes the procedural G24–G31 functions, the deferred-render settings (`Effect`/`EffectParams`) and the G13 water params; Qt-free stdio MCP server `fuhglgraphics_mcp_server`; `.fasset` JSON projects, G11 asset cache | [G12](docs/en/tutorials/g12_asset_creator.md) | — |

Next: **G12 P5 polish** (3D gizmo, thumbnails) — see [REQUIREMENTS.md](docs/REQUIREMENTS.md)
and [ASSET_CREATOR.md](docs/ASSET_CREATOR.md); **G11** is partially in place
(`io::AssetCache` content-hash cache, `scene::effectsToJson` JSON render config,
`editor::GlViewportWidget`).
Baseline: OpenGL 3.3 core (GL 4.3 gates GPU particles/skinning, macOS ≤ 4.1), inherited
from FuhGLCore.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFUHGLGRAPHICS_WITH_QT=OFF \
    -DFUHGLGRAPHICS_FUHGLCORE_DIR=/path/to/FuhGLCore   # optional, see above
cmake --build build
ctest --test-dir build --output-on-failure -L unit
cmake --install build --prefix <prefix>   # provides find_package(FuhGlGraphics)
```

Options: `-DFUHGLGRAPHICS_WITH_QT=ON|OFF` (default ON),
`-DFUHGLGRAPHICS_BUILD_TESTS=ON` (default), `-DFUHGLGRAPHICS_BUILD_EXAMPLES=ON` (default;
screenshot tool in `examples/`), `-DFUHGLGRAPHICS_ENABLE_HARDENING=ON` (default;
compiler/linker hardening flags, see [DEVELOPMENT.md](docs/DEVELOPMENT.md)),
`-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>`, `-DFUHGLGRAPHICS_FUHGLCORE_GIT_TAG=<ref>`.

Consume the installed package:

```cmake
find_package(FuhGlGraphics REQUIRED)
target_link_libraries(your_app PRIVATE FuhGlGraphics::fuhglgraphics)
```

**qmake alternative** (Qt build system): `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore"
FuhGlGraphics.pro && make` builds the static library `libfuhglgraphics.a`.
`FUHGLGRAPHICS_WITH_QT=0` (qmake var) builds the Qt-free headless core.

## Documentation

- [Documentation overview / Sprachauswahl](docs/README.md)
- [User guide](docs/USER_GUIDE.md) — concepts (the descriptive part, English)
- [Tutorials (English)](docs/en/tutorials/README.md) · [Tutorials (Deutsch)](docs/de/tutorials/README.md)
- [Development](docs/DEVELOPMENT.md) — layout, conventions, build, tests, CI (the technical part, English)
- [Requirements](docs/REQUIREMENTS.md) — the agreed requirements G1–G11 (English)
- API reference — Doxygen HTML (locally: `cd docs && doxygen Doxyfile`)

## Security

Found a vulnerability? Please report it privately (no public issues) —
see [SECURITY.md](SECURITY.md).

## License

MIT — see [LICENSE](LICENSE).
