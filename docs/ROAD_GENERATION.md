# FuhGlGraphics — Organic Road / Path Network Generation (G38) design

Status: **implemented in `procedural/roadgen.{hpp,cpp}`, integrated into the G31 landscape
pipeline (`procedural/worldgen.cpp`).** Language of this document: English (docs at this
level are English-only, see [README.md](README.md)). The algorithms below are deliberately
**language- and engine-agnostic**: no code, no frameworks — only mathematics, data structures
and step-by-step procedures.

## 0. Scope and relation to existing modules

Straight lines drawn over a terrain look artificial. Real traffic networks follow the path
of least resistance, so G38 routes roads with an **A\* least-cost search whose movement cost
is the physics of the terrain**, then blends the result back into the world. G38 is the
missing link of the closed world loop in G31 `generateWorld(Landscape)`:

| World loop stage | Module | G38's role |
|---|---|---|
| Terrain & climate decide where settlements are | G31 heightmap / erosion / rivers / Whittaker biome maps | site suitability = flat + dry + climate-suitable + near water |
| Settlements generate roads | **G38 `RoadGenerator`** | hierarchy of highways / country roads / desire paths between sites |
| Roads shape terrain | G31 heightmap | bench cut (Hanganschnitt) + embankment (Damm) carved into the heightmap; bridges over water |
| Vegetation reacts to roads | G31 splatmap + G37 scatter | `roadMask` suppresses trees/rocks on the road, weeds (bushes) grow at the frayed edges |
| Creatures use paths as fast routes | app / G32 creature placement | `GeneratedWorld::roadDistanceAt(x, z)` distance field to the nearest road |

G27 WFC (city mode) and the G31 pipeline city (`citygen.hpp`) already have their own street
grids on flat ground; G38 targets the **landscape**, where terrain physics matter.

```
RoadInput (heightmap, flow, biome, sites)          RoadGenParams
        │                                                  │
        ▼                                                  ▼
1. hierarchy: majors (importance ≥ highwayMinImportance) ──Prim MST──▶ highways
   every other site ──nearest connected site──▶ country road
   every site ──random nearby land POI──▶ desire-path trails (each its own A*)
        │
        ▼
2. A* least-cost path per connection (8-connected grid, physics cost function below);
   each solved path is stamped into the "existing road" mask so later paths merge
   (cost *= 1 − existingRoadDiscount·mask)
        │
        ▼
3. Catmull-Rom resample (~1 cell step) + iterative min-curve-radius clamp per class
        │
        ▼
4. masks: road mask (noise-frayed edges), wheel-rut mask, bridge mask,
   multi-source Dijkstra distance field (world units)
        │                     │
        ▼                     ▼
   carveTerrain():        buildGeometry():
   bench/embankment       cross-section extrusion (profiles per class),
   into the heightmap     bridge decks + posts, props (milestones,
   (water + border       lanterns, signs at junctions, guard walls)
   ring untouched)
        │
        ▼
RoadNetwork: paths (smoothed polylines + bridge flags), roadMesh, bridgeMesh, props,
mask / rutMask / bridgeMask / dist, roadDistanceAt()
```

## 1. Route: least-cost path (A* with physics rules)

Grid = the terrain heightmap (row-major `x + gridW*z`). 8-connected A\*; the cost of moving
one step of length `d` (world units) into cell `b` is:

```
step = d · (1 + slopeWeight · slope(b)²)          // slope = world dh/dx of the heightmap
if slope(b) > critSlope(class):  step *= criticalPenalty   // ~15° for highways -> switchbacks
if h(b) < waterLevel:            // water: ford or bridge
    depth = waterLevel − h(b)
    step = (depth ≤ fordMaxDepth && flow(b) ≤ fordMaxFlow) ? d · fordCost   // Furt
                                                          : d · waterCost   // Bruecke/Steg
else:
    step *= swampCost   if flow(b) high and near the water level (Sumpf)
    step *= forestCost  if biome is Forest/Jungle/Taiga (dense woods)
    step *= rockCost    if biome Rock or slope(b) very high
if existingMask(b) > 0:  step *= 1 − existingRoadDiscount · existingMask(b)  // merge, not parallel
```

- **Slope is the dominant factor** and enters quadratically (`cost += slopeWeight·slope²`).
  Above the class critical slope (`critSlopeHighway ≈ 0.27` ≈ 15°, country `0.45`, trail
  `0.95`) the cost explodes (`criticalPenalty`), so A\* automatically proposes realistic
  **switchbacks (Serpentinen)** on steep hillsides — for trails the threshold is high, so
  they may go straight up.
- **Ford detection**: a water cell is cheap only when it is shallow (`depth ≤ fordMaxDepth`)
  **and** narrow (`flow ≤ fordMaxFlow`, the erosion flow accumulation as a width proxy) —
  a *Furt*. Everything deeper/wider costs `waterCost`; when the route is *forced* across
  such a cell (no affordable detour), the path points there are flagged `bridge`.
- **Existing-road discount** (`cost *= 1 − 0.85·mask`): new paths merge into the network
  instead of running parallel — the network grows organically.
- Heuristic: euclidean distance × `(1 − existingRoadDiscount)` (admissible, because every
  step costs at least `d·(1−discount)`). Expansion budget `maxExpand` per search.

## 2. Hierarchy of the network

1. **Highways** (`RoadClass::Highway`): Prim MST over the *major* sites
   (`importance ≥ highwayMinImportance`; if fewer than two, the top two are promoted).
   Widest road, lowest critical slope, largest min curve radius.
2. **Country roads** (`RoadClass::Country`): every remaining site connects to the nearest
   already-connected site.
3. **Desire paths** (`RoadClass::Trail`): `desirePathsPerSite` random land points at
   `desirePathMinLen..MaxLen` around each site (house ↔ well / forest edge / viewpoint).

## 3. Geometry: splines & cross-sections

- The raw A\* result is a jagged grid line → **Catmull-Rom resample** at ~one cell step
  (endpoint tangents clamped), then an **iterative min-radius clamp**: for every triple of
  consecutive points with circumradius `< minRadius(class)`, the middle point is pulled
  toward the chord midpoint (up to 8 passes).
- **Cross-section extrusion** along the spline (profile = list of `(t ∈ [−1,1], yOffset,
  isRut)` across the half width), emitted as a triangle strip per segment:
  - *Highway*: crowned carriageway (`y = crown·(1−(|t|/car)²)`, rain runs off) + shoulders +
    drainage ditches at the rim (7 profile points).
  - *Country road*: two wheel ruts at `t = ±rutOffset` (`−rutDepth`) with a grass ridge in
    the middle (5 points).
  - *Trail*: one narrow trodden depression (3 points).
- **Bridge runs**: points flagged `bridge` get a flat deck at
  `max(terrain, waterLevelWorld + 0.7)` in a separate `bridgeMesh`, plus posts down to the
  ground every few samples.
- Vertex colors carry the **material**: per class + local biome (mountains → stone slabs,
  forest country road → dirt, flatland highway → asphalt); rut vertices are darkened
  (`color·0.5`) so the shader can lower roughness there and **puddles form in the ruts**
  after rain. UVs are world-space (`pos·uvScale`) so textures tile along the road.

## 4. Terrain & environment blending (the realism factor)

- **Bench carving** (`carveTerrain`): every spline sample stamps a soft disc
  (`m = 1 − smoothstep(halfWidth·0.85, halfWidth+blendMargin, d)`) into the normalized
  heightmap: uphill cells are cut toward the centerline height (max `benchCut` —
  *Hanganschnitt*), downhill cells are filled (max `embankment` — *Damm*). Water cells and
  the outermost grid ring (island falloff) are never modified; bridges cross water instead.
- **Road mask**: `1` on the corridor, fading over `blendMargin`, and at the rim
  (`m ≤ 0.85`) multiplied by `0.45 + 0.95·noise(x,z)` — **noise-frayed edges** so grass,
  earth and stones blend organically in the terrain shader (G31 merges it into the splatmap:
  dirt replaces grass, frayed edges go into the sand channel).
- **Rut mask**: two narrow strips at `±rutOffset` along country roads/trails (puddles).
- **Props** along the spline with jitter: *milestones* every `milestoneEvery` on
  highway/country, *lanterns* every `lanternEvery` within `lanternRadius` of a site,
  *way signs* at junctions (closest sample pair of two different paths), *guard walls*
  where the ground drops more than `wallMinDrop` at the road edge.

## 5. World integration (G31 `generateWorld` landscape)

Pipeline order in `generateLandscape`: relief → erosion/terracing/rivers → splatmap →
climate maps → **settlements + G38 roads** → scatter.

- **Sites**: poisson-disk scatter weighted by `siteWeight` (dry, `1 − smoothstep(0.22, 0.55,
  slope)`, biome-suitable — Desert/Tundra penalized, Shore boosted, near-water bonus).
  `importance = 0.3 + 0.4·flatness + 0.3·waterProximity` drives the hierarchy.
- **Order matters**: `generate()` routes on the pre-carve heightmap → `carveTerrain()` cuts
  the benches → `buildGeometry()` extrudes the road meshes against the **carved** terrain
  (the road sits exactly on its bench).
- **Houses**: per settlement, `housesPerSettlement` `HouseGenerator` village houses in a
  ring around the center, rejected when on the road corridor
  (`roadDistanceAt < highwayWidth/2 + 2`) or in water — the road passes through the village
  center like a village street. Stored in `GeneratedWorld::buildings`, ranges recorded in
  `GeneratedWorld::settlements`.
- **Vegetation**: tree/rock scatter weights are multiplied by `(1 − roadMask)` (plants keep
  off the roads); bushes additionally get `+0.2` where `0.02 < roadMask < 0.55` — **weeds
  at the frayed edges**.
- **Creatures**: `GeneratedWorld::roadDistanceAt(x, z)` (bilinear sample of the Dijkstra
  distance field, ~1e9 without roads) lets creature AI treat roads as fast routes through
  rough terrain.

## 6. Data output (all plain CPU data, no GL)

```cpp
struct RoadNetwork {
    std::vector<RoadPath> paths;     // cls, fromSite/toSite, smoothed points (world, y=height), bridge flags
    std::vector<RoadProp> props;     // kind (Milestone/Lantern/Sign/Wall), position, rotY, local mesh
    MeshData roadMesh, bridgeMesh;   // extruded profiles + bridge decks/posts, vertex colors
    std::vector<float> mask;         // gridW*gridH [0,1], noise-frayed road edge mask
    std::vector<float> rutMask;      // [0,1] wheel ruts (puddles)
    std::vector<unsigned char> bridgeMask;
    std::vector<float> dist;         // world-unit distance to the nearest road
    float roadDistanceAt(x, z);
};
// worldgen merges into GeneratedWorld: roads (ribbons), bridges, roadProps, settlements,
// buildings (hamlet houses), roadMask, rutMask, bridgeMask, roadDistance, roadDistanceAt()
```

Deterministic per `RoadGenParams::seed`; worldgen uses its own RNG stream
(`seed ^ 0xB0AD`) for the settlement/road phase so the downstream vegetation scatter stream
is untouched. Tests: `tests/test_procedural_roadgen.cpp` (A\* slope detour, ford vs. bridge,
existing-road merge, spline min-radius, hierarchy + carving + meshes + determinism) and
`testLandscapeSettlementsAndRoads` in `tests/test_procedural_worldgen.cpp` (settlements,
hamlet houses off the corridor, road mask suppression of vegetation, off-switch, determinism).
