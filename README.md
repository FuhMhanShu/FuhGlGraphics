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

Requirements G1–G11 are in place ([docs/REQUIREMENTS.md](docs/REQUIREMENTS.md)).
Implemented so far: **G1** (FuhGLCore dependency wiring), **G3** — the deferred renderer
`fuhglgraphics::render::DeferredRenderer` with an extended G-buffer (albedo+normal,
metallic/roughness/AO, emissive, motion vectors, material ID), PBR lighting reusing FuhGLCore
lights/shadows/IBL, and a post chain (TAA → FSR1-style upscale → ACES tonemap) with
per-effect toggles — see the [G3 tutorial](docs/en/tutorials/g3_deferred_renderer.md) and
screenshots in [docs/images/](docs/images/g3_deferred_1280x720.png) — and **G9** — the scene
graph `fuhglgraphics::scene::Scene` (nodes with a polymorphic component list, several meshes
+ albedo/normal-map textures per node, light/camera components, per-node effect lists +
parameters, per-node instancing, world AABBs + frustum culling; `Scene::fillFrame()` feeds
the G3 renderer) — see the [G9 tutorial](docs/en/tutorials/g9_scene_graph.md) and
effect-comparison screenshots in [docs/images/](docs/images/g9_scene_graph.png) — and
**G4** — glTF 2.0 asset loading: `fuhglgraphics::io::loadGltf()` (.gltf/.glb, own JSON
parser, buffers/accessors/primitives/materials/images) + `io::instantiateGltf()` onto the
G9 scene graph — see the [G4 tutorial](docs/en/tutorials/g4_gltf_loader.md) and screenshots
in [docs/images/](docs/images/g4_gltf_ship.png) — and **G5** — GPU particles
(`particles::ParticleSystem`, compute on GL >= 4.3 with CPU fallback) as a G9
`ParticleEmitterComponent`, rendered in the G3 "particles" post pass — see the
[G5 tutorial](docs/en/tutorials/g5_particles.md) and screenshots in
[docs/images/](docs/images/g5_particles_fire.png) — and **G6** — animation/skinning:
`fuhglgraphics::anim::AnimationPlayer` (TRS clips, Once/Loop/PingPong) +
`anim::Skeleton` with CPU skinning (`skinMeshCpu`), glTF animations/skins via the G4
loader, as a G9 `AnimationComponent` — see the
[G6 tutorial](docs/en/tutorials/g6_animation.md) and screenshots in
[docs/images/](docs/images/g6_animation_bend.png) — and **G7** — SH light probes
(`probes::LightProbeGrid`, CPU bake + 3D-texture sampling in the G3 lighting pass) for
diffuse indirect light / color bleeding, as a G9 `LightProbeComponent` — see the
[G7 tutorial](docs/en/tutorials/g7_light_probes.md) and screenshots in
[docs/images/](docs/images/g7_probes_on.png) — and **G8** — deferred decals
(`render::Decal` boxes projected into the G-buffer, material-ID gated) as a G9
`DecalComponent` — see the [G8 tutorial](docs/en/tutorials/g8_decals.md) and
screenshots in [docs/images/](docs/images/g8_decals_on.png) — and **G10** — quick upgrades:
DoF with a hexagonal bokeh kernel (`Effect::Dof`), a mini debug HUD on FuhGLCore's
TextRenderer (`Effect::DebugHud`), and a perceptual PSNR screenshot diff in CI — see the
[G10 tutorial](docs/en/tutorials/g10_upgrades.md) and screenshots in
[docs/images/](docs/images/g10_dof_on.png).
and **G14** — GPU skinning (`anim::GpuSkinner`: VAO + JOINTS_0/WEIGHTS_0 attributes +
matrix-palette SSBO, drawn by the G3 G-buffer pass's GLSL-430 skinning shader when
`Effect::GpuSkinning` is on, GL >= 4.3; CPU fallback below) — see the
[G14 tutorial](docs/en/tutorials/g14_gpu_skinning.md) and screenshots in
[docs/images/](docs/images/g14_gpu_skinning_on.png) — and **G15** — SSAO (`Effect::Ssao`:
G-buffer-based hemisphere-kernel pass before lighting, AO factor in the `ssao` resource
sampled by the lighting pass) — see the [G15 tutorial](docs/en/tutorials/g15_ssao.md) and
screenshots in [docs/images/](docs/images/g15_ssao_on.png) — and **G16** — screen-space
reflections (`Effect::Ssr`: post pass after lighting raymarching the G-buffer depth,
fresnel/roughness/edge mask, output `hdrS`) — see the [G16 tutorial](docs/en/tutorials/g16_ssr.md)
and screenshots in [docs/images/](docs/images/g16_ssr_on.png) — and **G17** — bloom
(`Effect::Bloom`: post pass after particles, before TAA; bright-pass above the adjustable
`bloomThreshold` + 9-tap blur + add into `hdrB`) — see the [G17 tutorial](docs/en/tutorials/g17_bloom.md)
and screenshots in [docs/images/](docs/images/g17_bloom_on.png) — and **G18** — picking /
ray-casting on the scene graph (`scene::pick()`: ray from the camera through a pixel, world
AABB culling, Möller–Trumbore triangle tests on `MeshEntry::meshData`) — see the
[G18 tutorial](docs/en/tutorials/g18_picking.md) and screenshots in
[docs/images/](docs/images/g18_picking_marker.png) — and **G19** — scene serialization
(`scene/serialize.hpp`: Scene <-> JSON round-trip via `io::writeJson`, mesh specs resolved
through a `MeshResolver` callback, `saveScene`/`loadScene` for .json files) — see the
[G19 tutorial](docs/en/tutorials/g19_serialization.md) and screenshots in
[docs/images/](docs/images/g19_serialization_loaded.png) — and **G20** — terrain + LOD
(`scene::TerrainComponent`: FuhGLCore F38/F41 `terrainLods` LodMesh in the `fillFrame()` path,
per-frame LOD level from the camera distance via `DrawItem::lod`, `heightAt()` for object
placement) — see the [G20 tutorial](docs/en/tutorials/g20_terrain_lod.md) and screenshots in
[docs/images/](docs/images/g20_terrain_far.png) — and **G21** — volumetric fog/clouds
(`Effect::VolumetricFog`: "volumetric" post pass after particles, before bloom — raymarched
3D-noise volume with height falloff + Henyey–Greenstein sun scattering against the G-buffer
depth; pure CPU twins in `render/volumetric.hpp`) — see the
[G21 tutorial](docs/en/tutorials/g21_volumetric_fog.md) and screenshots in
[docs/images/](docs/images/g21_volumetric_on.png) — and **G22** — wetness/rain on PBR
(`Effect::Wetness`: lighting-pass roughness/albedo modulation with a noise puddle mask on
up-facing surfaces + SSR strength boost; pure CPU twins in `render/wetness.hpp`) — see the
[G22 tutorial](docs/en/tutorials/g22_wetness.md) and screenshots in
[docs/images/](docs/images/g22_wetness_wet.png) — and **G23** — tonemapping operators
(`render::TonemapOperator`: ACES / Reinhard / Uncharted2 / linear, per frame via `uOperator` in
the tonemap pass, pure CPU twins in `render/tonemap.hpp`) + adjustable bloom threshold
(`bloomThreshold`, G17) — see the [G23 tutorial](docs/en/tutorials/g23_tonemap.md) and
screenshots in [docs/images/](docs/images/g23_tonemap_uncharted2.png) — and **G24** — the
procedural noise module (`procedural::Noise` in `procedural/noise.hpp`: seeded, deterministic
value/Perlin/Simplex noise 2D+3D with fBm/ridged/turbulence and domain warp; shared noise
source for the G25–G29 generators) — see the [G24 tutorial](docs/en/tutorials/g24_procedural_noise.md)
and screenshots in [docs/images/](docs/images/g24_noise_wide.png) — and **G25** — marching
cubes (`procedural::marchingCubes` / `marchingCubesChunked`: voxel volume → watertight indexed
`MeshData`, used for cave terrain and SDF meshing) — see the
[G25 tutorial](docs/en/tutorials/g25_marching_cubes.md) and screenshots in
[docs/images/](docs/images/g25_marching_cubes_cave.png) — and **G26** — L-systems
(`procedural::LSystem`: rule rewriting + 3D turtle → `MeshData` trees/ferns/bushes, presets
`makeTree`/`makeFern`/`makeBush`) — see the [G26 tutorial](docs/en/tutorials/g26_lsystem.md)
and screenshots in [docs/images/](docs/images/g26_lsystem_forest.png) — and **G27** — wave
function collapse (`procedural::Wfc`: edge-tag tiles + rotations, min-entropy + propagation +
backtracking → dungeon/city layouts as scene nodes) — see the
[G27 tutorial](docs/en/tutorials/g27_wfc.md) and screenshots in
[docs/images/](docs/images/g27_wfc_overview.png) — and **G28** — Voronoi diagrams
(`procedural::voronoi2d` cells + F1/F2 + `extrudeCells` prisms → cracked plates, crystal shards)
— see the [G28 tutorial](docs/en/tutorials/g28_voronoi.md) and screenshots in
[docs/images/](docs/images/g28_voronoi_shattered.png) — and **G29** — SDF / 3D fractals
(`procedural::sdf` primitives + `smoothMin` + `mandelbulb` DE; `procedural::meshFromSdf` meshes
any SDF via G25 marching cubes) — see the [G29 tutorial](docs/en/tutorials/g29_mandelbulb.md)
and screenshots in [docs/images/](docs/images/g29_mandelbulb_wide.png) — and **G30** —
procedural creatures (`procedural::Skeleton` graph + `creatureSdf` smooth-min SDF + simplex
detail; `meshCreature` via G25 with curvature in the vertex color; `bakeCreatureMaps`
object-space noise skin maps) — see the [G30 tutorial](docs/en/tutorials/g30_creature.md) and
screenshots in [docs/images/](docs/images/g30_creature_wide.png).
Next: **G11** (engine infrastructure: asset cache, JSON config, minimal Qt editor) or **G2**
(Qt editor).
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
