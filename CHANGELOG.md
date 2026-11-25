# Changelog

All notable changes to FuhGlGraphics are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) (the CMake project version is
`0.1.0` with same-major compatibility).

## [Unreleased]

### Fixed

- **G25 marching-cubes demo** showed see-through holes: the cave SDF reached the volume
  boundary, so the iso-surface was open there and the pipeline's back-face culling made the
  interior invisible. The demo field now keeps positive near the box border (falloff), so the
  surface closes inside the volume; crystals raised above the ridge and emissive/bloom
  strengthened so they visibly glow. Refs `docs/images/g25_marching_cubes_*.png` re-rendered
  + compressed; the closed-volume gotcha is documented in the G25 tutorials (en/de).
- **G30 creature look**: eyes no longer bloom into bright balls (smaller 0.06 spheres, embedded
  into the head, glossy dark albedo + dim amber emissive below the bloom threshold); the skin
  is no longer lumpy (geometry detail `detailAmp` 0.035 -> 0.02 default, higher frequency, fewer
  octaves) and the baked pattern now reads (worley scale cells with dark borders + per-cell tint,
  wider stripes, lower normal-map `bumpStrength` 1.5, calmer fog/volumetric/bloom in the demo).
  Curvature gain lowered to 3 in the demo so vertex-color shading darkens folds without blotching.
  Refs `docs/images/g30_creature_*.png` re-rendered + compressed.
- **G26 L-system leaves** were square quads; `L` now emits two crossed leaf-shaped kite quads
  (pointed tip at 1.35x leafSize along the heading, narrow base) — regression test
  `testLeafShape` asserts the tip extent. Refs `docs/images/g26_lsystem_*.png` re-rendered.
- **G26 L-system presets** drew vertical sticks instead of trees: the rules used only `+`/`-`,
  which in the 3D turtle is a roll around the heading and never changes the drawing direction
  (heading starts at +Y). Presets (`makeTree` variants 0-2, `makeFern`, `makeBush`) now combine
  roll with pitch (`&`) — e.g. `F -> FF+[+&F-FL]-[-&F+FL]` — so branches spread in 3D.
  Regression test `testPresetsSpread3D` asserts every preset's horizontal spread > 30% of its
  height. Reference screenshots `docs/images/g26_lsystem_*.png` re-rendered (crown closeup
  camera reframed); tutorials en/de document the roll-vs-pitch pitfall.

### Changed

- Re-compressed all 86 reference screenshots in `docs/images/` losslessly with
  `docs/compress_png.py` (per-row filter + zlib 9): 57.5 MB -> 18.1 MB, pixels byte-identical
  (verified by SHA-256 over decoded RGBA and by re-rendering G24/G27/G28/G29 references at
  PSNR >= 28 dB), so the CI perceptual diff is unaffected.

### Added

- **G12 — asset creator Qt tool + MCP layer** (design: [docs/ASSET_CREATOR.md](docs/ASSET_CREATOR.md)):
  - Qt-free MCP library module `mcp/` (`include/fuhglgraphics/mcp/`, `src/mcp/`): `mcp::jsonrpc`
    (JSON-RPC 2.0 line-delimited build/parse/framing/id-matching on `io::Json`),
    `mcp::tool_catalog` (`toolsListJson()`/`callTool()` over a `mcp::ToolHost`) and
    `mcp::mcp_server` (`handleLine`/`runServer`: initialize, notifications/initialized,
    ping, tools/list, tools/call). The catalog exposes **scene ops** (`scene_*`),
    **deferred-render settings** (`render_list_effects`, `render_set_effect`,
    `render_get_settings`, `render_set_param(s)` — every `render::Effect` and
    `scene::EffectParams` field), **G13 water** (`water_add_to_scene`, `water_get_params`,
    `water_set_params`) and the **procedural functions G24–G31** (`procedural_noise`,
    `procedural_marching_cubes`, `procedural_lsystem`, `procedural_wfc`,
    `procedural_voronoi`, `procedural_sdf`, `procedural_creature`, `procedural_worldgen`;
    `attach:true` adds generated meshes as scene nodes via the host's GL resolvers).
  - `tools/mcp_server/` → Qt-free executable `fuhglgraphics_mcp_server` (stdio MCP server,
    headless: data tools fully, attach reports `attached:false` without GL).
  - `tools/asset_creator/` → Qt6 tool `fuhglgraphics_asset_creator` (CMake option
    `FUHGLGRAPHICS_BUILD_TOOLS`, qmake `asset_creator.pro`): MVC — scene tree + inspector
    (TRS, per-node effect toggles, EffectParams, G13 water params) + asset browser (left
    dock), G3 `DeferredRenderer` viewport with orbit camera + `scene::pick` selection
    (center), MCP chat (right dock; external server command via settings dialog or
    `builtin` = in-repo catalog in-process; `/tool {json}` direct calls; `assetAction`
    results → QUndoCommands). `.fasset` JSON projects (`scene::serialize` + `io::writeJson`),
    UI multilingual (en default, de). CI: stdio MCP smoke + `xvfb-run ... --version`.
  - `scene::Node::removeChild`/`takeChild` (removal with undo), public effect-JSON helpers
    `scene::effectsToJson`/`effectsFromJson`/`setEffectParam`/`effectParamToJson`/
    `effectParamNames` in `scene/serialize.hpp` (also the G11 JSON render config).
  - G11 part: `io::AssetCache` (`include/fuhglgraphics/io/asset_cache.hpp`) — on-disk asset
    cache keyed by FNV-1a 64 content hash; used for `<project>/.cache`.
  - Tests: `tests/test_mcp.cpp` (label `unit`, no GL/Qt) — jsonrpc round-trips, the whole
    catalog incl. attach path, the stdio server end-to-end, AssetCache. Tutorial
    `docs/en|de/tutorials/g12_asset_creator.md`.
- **G31 — world generator helper** (`include/fuhglgraphics/procedural/worldgen.hpp`,
  `src/procedural/worldgen.cpp`): `procedural::generateWorld(WorldGenParams)` builds either a
  landscape or a city. Landscape pipeline: macro relief from layered noise (domain warp +
  continentalness mask + ridged multifractal + plains fBm), hydraulic droplet erosion (fills a
  `flowmap`) + thermal talus erosion, RGBA biome splatmap (R grass / G rock-by-slope / B sand +
  riverbeds / A snow-by-altitude), poisson-disk scattering of L-system trees, bushes and rocks
  weighted by the splatmap; `heightAt()` world-space sampling. City (G27 WFC layout from
  `makeCityTiles()`, merged road slabs, per-block procedural houses from the `HouseGenerator`
  class in `procedural/house.hpp` — gable/shed/hipped/flat roofs, bay windows, dormers, annex,
  porch, Fachwerk, highrise window grid — plus G28 voronoi district plates and parks), and the
  `procedural::generateCityWorld(CityGenParams)` pipeline in `procedural/citygen.hpp`
  (terrain+zones → warped road graph → OBB lot subdivision with road-access check → houses).
  Pure CPU, deterministic per seed; result `GeneratedWorld`/`CityWorld` attaches to a
  `scene::Scene` via `Mesh::setMeshData` + node transforms (splatmap as terrain albedo texture,
  grass via GPU instancing). Unit tests `tests/test_procedural_{worldgen,house,citygen}.cpp`,
  demo `examples/worldgen_screenshot` (landscape/city/city-street/pipeline headless PNGs), CI
  docs-screenshots step + lossless optipng compression, references
  `docs/images/g31_{worldgen_{landscape,city,city_street},pipeline{,_street}}.png`,
  tutorials en/de. Requirement entry G31 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).
  Extensions on top: **PlantGenerator** (`procedural/plant.hpp`, L-system based:
  Deciduous/Fir/Birch trees + bushes with per-kind rules, palettes incl. white birch bark and
  conical fir crown squash; used by the landscape scatter and city parks);
  **HouseGenerator** facade material selectable (`FacadeMaterial::Brick/Plaster/Stone` with
  plinth band / relief courses, `Params::facade` or random), roof gutter + downpipe, window
  shutters, separate reflective `HouseMesh::glass` pane mesh, garage with roller door, balcony
  with balcony door, slightly irregular walls (`wallIrregular` per-vertex jitter);
  **roads**: `RoadSurface` (Asphalt/Cobble/Paving/Dirt) + `roadIrregularity` + `roadDamage`
  (potholes) + `wetRoads` echo in the result, `wfcRoadsConnected()` and a largest-component
  repair so streets are never interrupted by paths unless `allowPathCuts = true`; street slabs
  are split from pavements/sidewalks (`GeneratedWorld::roads`/`pavements`,
  `CityWorld::roadMesh`/`sidewalkMesh`) so the app can texture only the streets (cobble with POM);
  `PlacedMesh` carries `glassMesh` + `facade`; the demo wires per-facade POM walls, a reflective
  glass entry, the selectable road surface (`--surface`, asphalt default; CI also renders a gated
  cobble+wet showcase) and wet reflective streets (`--wet`, SSR + roughness 0.12/metallic 0.4). Unit tests
  `tests/test_procedural_{worldgen,house,citygen,plant}.cpp`. Landscape upgrade: Minecraft-style
  three-layer relief — continentalness mapped through a new `Spline1D` curve
  (`contOcean`/`contCoast`/`contPlains` knots), an erosion map blending smooth fBm with ridged
  multifractal, and a peaks&valleys ridged layer (`pvScale`/`pvWeight`) — plus optional
  terracing (`terraceSteps`/`terraceStrength`), rivers (`riverCount`: gradient descent from
  mountain sources, V-carved channels into heightmap+flowmap, sinks above the water level filled
  to their spill point as mountain lakes in `pondMask`), and climate/biome maps
  (`temperature`/`humidity`/`biome` via a Whittaker lookup with `lapseRate`; scatter species and
  density follow the biome). The demo colors the vegetation splat channel with a soft Whittaker
  blend and adds strata bands to rock; new tests: Spline1D, climate/rivers, terracing
  (1,648,832 checks, 0 failures). New `--mode village` demo scene: big landscape + a small
  procedural village (HouseGenerator houses on a flattened lake-shore plateau) rendered with
  water, CSM shadows, IBL, HBAO, SSR, bloom, volumetric fog and wetness — CI-gated ref
  `docs/images/g31_worldgen_village.png`; the landscape shot keeps the low lake-shore vantage
  (sky + mountains + lake band + shore) with scatter cleared around the camera so no tree can
  cross the near plane (that rendered as a green star blob in earlier refs).
- **G30 — procedural creatures** (`include/fuhglgraphics/procedural/creature.hpp`,
  `src/procedural/creature.cpp`): `Skeleton` graph (nodes with position/radius/`BodyPart`,
  `finalize()` bakes world-space ellipsoid + tapered-capsule prims with broadphase AABBs, linear
  cache-friendly evaluation); `creatureSdf()` joins prims via G29 `sdf::smoothMin` and adds G24
  simplex fBm detail (scales/ribs/folds in the geometry); `meshCreature()` meshes via G25
  marching cubes with analytic SDF-gradient normals, per-vertex curvature (SDF Laplacian)
  written into the vertex color channel (the G-buffer pass multiplies vColor into albedo, so
  creases darken and ridges brighten automatically) and a seamless cylindrical UV unwrap;
  `bakeCreatureMaps()` bakes albedo + tangent normal maps from 3D object-space noise
  (worley scale cells, stripes, lighter belly — seamless in u). Presets `makeQuadruped()` and
  `makeReptile()`. Unit tests `tests/test_procedural_creature.cpp` (incl. curvature-sign test on
  a two-sphere dumbbell), creature case in `tests/gl_smoke_procedural.cpp`, demo
  `examples/creature_screenshot` (dusk sun + CSM + god rays, sky + IBL, SSAO, bloom,
  volumetric fog, TAA + ACES) with references `docs/images/g30_creature_{wide,closeup}.png`,
  CI step, tutorials en/de. Requirement entry G30 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G28 — Voronoi diagrams** (`include/fuhglgraphics/procedural/voronoi.hpp`,
  `src/procedural/voronoi.cpp`): `procedural::voronoi2d(n, min, max, seed)` builds a seeded 2D
  Voronoi diagram — one convex polygon per seed (half-plane clipping against every bisector,
  robust for moderate n) plus adjacency edges; `voronoiDistances()` gives F1/F2 (border =
  F2-F1); `extrudeCells(v, y0, y1, gap, heightJitter, jitterSeed, color, colorJitter)` extrudes
  cells into prism `MeshData` (gap shrinks polygons towards their centroid → cracks, height
  jitter → crystal shards). Unit tests `tests/test_procedural_voronoi.cpp`: cells contain their
  seeds, convex, in bounds, tile the rect (area sum), adjacency, F1/F2 correctness,
  extrusion validity + determinism + full-rect bbox at gap=0. Demo
  `examples/voronoi_screenshot`: shattered plaza (70 cells with gaps over an emissive underlay
  → glowing cracks + bloom) + crystal-shard cluster (height jitter), sunset sky cubemap + IBL,
  sun with CSM shadows + god rays, SSAO + volumetric fog, TAA + upscale + ACES; references
  `docs/images/g28_voronoi_{shattered,crystals}.png`. Tutorial
  `docs/en/tutorials/g28_voronoi.md` + German translation. Requirement entry G28 in
- **G29 — SDF / 3D fractals (Mandelbulb)** (`include/fuhglgraphics/procedural/sdf.hpp`,
  `src/procedural/sdf.cpp`): `procedural::sdf` distance primitives (sphere, box, roundedBox,
  plane, cylinder), `smoothMin` (polynomial smin) and `mandelbulb` (distance estimator, power 8,
  bailout 2); `procedural::meshFromSdf(field, min, max, resolution, color)` samples the field
  into a `VolumeGrid` and polygonises it with the G25 marching cubes at isolevel 0 -> indexed
  `MeshData`. Unit tests `tests/test_procedural_sdf.cpp`: primitive signs/values, smooth-min
  properties, DE sanity (origin ~0, far point large, finite), sphere SDF -> closed manifold mesh
  with correct shell radius, mandelbulb mesh bounded + finite + indexed. Demo
  `examples/mandelbulb_screenshot`: Mandelbulb (56^3 samples, ~22.5k tris) scaled x5 as a
  floating emissive sculpture + 3 satellite bulbs, violet core point light (bloom), warm low
  sun with CSM shadows + god rays, dusk sky cubemap + IBL, SSAO + volumetric fog, TAA + upscale
  + ACES; references `docs/images/g29_mandelbulb_{wide,closeup}.png`. Tutorial
  `docs/en/tutorials/g29_mandelbulb.md` + German translation. Requirement entry G29 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G27 — wave function collapse (tiled)** (`include/fuhglgraphics/procedural/wfc.hpp`,
  `src/procedural/wfc.cpp`): `procedural::Wfc` over a width x height grid. Tiles
  (`WfcTile`) carry 4 edge tags in cyclic dir order (E, S, W, N); rotations map
  `rotatedTags(rot)[d] = tags[(d + rot) mod 4]`; adjacency = equal touching tags; the grid
  border is a fixed tag (default "w", so dungeons ring themselves in walls). `solve(seed,
  maxBacktracks)` = min-entropy cell selection (weighted random collapse) + AC-3-style
  constraint propagation + seeded backtracking on contradiction; deterministic per seed.
  Preset `Wfc::makeDungeonTiles()` (floor, wallH/wallV bands, wallCap, corner, doorH/doorV,
  pillar). Unit tests `tests/test_procedural_wfc.cpp`: rotation mapping, compatibility,
  full-solve adjacency validity incl. border, determinism, dungeon structure (walled ring,
  open floor), contradiction termination. Demo `examples/wfc_screenshot`: 14x14 dungeon
  solved by WFC, instantiated as merged per-(tile,rot) scene nodes, torches (emissive +
  point lights + bloom), wear decals (G8), dim directional light with CSM shadows, SSAO,
  volumetric fog, TAA + upscale + ACES; references `docs/images/g27_wfc_{dungeon,overview}.png`.
  Tutorial `docs/en/tutorials/g27_wfc.md` + German translation. Requirement entry G27 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G26 — L-systems (Lindenmayer) → mesh** (`include/fuhglgraphics/procedural/lsystem.hpp`,
  `src/procedural/lsystem.cpp`): `procedural::LSystem` with axiom + production rules +
  iterations; `derive()` rewrites the string deterministically; `generate()` interprets it
  with a 3D turtle (orthonormal frame; F/f draw/move, +− roll, &^ pitch, \| yaw, [ ] push/pop
  with radius taper per depth, L = two crossed leaf quads) and emits `fuhgl::render::MeshData`
  (tapered `sides`-gon cylinders per segment + leaf quads, 11 floats/vertex) — ready for
  `Mesh::setMeshData` + `MeshComponent`. Presets: `LSystem::makeTree(variant 0..2)`,
  `makeFern()` (Barnsley-style), `makeBush()`. Unit tests `tests/test_procedural_lsystem.cpp`:
  rule rewriting (exact strings, determinism), balanced brackets of all presets, mesh validity
  (finite, indexed, unit normals, upward growth bounds), presets differ, leaf/trunk vertex
  colors. Demo `examples/lsystem_screenshot`: 9 L-system trees (3 variants) + ferns on a
  G24-noise terrain, 3000 instanced grass blades snapped via `Instancing::heightFn`, noon sun
  with CSM shadows + god rays, day-sky cubemap + IBL, SSAO + volumetric fog, TAA + upscale +
  ACES; references `docs/images/g26_lsystem_{forest,tree}.png`. Tutorial
  `docs/en/tutorials/g26_lsystem.md` + German translation. Requirement entry G26 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G25 — marching cubes (voxel → mesh)** (`include/fuhglgraphics/procedural/marching_cubes.hpp`,
  `src/procedural/marching_cubes.cpp`): `procedural::VolumeGrid` + `sampleField(field, min, max,
  nx, ny, nz)` sample a scalar volume; `marchingCubes(grid, isolevel, color)` runs the classic
  33-case Lorensen & Cline polygonisation with linear edge interpolation, central-difference
  gradient normals (interpolated per edge) and a lattice-edge vertex cache, so the result is a
  watertight indexed `fuhgl::render::MeshData` (11 floats/vertex) ready for
  `Mesh::setMeshData`. `marchingCubesChunked(...)` splits large volumes into chunks that share
  their boundary point planes (exact seams, every cell processed once). The canonical triTable
  (public-domain reference, Paul Bourke) is validated by the unit tests (per-config crossing-edge
  coverage, ≤5 triangles) plus geometric checks (sphere → closed manifold, Euler char 2, outward
  normals; plane → exact triangle count; chunked triangle count == single). Tests:
  `tests/test_procedural_mc.cpp` (unit) + `tests/gl_smoke_procedural.cpp` (label `gl_context`;
  noise-metaball blob rendered through the G3 renderer, pixels verified). Demo:
  `examples/marching_cubes_screenshot` — cave terrain from a G24-noise volume (112^3, chunked
  4x1x4, ~115k triangles) + emissive crystal blobs with bloom, dusk sky cubemap + IBL, sun with
  CSM shadows + god rays, SSAO + volumetric fog, TAA + upscale + ACES; references
  `docs/images/g25_marching_cubes_{cave,blob}.png`. Tutorial:
  `docs/en/tutorials/g25_marching_cubes.md` + German translation. Requirement entry G25 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G24 — procedural noise module (CPU)** (`include/fuhglgraphics/procedural/noise.hpp`,
  header-only `procedural::Noise` value type, deterministic per seed): value noise 2D/3D,
  Perlin 2D/3D (delegates to `fuhgl::math::Noise`), Simplex 2D/3D (Gustavson scaling),
  fBm / ridged / turbulence over a selectable `Base`, domain warp (`warp2`/`warp3`) and a
  `height2` terrain field in [0, 1]. Shared noise source for the G25-G29 generators (marching
  cubes volumes, L-system jitter, WFC seeds, voronoi jitter, SDF fields) and usable for
  heightmap carving (`TerrainComponent::heights` + `rebuildLod()`), sky cubemaps and
  textures. Unit tests: `tests/test_procedural_noise.cpp` (determinism, ranges, Perlin
  zero at integer lattice, fractal ranges, continuity, warp bounds). Demo:
  `examples/noise_screenshot` — terrain carved with domain-warped ridged fBm + carved lake
  basin, G13 lake water, cloud-sky cubemap from the same `Noise` + IBL, warm low sun with
  CSM shadows + god rays, SSAO + volumetric fog + wetness + bloom, TAA + upscale + ACES;
  references `docs/images/g24_noise_{wide,ridge}.png`. Tutorial:
  `docs/en/tutorials/g24_procedural_noise.md` + German translation. Requirement entry G24
  in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G23 — tonemapping operators + adjustable bloom threshold**
  (`include/fuhglgraphics/render/tonemap.hpp`, `uOperator` in `kTonemapFrag`,
  `DeferredRenderer::tonemapOperator`, `EffectParams::tonemapOperator`): the final pass now
  supports `render::TonemapOperator` — ACES (default, previous behavior), Reinhard, Uncharted2
  (Hable, white point 11.2) and linear — selected per frame; gamma 1/2.2 is applied after the
  curve as before. Pure CPU twins (`tonemapAces/Reinhard/Uncharted2/Linear`, `tonemapApply`,
  name round-trip) are unit-tested. The G17 bloom threshold stays adjustable per frame via
  `bloomThreshold`. Tests: `tests/test_tonemap.cpp` (unit) + `tests/gl_smoke_tonemap.cpp`
  (label `gl_context`; HDR scene with all four operators — every pair of frames differs
  measurably, no GL error). Demo: `examples/tonemap_screenshot` (dusk plaza with HDR lamps,
  one PNG per operator + ACES with bloom threshold 4) headless to PNG; references
  `docs/images/g23_tonemap_{aces,reinhard,uncharted2,linear,bloomthr4}.png`. Tutorial:
  `docs/en/tutorials/g23_tonemap.md` + German translation.
  Requirement entry G23 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G22 — wetness / rain on PBR materials** (`include/fuhglgraphics/render/wetness.hpp` pure
  helpers, `kLightFragWetness` block in the G3 lighting fragment shader, `Effect::Wetness`
  (kEffectCount = 21, off by default), `EffectParams::wetness*`): with the effect on, the
  lighting pass modulates the PBR inputs per pixel — up-facing surfaces (`N.y`) get a glossy
  water film (roughness mixed toward 0.06) and a ~35 % darker albedo at full wetness, and a
  deterministic 2D-noise puddle mask over world xz deepens depressions (`wetnessPuddle`: 0 =
  flat film, 1 = puddle spots only). Additionally `wetnessAmount` is added to the SSR strength
  in the G16 pass (wet surfaces reflect more; stacks with `ssrWetnessBoost`). Pure CPU twins
  (`wetnessPuddleMask`, `wetnessFactor`, `wetnessRoughness`, `wetnessAlbedoScale`) are
  unit-tested. Tests: `tests/test_wetness.cpp` (unit) + `tests/gl_smoke_wetness.cpp` (label
  `gl_context`; plaza with SSR, dry vs wet measurably different, no GL error). Demo:
  `examples/wetness_screenshot` (dry/wet/puddles) headless to PNG; references
  `docs/images/g22_wetness_{dry,wet,puddles}.png`. Tutorial:
  `docs/en/tutorials/g22_wetness.md` + German translation.
  Requirement entry G22 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G21 — volumetric fog / clouds** (`include/fuhglgraphics/render/volumetric.hpp` pure
  helpers, `kVolumetricFrag` + "volumetric" post pass in the G3 renderer, `Effect::VolumetricFog`
  (kEffectCount = 20, off by default), `EffectParams::volumetric*`): after the particles pass
  and before bloom (hdrP -> hdrV -> hdrB), each pixel's view ray is reconstructed from the
  G-buffer depth (sky marches to `volumetricMaxDist`) and raymarched in `volumetricSteps`
  samples through a 3D-noise volume — exponential height falloff (`volumetricHeightFalloff`)
  times fBm clouds (`volumetricCloudiness`, `volumetricNoiseScale`, `volumetricTime` drift) —
  accumulating transmittance and Henyey–Greenstein sun scattering (`volumetricAnisotropy`) plus
  sky ambient; output `hdr * T + inscatter`. Geometry and sky are fogged. Pure CPU twins
  (`volumetricHash/Noise/Fbm/Density`, `henyeyGreenstein`, `volumetricMarch`) are
  unit-tested. Tests: `tests/test_volumetric.cpp` (unit) + `tests/gl_smoke_volumetric.cpp`
  (label `gl_context`; fog on vs off measurably different, no GL error). Demo:
  `examples/volumetric_screenshot` (terrain scene, off/on/dense) headless to PNG; references
  `docs/images/g21_volumetric_{off,on,dense}.png`. Tutorial:
  `docs/en/tutorials/g21_volumetric_fog.md` + German translation.
  Requirement entry G21 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G20 — terrain + LOD integration** (`scene::TerrainComponent`,
  `include/fuhglgraphics/scene/component.hpp`, `src/scene/component.cpp`): owns a FuhGLCore
  F38/F41 `terrainLods()` LodMesh (procedural heightmap + `levels` grid resolutions, switch
  distances from `LodMesh::defaultDistances` or `customDistances`). `create()` needs a GL
  context; `heightAt(x, z)` is a bilinear CPU heightmap sample for placing objects; `fill()`
  appends one draw item with `lod` set, so the G3 renderer resolves the level per frame from
  the camera distance (`resolveLodMesh`; `SceneView::lodEnabled = false` forces level 0) and
  `DeferredRenderer::render()` now propagates the chosen level back to the caller's
  `SceneView::lastLodLevel`. The terrain node also contributes its bounding-sphere AABB to
  frustum culling (`Node::computeAabb`). Any `MeshEntry::lod` works the same way. Tests:
  `tests/test_terrain_lod.cpp` (unit: selectLevel/defaultDistances/resolveLodMesh/heightAt/
  fillFrame path) + `tests/gl_smoke_terrain.cpp` (label `gl_context`; near camera -> level 0,
  far -> level > 0, lod off -> -1). Demo: `examples/terrain_screenshot` (near/far PNGs with
  printed levels) headless to PNG; references `docs/images/g20_terrain_{near,far}.png`.
  Tutorial: `docs/en/tutorials/g20_terrain_lod.md` + German translation.
  Requirement entry G20 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G19 — scene serialization** (`fuhglgraphics::scene::serialize`,
  `include/fuhglgraphics/scene/serialize.hpp`, `src/scene/serialize.cpp`): Scene <-> `io::Json`
  round-trip — node hierarchy (name + TRS), per-node effects (list + params), instancing and the
  pure-data components (MeshComponent entries with material params/materialId/castShadow/
  localOffset, LightComponent, DecalComponent, AnimationComponent clips) — plus
  `saveScene`/`loadScene` for .json files. GL meshes are stored as JSON mesh specs
  (e.g. `{"shape":"box","halfExtents":[..]}`) and re-created on load through a caller-provided
  `MeshResolver` into a mesh arena; `defaultShapeResolver` covers the procedural box/quad
  shapes. `io::writeJson()`/`io::writeJsonFile()` added to `io/json.*` (pretty printer, escapes,
  shortest round-trip numbers). Basis for G12 `.fasset` project files. Tests:
  `tests/test_serialize.cpp` (unit: writeJson/parseJson round-trip incl. escapes/unicode/numbers,
  full scene JSON round-trip equality, save/load file round-trip, malformed input rejected) +
  `tests/gl_smoke_serialize.cpp` (label `gl_context`; save -> patch box spec -> load with
  defaultShapeResolver -> render, checks items/lights + non-dark frame). Demo:
  `examples/serialization_screenshot` (original vs. loaded-from-file PNGs match, round-trip
  JSON equal) headless to PNG; references `docs/images/g19_serialization_{original,loaded}.png`.
  Tutorial: `docs/en/tutorials/g19_serialization.md` + German translation.
  Requirement entry G19 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G18 — picking / ray-casting on the scene graph** (`fuhglgraphics::scene::picking`,
  `include/fuhglgraphics/scene/picking.hpp`, `src/scene/picking.cpp`): pure CPU module (no GL) —
  `rayFromScreen()` (world ray through a pixel), `rayIntersectAabb()` (slab test, culls node
  subtrees by their world AABBs), `rayIntersectTriangle()` (Möller–Trumbore), and
  `scene::pick()` returning the nearest `PickHit` (node, mesh entry, distance, world point,
  triangle). Triangle tests run against `MeshEntry::meshData` — the CPU `MeshData` copy that
  the glTF loader now sets for every primitive (and that apps set for procedural meshes).
  Basis for editor selection/gizmos (G2/G11/G12). Tests: `tests/test_picking.cpp` (unit: ray
  construction, AABB/triangle intersection, full pick near/far/miss/null-camera) +
  `tests/gl_smoke_picking.cpp` (label `gl_context`; center-pixel pick on a rendered scene +
  marker visibility). Demo: `examples/picking_screenshot` (plaza crates, marker at the hit
  point) headless to PNG; references `docs/images/g18_picking_{marker,nomarker}.png`.
  Tutorial: `docs/en/tutorials/g18_picking.md` + German translation.
  Requirement entry G18 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G17 — bloom** (`include/fuhglgraphics/render/bloom.hpp`, `kBloomFrag` in
  `src/render/deferred_glsl.hpp`, "bloom" post-graph pass in the G3 renderer): after the
  particles pass and before TAA, the HDR image gets a per-channel bright-pass above the
  adjustable `bloomThreshold`, a 9-tap golden-angle blur (`bloomRadius` texels) and is added
  with `bloomStrength` (input `hdrP`, output `hdrB` consumed by the TAA pass, so the halo is
  temporally stabilized). Pure helpers `bloomBrightPass()` / `bloomTaps()` (unit-tested). New
  `render::Effect::Bloom` (kEffectCount = 19, off by default), `EffectParams::bloom*`. Tests:
  `tests/test_bloom.cpp` (unit) + `tests/gl_smoke_bloom.cpp` (label `gl_context`; HDR lamp in
  a dark scene: halo ring appears with bloom on, suppressed with threshold 100). Demo:
  `examples/bloom_screenshot` (dusk plaza, HDR lamps + sun disk) headless to PNG; references
  `docs/images/g17_bloom_{on,off,thr4}.png` (re-rendered g15/g16 references with frame-level
  effect params). Tutorial: `docs/en/tutorials/g17_bloom.md` + German translation.
  Requirement entry G17 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G16 — screen-space reflections (SSR)** (`include/fuhglgraphics/render/ssr.hpp`,
  `kSsrFrag` in `src/render/deferred_glsl.hpp`, "ssr" post-graph pass in the G3 renderer):
  after the lighting pass the reflection vector is marched in world space against the
  G-buffer depth (linearized compare, thickness band, binary refine on hit) and the hit HDR
  color is added masked by fresnel × (1−roughness)² × screen-edge fade × distance fade
  (pure helpers `ssrEdgeFade` / `ssrMask`, unit-tested). The pass reads `hdr/gNormal/gDepth/
  gPbr` and writes `hdrS`, which then feeds the water and particles passes. New
  `render::Effect::Ssr` (kEffectCount = 18, off by default), `EffectParams::ssr*`,
  `DeferredRenderer::ssrWetnessBoost` (G22 adds wetness on top). Tests: `tests/test_ssr.cpp`
  (unit) + `tests/gl_smoke_ssr.cpp` (label `gl_context`; mirror floor + emissive box: the
  floor region is brighter with SSR on). Demo: `examples/ssr_screenshot` headless to PNG;
  references `docs/images/g16_ssr_{on,off}.png`. Tutorial: `docs/en/tutorials/g16_ssr.md` +
  German translation. Requirement entry G16 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G15 — SSAO** (`fuhglgraphics::render`, `include/fuhglgraphics/render/ssao.hpp`,
  `src/render/ssao.cpp`): a new post-graph pass `ssao` runs before the lighting pass —
  world-position reconstruction from the G-buffer depth, a deterministic hemisphere kernel
  (`ssaoKernel(samples, seed)`, per-pixel rotation via `ssaoRotation`, `linearizeDepth` for the
  depth compare, range check + bias) written into the `ssao` resource (RG16F, `.r`), sampled
  by the lighting pass on unit 6 (`uSsao`/`uSsaoEnabled`) multiplying the material AO. New
  `render::Effect::Ssao` (kEffectCount = 17, off by default), `EffectParams::ssa*`,
  `DeferredRenderer::texSsao()`. Tests: `tests/test_ssao.cpp` (unit: kernel/rotation/
  linearization) + `tests/gl_smoke_ssao.cpp` (label `gl_context`; SSAO on vs off differ and
  darken). Demo: `examples/ssao_screenshot` headless to PNG; references
  `docs/images/g15_ssao_{on,off}.png`. Tutorial: `docs/en/tutorials/g15_ssao.md` + German
  translation. Requirement entry G15 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G14 — GPU skinning** (`fuhglgraphics::anim::GpuSkinner`, `include/fuhglgraphics/anim/gpu_skinner.hpp`,
  `src/anim/gpu_skinner.cpp`): the G6 "future work" — a PIMPL class owning one skinned mesh's GPU side:
  a VAO with the bind-pose vertices (standard layout, attribs 0..3) plus JOINTS_0/WEIGHTS_0 as float4
  attributes (locations 5/6) and an SSBO with the joint matrix palette (binding 0).
  `DeferredRenderer` compiles a GLSL-430 skinning variant of the G-buffer vertex shader
  (`kGBufferVertSkin`, `skin = Σ wᵢ·uPalette[jᵢ]`) on GL >= 4.3 and draws the skinner instead of the
  plain mesh when the new `render::Effect::GpuSkinning` is enabled (capability-gated, pure
  `effectSupported()`; CPU fallback `skinMeshCpu` below 4.3 / when off). Scene wiring:
  `MeshEntry::gpuSkin` -> `FrameData::skins` (parallel to `view.items`) ->
  `DeferredRenderer::render(..., skins)`; `MeshEntry::meshData` stores the CPU `MeshData`
  (set by the glTF loader for every primitive — also used by G18 picking). glTF:
  `instantiateGltf()` creates one `GpuSkinner` per skinned primitive on GL >= 4.3
  (`GltfGpuResources::gpuSkins`). `render::Effect` gained `GpuSkinning` (kEffectCount = 16) +
  `effectFromName()`. Tests: `tests/test_render.cpp` (unit: registry/capability/effectFromName) +
  `tests/gl_smoke_gpu_skin.cpp` (label `gl_context`, skips with 77 below GL 4.3; two palettes
  differ with the effect on, identical with it off). Demo: `examples/gpu_skinning_screenshot`
  (robot arm bent via the palette on the GPU) headless to PNG; reference images
  `docs/images/g14_gpu_skinning_{on,off}.png`. Tutorial: `docs/en/tutorials/g14_gpu_skinning.md` +
  German translation. Requirement entry G14 in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

- **G13 — realistic water** (`fuhglgraphics::water`, `include/fuhglgraphics/water/water_surface.hpp`,
  `src/water/water_surface.cpp`): `water::WaterSurface` — a Gerstner-wave grid mesh
  (`create(resolution, size)`) with shading parameters (deep/shallow/foam colors, fresnel F0,
  reflection strength, distortion, crest + shore foam, sun glint), animated via `update(dt)`.
  The pure CPU twin `evalGerstner()` (displacement + analytic wave normal) and
  `reflectionMatrix()` / `waterPlaneFromModel()` are unit-tested (`tests/test_water.cpp`).
  G3 renderer extension (`render::Effect::Water`, `DeferredRenderer::render(..., water)`):
  a planar-reflection pass renders the scene (without the water) from the mirrored camera,
  clipped at the water plane, into an RGBA16F target (`reflectionTexture()`, resolution =
  internal size * `waterReflectionScale`); the water mesh stays out of the G-buffer and is
  shaded in a new "water" post pass of the HDR chain (lighting → water → particles → TAA →
  upscale → tonemap): Gerstner displacement + analytic normal in the vertex shader,
  fresnel mix of the planar reflection and the refraction (scene HDR, normal-distorted,
  depth-absorbed), wave-crest foam ("Gicht") + G-buffer-depth shore foam, sun glint, fog,
  manual depth test against the G-buffer depth. G9 integration: `scene::WaterComponent` →
  `FrameData::water` → `render(..., frame.water)`; the node transform places the water
  plane and the node AABB is padded by the max wave height. With the effect off, the water
  mesh falls back to plain G-buffer PBR. Tests: `tests/test_water.cpp` (unit) +
  `tests/gl_smoke_water.cpp` (headless GL, label `gl_context`, skips with 77 without GL).
  Demo: `examples/water_screenshot` (lake scene: terrain basin, rocks, shore trees, cloud
  sky + IBL, sun, fog) headless to PNG; reference images `docs/images/g13_water_*.png`.
  Tutorial: `docs/en/tutorials/g13_water.md` + German translation. Requirement entry G13
  in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

### Changed

- **G13 water tuning**: sharper surface reflection — the water shader now adds a crisp
  sky-cubemap mirror term (sampled by the reflected ray, plus the sun disk, weighted by the
  full fresnel) on top of the planar reflection, the reflection target resolution raised
  (`waterReflectionScale` 0.5 → 0.75), slightly higher base reflectance (`fresnelF0`
  0.02 → 0.035) and lower distortion (`distortion` 0.12 → 0.02, `refractionDistortion`
  0.06 → 0.035). Much less foam: `foamCrestStart` 0.72 → 0.95, `foamCrestSoftness`
  0.18 → 0.04, `shoreFoamWidth` 1.2 → 0.3, `foamStrength` 1.0 → 0.3. Reference images
  re-rendered with the new defaults (`g13_water_*`, plus the other water scenes the CI
  screenshot diff compares: `cave_*`, `g24_noise_*`, `sunrise_*`) and all
  `docs/images/*.png` losslessly re-compressed with `docs/compress_png.py`
  (44 MB → 22 MB).

### Planned

- **G12 — asset creator Qt tool** — design agreed, implementation in phases after review:
  `tools/asset_creator/` (executable `fuhglgraphics_asset_creator`, built with
  `FUHGLGRAPHICS_WITH_QT=ON` + new option `FUHGLGRAPHICS_BUILD_TOOLS`) to create/load/edit
  assets: data-structures panel (scene tree + inspector + asset browser), viewport with
  editing tools on the G3 `DeferredRenderer`, LLM chat over MCP (stdio JSON-RPC 2.0,
  configurable server command). Project files are own `.fasset` JSON. Design doc:
  [docs/ASSET_CREATOR.md](docs/ASSET_CREATOR.md); requirement entry G12 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).

### Added

- **G2 editor building block — GL viewport widget** (`fuhglgraphics::editor::GlViewportWidget`,
  `include/fuhglgraphics/editor/gl_viewport_widget.hpp` + `editor/gl_viewport_widget.cpp`,
  built only with `FUHGLGRAPHICS_WITH_QT=ON`): minimal `QOpenGLWidget` that owns the GL context
  lifecycle (initializeGL/resizeGL/paintGL delegated to injected handlers — MVC view) plus
  on-demand rendering (`requestRender()` dirty flag), a `setMaxFps()` frame-rate limiter
  (paced `QTimer`), VSync via swap interval, MSAA + requested GL version on the surface format,
  device-pixel resize reporting, smoothed fps/frame-time stats via `frameRendered()` (G10 HUD
  input), `grabFrame()` screenshots, and `glInitialized()` / `glAboutToBeDestroyed()` lifecycle
  signals. PIMPL, no exceptions (bool + log). Unit test `tests/test_editor_widget.cpp`
  (offscreen, label `unit`, WITH_QT only).
- **G3 — deferred renderer** (`fuhglgraphics::render::DeferredRenderer`,
  `include/fuhglgraphics/render/deferred_renderer.hpp`, `src/render/`): extended G-buffer
  (albedo+normal, metallic/roughness/AO, HDR emissive, motion vectors, material ID — layout
  in `render/gbuffer.hpp`; optional MSAA with per-attachment resolve), PBR lighting reusing
  FuhGLCore lights (`LightBlock` UBO), `ShadowMap`/`CascadeShadowMap` shadows and IBL
  (`render::Environment`), plus its own post chain on `fuhgl::render::RenderGraph`:
  TAA (Halton jitter + motion-vector history) → FSR1-style EASU+RCAS upscaling
  (`setRenderScale`) → ACES tonemap, with per-effect toggles and F39/F40-style capability
  gating (`render/effects.hpp`). Per-item material IDs via `render(scene, &materialIds)`
  land in the G-buffer MaterialId attachment (preparation for G8 decals).
  Tests: unit tests for the pure parts (`tests/test_render.cpp`, label `unit`) + headless
  GL smoke test (`tests/gl_smoke_render.cpp`, label `gl_context`, skips with exit code 77
  without GL). Screenshot tool `examples/screenshot` renders the demo scene headless
  (surfaceless EGL) to PNG; reference images in `docs/images/`
  (`g3_deferred_1280x720.png`, `g3_deferred_upscale_0.6.png`, `g3_deferred_nofx.png`).
  G3 tutorial in `docs/en/tutorials/g3_deferred_renderer.md` + German translation.
  New CMake option `FUHGLGRAPHICS_BUILD_EXAMPLES` (default ON).
- **G9 — scene graph** (`fuhglgraphics::scene`, `include/fuhglgraphics/scene/`,
  `src/scene/`): `Scene` owns the root `Node`; nodes have a local TRS transform, children
  and a polymorphic component list (`attach<T>` / `find<T>` / `findAll<T>` / `detach`).
  `MeshComponent` holds **several `MeshEntry` meshes per node** — each with its own local
  offset, LOD, `castShadow` flag, material ID and material parameters incl. **albedo +
  normal-map textures per mesh** (explicit preset material or inline fields synced into an
  owned preset). `LightComponent` places a `fuhgl::render::Light` at the node's world
  transform; `CameraComponent` provides the camera for `fillFrame(frame, nullptr, w, h)`.
  Every node carries an **`Effects` object** (`std::vector<render::Effect>` +
  `EffectParams`): `Scene::fillFrame()` merges them per frame (union of the lists, the
  last-visited node's parameters win) and `Effects::applyTo(renderer)` pushes them onto the
  G3 `DeferredRenderer`. **Instancing is a node property** (`Node::instancing`:
  grid/random layouts via `makeLocalInstances()`, one instanced draw call per mesh entry).
  World matrices + world AABBs are maintained for **frustum culling**. `Scene::fillFrame()`
  produces a `FrameData` (SceneView + parallel `materialIds` / `castShadow` + merged
  `Effects`) that is rendered via `renderer.render(frame.view, &frame.materialIds,
  &frame.castShadow)` — the G3 renderer gained the per-item `castShadow` filter for the
  shadow depth pass and normal-map support in the G-buffer pass (derivative-based tangent
  frame, unit `kTextureUnitNormalMap`).
  Tests: `tests/test_scene.cpp` (unit: TRS/world matrices, effects merge, instancing
  layouts, components, fillFrame incl. culling) + `tests/gl_smoke_scene.cpp` (label
  `gl_context`, skips with 77). New tool `examples/scene_screenshot` renders the G9 demo
  scene headless to PNG with CLI effect switches; reference images `docs/images/g9_*.png`
  (`g9_scene_graph.png` all effects, `g9_no_shadows.png`, `g9_no_ibl.png`,
  `g9_upscale_scale0.6.png`, `g9_no_effects.png`). G9 tutorial in
  `docs/en/tutorials/g9_scene_graph.md` + German translation.
- **G3 extensions — sky cubemap + god rays**: `DeferredRenderer::setSkyCubemap(&cube)`
  renders a sky cubemap for background pixels in the lighting pass (unit
  `kTextureUnitSkyCube` = 11) instead of the gradient; `renderer.godRays` adds god rays
  in the tonemap pass (radial blur toward the sun's screen position, weighted toward the
  sun end, occlusion-tested against the G-buffer depth passed as a second pass input —
  geometry casts real light shafts; the sun disk comes from the first directional light).
  Covered by `tests/gl_smoke_scene.cpp` (sky cubemap sampled in the background; god rays
  strictly brighten the frame). New tool `examples/landscape_screenshot`: G9 landscape
  (heightmap terrain + ~170 two-mesh tree nodes, procedural fBm cloud sky cubemap also
  baked as IBL env, low warm sun, distance fog, CSM shadows, god rays) with CLI switches
  (`--no-rays/--no-fog/--no-clouds/--no-shadows/--no-ibl`); reference images
  `docs/images/g9_landscape_sky_sun_fog_rays.png` and `g9_landscape_no_rays.png`,
  rendered by the CI `docs-screenshots` job too.
- Initial skeleton with agreed requirements G1–G11 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) plus the dependency wiring:
  CMake ≥ 3.20 static library `fuhglgraphics` (alias/export
  `FuhGlGraphics::fuhglgraphics`, install + `find_package(FuhGlGraphics)`),
  direct FuhGLCore dependency resolved via
  `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>` (add_subdirectory) →
  `find_package(FuhGLCore)` → FetchContent fallback from
  `github.com/FuhMhanShu/FuhGLCore` (`FUHGLGRAPHICS_FUHGLCORE_GIT_TAG`, default
  `main`); optional Qt6 (`FUHGLGRAPHICS_WITH_QT=ON` default, OFF = Qt-free
  headless core); options `FUHGLGRAPHICS_BUILD_TESTS` /
  `FUHGLGRAPHICS_ENABLE_HARDENING`; version API
  (`include/fuhglgraphics/version.hpp`, `src/version.cpp`) + umbrella header
  `graphics.hpp`; mini test runner with unit tests (`ctest -L unit`);
  docs (README.md/README.de.md, docs/ with language picker, USER_GUIDE.md,
  DEVELOPMENT.md, Doxyfile, en/de tutorial placeholders), CI matrix
  (Linux with Qt6, win/mac Qt-free), SECURITY.md, .gitignore.
- qmake support: `FuhGlGraphics.pro` builds the standalone static library
  (`qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" FuhGlGraphics.pro && make` →
  `libfuhglgraphics.a`), pulling in FuhGLCore via `fuhglcore.pri`
  (`FUHGLCORE_DIR` as qmake var or environment, `error()` when unset);
  `FUHGLGRAPHICS_WITH_QT=1` (default) adds `QT += widgets openglwidgets` +
  `automoc` + `FUHGLGRAPHICS_WITH_QT=1`, `0` builds the Qt-free headless core.
  Source lists use per-directory globs (no `**`, which expands to nothing on
  some Qt6 qmake versions).
- **G4 — glTF 2.0 asset loading** (`fuhglgraphics::io`, `include/fuhglgraphics/io/`,
  `src/io/`): own minimal JSON parser + base64 (`io/json.hpp`, no nlohmann) and
  `io::loadGltf(path)` for .gltf (JSON) and .glb (binary container) — buffers (external
  .bin, embedded base64 data-URIs, GLB BIN chunk), bufferViews/accessors (all component
  types, normalized, strided, sparse), primitives (TRIANGLES/STRIP/FAN with
  POSITION/NORMAL/TEXCOORD_0/COLOR_0 + indices → FuhGLCore `render::MeshData`),
  `KHR_materials_pbrMetallicRoughness` (factors + baseColor/normal textures, alphaMode,
  doubleSided), images (external/embedded; PNG/JPEG → RGBA8 via Qt QImage when
  `WITH_QT=ON`, else factors only), samplers, nodes (TRS + matrix→TRS), scenes.
  `io::instantiateGltf(asset, scene, gpu, parent)` maps the default scene onto the G9
  scene graph with GL resources (one `render::Mesh` per primitive, preset materials with
  textures, one `MeshEntry` per primitive, `castShadow = true`, running material IDs).
  bool+log errors, no exceptions; no vendored third-party assets (tests use generated
  fixtures in `tests/gltf_fixture.hpp`). Tests: `tests/test_io.cpp` (unit) +
  `tests/gl_smoke_gltf.cpp` (label `gl_context`, skip 77; with `WITH_QT=ON` also a PNG
  decode roundtrip). New tool `examples/gltf_demo` (`--model x.glb --out shot.png ...`)
  renders any glTF model headless to PNG; reference images `docs/images/g4_*.png`
  (Kenney CC0 ship/chest, pinned commit — see the G4 tutorial). G4 tutorial in
  `docs/en/tutorials/g4_gltf_loader.md` + German translation.
- **G3 shadows on the FuhGLCore F45 shadow kit** (requires FuhGLCore >= `9fc991c`, PR #42
  "shadow kit"): `ShadowMap`/`CascadeShadowMap` are now thin wrappers over
  `ShadowTarget`; the G3 `DeferredRenderer` gained `setShadowFilter(ShadowFilter)`
  (Pcf3x3 legacy / PoissonPcf / Pcss / Vsm — VSM renders RGBA32F moments into an extra
  attachment), `shadowSnapToTexels` (texel-snapped cascade VPs, no swimming),
  `shadowCascadeBlend` (cross-fade near the splits), normal-offset bias, and the GLSL
  snippets are composed from `fuhgl/render/shadow_glsl.hpp` (`kSkFuncs`). New:
  `enablePointShadow()` (F46 `CubeShadowMap` for one point light, Effect::PointShadows)
  and `contactShadowsEnabled` (screen-space raymarched contact shadows against the
  G-buffer depth in the lighting pass, Effect::ContactShadows). `render::Effect` gained
  `PointShadows` + `ContactShadows` (kEffectCount = 9); `scene::EffectParams` gained
  shadowFilter/snap/cascadeBlend/lightSize/minVariance/normalOffset/pcfRadius, applied
  by `Effects::applyTo()`. Sky cubemap moved from texture unit 11 to 16 (kit uses 11–15).
  `examples/landscape_screenshot` gained `--shadow-filter/--cascade-blend/--light-size/
  --point-shadows/--contact-shadows/--no-snap`; new reference images
  `docs/images/g9_landscape_shadowkit_{pcss,vsm,pcf3x3,lamp}.png`; gl_smoke_render now
  exercises all filters + point/contact shadows. Docs: USER_GUIDE, REQUIREMENTS §6,
  G9 tutorials (en/de) embed the new screenshots; CI docs-screenshots renders them.
- **G5 — GPU particle system** (`fuhglgraphics::particles`, `include/fuhglgraphics/particles/`,
  `src/particles/`): `ParticleSystem` (PIMPL) with emitter + integrate compute shaders over
  an SSBO on GL >= 4.3 (same pattern as FuhGLCore's GpuCuller — the SSBO doubles as the
  divisor-1 instance-attribute buffer), rendered as instanced camera-facing billboards;
  on GL 3.3–4.2 the identical math runs on the CPU (`CpuSim`, pure/unit-tested) and streams
  into a dynamic VBO. The G3 DeferredRenderer gained a "particles" post pass (hdr -> hdrP,
  before TAA): additive/alpha blend, radial falloff, life fade, soft-particle depth fade
  against the G-buffer depth. New `render::Effect::Particles` (kEffectCount = 10); G9
  integration via `scene::ParticleEmitterComponent` -> `FrameData::particleSystems` ->
  `DeferredRenderer::render(..., &particleSystems)`.
  Tests: `tests/test_particles.cpp` (unit: CpuSim) + `tests/gl_smoke_particles.cpp`
  (gl_context: gpu() vs GL version, aliveCount, brighter emitter region, no GL error).
  New tool `examples/particles_screenshot` (`--kind fire|smoke|pollen|all`) with reference
  images `docs/images/g5_particles_{fire,smoke,pollen,all_nofx}.png`. G5 tutorial en/de;
  USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI docs-screenshots renders them.
- **G6 — animation/skinning** (`fuhglgraphics::anim`, `include/fuhglgraphics/anim/animation.hpp`,
  `src/anim/animation.cpp`): `AnimationClip`/`TrsChannel` (keyframe channels per node
  name; translation/scale lerp, rotation `fuhgl::math::slerp`), `AnimationPlayer`
  (`addClip`, `play(name, LoopMode::Once|Loop|PingPong, speed)`, `update(dt)`, `applyTo(scene)`
  / node-map overload — writes the sampled TRS into the named nodes; PingPong mirrors via
  the speed sign), `Skeleton` (joint node indices + inverse bind matrices,
  `updatePalette(jointNodePtrs)` → palette = jointWorld * IBM) and CPU skinning
  `skinMeshCpu(base, SkinWeights, palette, out)` (the requirement allows "CPU or GPU";
  a GPU matrix-palette SSBO in the G3 G-buffer pass on GL >= 4.3 is future work).
  G4 extension: glTF `animations[]` (channels/samplers → clips) and `skins[]` (joints +
  inverseBindMatrices) plus JOINTS_0/WEIGHTS_0 on primitives are parsed into
  `GltfAsset::{animations, skins, ...}`; `instantiateGltf()` maps them onto
  `GltfGpuResources::{player, skeletons, nodePtrs, skinnedMeshes}`. G9:
  `scene::AnimationComponent` holds an `anim::AnimationPlayer`.
  Tests: `tests/test_anim.cpp` (unit: sampling, loop modes, applyTo, skinMeshCpu) +
  `tests/test_io.cpp` extended (fixture now carries a clip + skin; index winding fixed to
  CCW) + `tests/gl_smoke_anim.cpp` (gl_context: fixture .glb → player/skeletons/
  skinnedMeshes, CPU-skinned vertex check, rendered frames at t=0 vs t=1 differ).
  New tool `examples/animation_screenshot` (`--out shot.png [--time 0.7] [--frames 16]
  [--no-shadows] [--no-rays] ...`): 2-joint skinned robot arm (CPU palette) + waving flag,
  reference images `docs/images/g6_animation_{bend,straight,nofx}.png`. G6 tutorial
  en/de; USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI docs-screenshots renders
  them.
- **G7 — light probes / SH irradiance GI** (`fuhglgraphics::probes`,
  `include/fuhglgraphics/probes/light_probes.hpp`, `src/probes/light_probes.cpp`):
  `LightProbeGrid` — regular grid of SH L2 irradiance probes over an AABB, baked on the
  CPU (`bake(BakeInput)`: fibonacci-sphere rays vs a world-space `ProbeTriangle` soup,
  one bounce = sky via `skyRadiance(dir)` callback + direct lights with shadow-ray
  occlusion; projected with `shProject`; `shIrradiance` evaluates E(n) = sum A_l c_lm
  Y_lm(n), constant radiance L -> E = pi*L, unit-tested). GL side (GL 3.3 baseline): the
  27 floats per probe are packed into seven RGBA16F 3D textures (units 17..23) and
  sampled with hardware trilinear interpolation in the G3 lighting pass; upload is lazy
  in `render()`. With `render::Effect::LightProbes` (kEffectCount = 11) the diffuse
  ambient becomes `kD * albedo * (E(N)/pi) * ao * lightProbeStrength` (IBL keeps its
  specular part) — diffuse indirect light / color bleeding. G9:
  `scene::LightProbeComponent` -> `FrameData::lightProbes` ->
  `DeferredRenderer::render(..., lightProbes)`; `EffectParams::lightProbeStrength`.
  Tests: `tests/test_probes.cpp` (unit) + `tests/gl_smoke_probes.cpp` (gl_context: upload,
  render on vs off differ, frame brighter with probes, no GL error).
  New tool `examples/probes_screenshot` (colored-wall courtyard, `--no-probes/
  --no-shadows/--no-rays`) with reference images `docs/images/g7_probes_{on,off,nofx}.png`
  (pixel-verified: sky unchanged, shadowed floor brighter, per-channel color bleeding).
  G7 tutorial en/de; USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI
  docs-screenshots renders them.
- **G8 — decals / projected textures** (`render::Decal` in
  `include/fuhglgraphics/render/decals.hpp`; decal pass in `src/render/deferred_renderer.cpp`,
  `kDecalVert/kDecalFrag` in `src/render/deferred_glsl.hpp`): oriented box decals projected
  into the G-buffer albedo + normal attachments between the geometry pass and the lighting
  pass. The fragment shader reconstructs the world position from the G-buffer depth, keeps
  pixels inside the box, gates on the G-buffer MaterialId (`materialIdMask`), projects onto
  the dominant face axis of the surface normal and mixes albedo/normal in. Sorted
  back-to-front, capped at `render::kMaxDecals` = 16 (one-time warning), after the MSAA
  resolve when MSAA is on. New `render::Effect::Decals` (kEffectCount = 12);
  `DeferredRenderer::render(..., decals)` trailing parameter. Pure `render::decalProject()`
  helper (unit-tested). G9: `scene::DecalComponent` -> `FrameData::decals`.
  Tests: `tests/test_render.cpp` (decalProject: containment, dominant-axis UV, rotated
  boxes) + `tests/gl_smoke_decals.cpp` (gl_context: mark appears only with the effect on,
  material-ID mask gates it, no GL error).
  New tool `examples/decals_screenshot` (plaza with bullet-hole decals on ground + wall and
  a red ring marking; `--no-decals/--no-shadows/--no-rays`) with reference images
  `docs/images/g8_decals_{on,off,nofx}.png` (pixel-verified: red marking present only with
  decals on). G8 tutorial en/de; USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI
  docs-screenshots renders them.
- **G10 — quick upgrades (roadmap 7)**, the remaining open items (MSAA wiring+test,
  CSM penumbra hardening and FSR1-style temporal upscaling already landed with G3/#4):
  - **DoF bokeh** (`render::Effect::Dof`, kEffectCount = 14): new post-graph pass "dof"
    between upscale and tonemap (`kDofFrag` in `src/render/deferred_glsl.hpp`,
    `DeferredRenderer::drawDof`) — CoC from the linearized G-buffer depth, hexagonal bokeh
    gather (6 full-ring + 6 half-ring taps); plain copy when off. Params
    `EffectParams::dofFocusDist/dofStrength/dofMaxCoC` via `Effects::applyTo()`.
  - **Mini debug HUD** (`render::DebugHud`, `include/fuhglgraphics/render/debug_hud.hpp`,
    `src/render/debug_hud.cpp`, PIMPL over FuhGLCore's TextRenderer with the embedded 8x8
    font): fps (FpsCounter), frame ms, per-effect on/off lines; `render::Effect::DebugHud`;
    drawn by `render()` after the post graph into `output()` (NOT as a graph pass —
    resolvePassOrder is LIFO for independent passes and would run it before tonemap);
    `renderer.hud().fps()` / `renderer.lastFrameMs()`.
  - **CI screenshot perceptual diff**: `docs/screenshot_diff.py` (pure python3, zlib +
    struct, no PIL) compares `docs/images/ci/*.png` against the checked-in references in
    `docs/images/` with a PSNR threshold and fails the `docs-screenshots` job below it.
  Tests: `tests/test_render.cpp` (kEffectCount = 14, effect names) +
  `tests/gl_smoke_g10.cpp` (gl_context: DoF on/off blurs the near crate, HUD changes the
  top-left pixels, hud created, no GL error).
  New tool `examples/dof_screenshot` (`--focus 9 --no-dof --hud ...`) with reference
  images `docs/images/g10_{dof_on,dof_off,hud}.png` (verified: foreground pillar blurred,
  background sharp; HUD panel readable top-left). G10 tutorial en/de;
  USER_GUIDE/REQUIREMENTS/READMEs/DEVELOPMENT updated; CI renders the G10 images and runs
  the screenshot diff.
