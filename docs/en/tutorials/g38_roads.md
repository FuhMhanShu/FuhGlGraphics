# Tutorial G38 — Organic road / path network (least-cost paths + terrain blending)

> 🇩🇪 **Deutsche Version:** [docs/de/tutorials/g38_roads.md](../../de/tutorials/g38_roads.md)

**Goal:** generate a believable traffic network — highways, country roads, desire paths —
that follows the physics of the terrain instead of cutting straight lines through it:
`procedural::RoadGenerator` in `procedural/roadgen.hpp` (design: [docs/ROAD_GENERATION.md](../../ROAD_GENERATION.md)).
In the G31 landscape, `generateWorld()` already uses it end-to-end: terrain & climate pick
the settlements, the settlements are connected by the road network, the roads carve their
bench into the terrain, vegetation keeps off the roads (weeds at the edges), and
`roadDistanceAt()` gives creatures fast routes.

## Usage (standalone)

```cpp
#include <fuhglgraphics/procedural/roadgen.hpp>

using namespace fuhglgraphics::procedural;

RoadInput in;
in.heightmap = &world.heightmap;      // normalized [0,1], row-major x + gridW*z
in.gridW = world.gridW;  in.gridH = world.gridH;
in.sizeX = world.sizeX;  in.sizeZ = world.sizeZ;
in.heightScale = world.heightScale;
in.waterLevel = 0.30f;                // normalized
in.flow = &world.flowmap;             // erosion/river flow (ford detection) — optional
in.biome = &world.biome;              // biome codes (vegetation cost) — optional
in.sites = {{{-30.f, 0.f}, 0.9f, 0},  // pos, importance, id
             {{ 30.f, 0.f}, 0.9f, 1},
             {{  0.f, 35.f}, 0.4f, 2}};

RoadGenParams rp;
rp.seed = 42;
RoadGenerator rg;
RoadNetwork net = rg.generate(in, rp);   // 1. route + stamp masks
rg.carveTerrain(heightmap, net, rp);     // 2. bench/embankment into the heightmap
rg.buildGeometry(net, in, rp);           // 3. meshes + props on the carved terrain
```

`generate()` routes the hierarchy — **highways** between the important sites (Prim MST over
`importance ≥ highwayMinImportance`), **country roads** from every other site to the nearest
connected one, **desire paths** to random nearby land POIs — each with its own A\* run.

## The A* cost function (the physics)

Per step of length `d` into cell `b` (slope = world `dh/dx` of the heightmap):

```
step = d · (1 + slopeWeight · slope(b)²)               // quadratic slope cost
if slope(b) > critSlope(class)  step *= criticalPenalty // >~15° for highways → switchbacks
water cell:  shallow+narrow (depth ≤ fordMaxDepth && flow ≤ fordMaxFlow) → d·fordCost
             (preferred BRIDGE location) — every water crossing is rendered as a bridge/steg
             deck above the water, never as a sunken ford ribbon
             else                                            → d·waterCost (bridge if forced)
swamp (high flow near water) → ·swampCost;  forest biome → ·forestCost;  rock → ·rockCost
existing road mask m → step *= 1 − existingRoadDiscount·m  // new paths merge, not parallel
```

**Every** path point below the water level gets `bridge = 1` — roads cross water only as a
bridge/steg deck (`max(terrain, waterLevel+0.7)` + posts in `net.bridgeMesh`); the terrain
carving skips water, so the riverbed under the bridge stays intact.

## Geometry & blending

- Raw A\* grid line → **Catmull-Rom spline** resampled at ~1 cell + **min-curve-radius
  clamp** per class (`minRadiusHighway/Country/Trail`).
- **Cross-section extrusion** into `net.roadMesh` (vertex colors = material: asphalt /
  cobble / gravel / dirt per class + biome; rut vertices darkened for puddles):
  highway = crowned carriageway + shoulders + ditches; country road = two wheel ruts +
  grass ridge (`net.rutMask` marks the ruts); trail = narrow trodden depression.
  Bridge runs → flat deck + posts in `net.bridgeMesh`.
- `carveTerrain()` cuts the **bench** into the uphill side and fills the **embankment** on
  the downhill side (water and the border ring untouched).
- `net.mask` = road mask with **noise-frayed edges** (terrain-shader edge blending +
  scatter suppression); `net.dist` = Dijkstra distance field → `net.roadDistanceAt(x, z)`.
- **Props** with jitter along the spline: milestones, lanterns near settlements, way signs
  at junctions, guard walls at steep drops.

## Key parameters (`RoadGenParams`)

```cpp
rp.highwayMinImportance = 0.7f;  // importance threshold for highway hubs
rp.desirePathsPerSite = 2;       // trails per site
rp.slopeWeight = 26.f;           // quadratic slope cost
rp.critSlopeHighway = 0.27f;     // ~15°; Country 0.45, Trail 0.95 (auto switchbacks)
rp.criticalPenalty = 90.f;
rp.waterCost = 500.f;  rp.fordMaxDepth = 0.025f;  rp.fordMaxFlow = 0.2f;  rp.fordCost = 5.f;
rp.existingRoadDiscount = 0.85f; // merge into existing roads
rp.minRadiusHighway = 45.f;  rp.highwayWidth = 6.f;  rp.shoulderWidth = 1.6f;
rp.countryWidth = 3.6f;  rp.trailWidth = 1.3f;  rp.rutDepth = 0.09f;  rp.rutOffset = 0.3f;
rp.benchCut = 1.2f;  rp.embankment = 1.0f;  rp.blendMargin = 2.5f;  // carving
rp.milestoneEvery = 26.f;  rp.lanternEvery = 18.f;  rp.wallMinDrop = 1.6f; // props
```

## In the world generator (G31 landscape)

```cpp
WorldGenParams p;
p.mode = WorldGenParams::Mode::Landscape;
p.settlementCount = 4;        // hamlets placed by terrain+climate (0 = off)
p.housesPerSettlement = 8;
p.roads.seed = 777;           // nested RoadGenParams for the network
const GeneratedWorld w = generateWorld(p);
// w.settlements     — centers/importance/house ranges (houses in w.buildings)
// w.roads           — road ribbons (landscape mode), w.bridges — bridge decks/posts
// w.roadProps       — milestones/lanterns/signs/walls as PlacedMesh
// w.roadMask / w.rutMask / w.bridgeMask / w.roadDistance  — per-cell maps
w.roadDistanceAt(x, z);       // distance to the nearest road — creature fast routes
```

## Screenshot

The G31 renders show the feature end-to-end — hamlets connected by the road network, bridges
only over water: [g31_worldgen_landscape.png](../../images/g31_worldgen_landscape.png)
(overview at the main hamlet) and [g31_worldgen_roads.png](../../images/g31_worldgen_roads.png)
(street-level along the main road) — CI-rendered references from the PR run.

## Tests

`tests/test_procedural_roadgen.cpp`: A\* slope detour through a ridge gap, ford vs. deep
river, existing-road merge discount, spline endpoints/min-radius, hierarchy + bridge flags +
carving + mesh extrusion + determinism. `testLandscapeSettlementsAndRoads` in
`tests/test_procedural_worldgen.cpp`: settlements with houses off the road corridor,
vegetation suppressed on the road mask, off-switch (`settlementCount = 0`), determinism.
