# Ambient life (G39) — causal clutter, micro-fauna and atmospheric particles

Design doc for the G39 "ambient life" layer of the world generator. Two modules, both pure
CPU and deterministic per seed, integrated as step 5 of the G31 landscape pipeline
(`generateWorld(Landscape)`):

- **G39a — causal clutter scattering** (`procedural/clutter.{hpp,cpp}`,
  `ClutterGenerator`): *what lies where* is derived from the world data, not from uniform
  randomness.
- **G39b — ambient life sims** (`procedural/lifegen.{hpp,cpp}`): a shared 3D wind field,
  boids flocks, curl-noise insect swarms, biome-specific ambient particles, and `LifeZone`
  descriptors that worldgen derives from the same world data.

The closed loop stays intact: terrain & climate decide settlements (G31) → settlements produce
roads (G38) → roads carve the terrain → vegetation keeps off the roads and clutter accumulates
causally at road rims, cliff feet, riverbeds, under trees and around buildings (G39a) → life
zones and particles live where the biome says they should (G39b) → all of it reacts to one
shared wind field, so a gust sweeps through grass, tree crowns and drifting leaves in order.

## G39a — causal clutter (`ClutterGenerator`)

Input (`ClutterInput`) is plain world data: normalized heightmap + grid/size/heightScale,
water level, flow map (erosion/river accumulation), humidity map, biome cast, road mask,
tree spots (+litter radii) and building spots. Output: `std::vector<ClutterItem>` —
kind + local mesh (base at y=0, vertex colors encode the kind) + world position + rotY + scale.

### The causal rules (each weight function is public and unit-tested)

| Rule | Reads | Weight |
| --- | --- | --- |
| Rockfall / talus | slope map (central differences of the heightmap) | `smoothstep(cliffSlope*0.55, cliffSlope*1.35, slope)`, zero below `waterLevel+0.04` (no talus in the water) — gathers at cliff feet |
| Pebbles / driftwood | flow map + height | `flow > 0.35` and `h <= waterLevel+0.04`; driftwood where `flow > 0.7` |
| Reeds | shore band + flow | `h` in `[waterLevel-0.02, waterLevel+0.08]` band × flow factor — lines the banks |
| Tree litter (branch/cone/bark/leaf pile) | tree spots, biome (conifer→cones), humidity | parent-child: spawns only inside the tree's litter radius, skipped under water |
| Mushrooms | tree spots + humidity | north side (`z = t.y − |sin|·r − 0.3`), only where `humidity` passes — wet shade |
| Building clutter (weed/gravel/roof tile/crate) | building spots | ring of radius 2.5..7 around each building |
| Road-rim clutter (weed tufts/gravel) | road mask | only the frayed rim `mask ∈ (0.03, 0.6)`, not the carriageway |

### Clusters: poisson-disk + high-frequency noise mask (+ zone targeting)

Real nests (mushrooms, pebble beds, weed tufts) never grow alone. `poissonScatter()` does
weighted dart-throwing with a spatial hash (minimum distance `minDist`); on top of it
`clusterMask()` evaluates high-frequency fbm2 and remaps it through `clusterThreshold`:
below the threshold nothing grows at all, above it a dense nest forms. Each rule uses its own
noise seed so the nests of different kinds do not overlap perfectly.

Thin features (riverbeds, the waterline band) would almost never be hit by uniform dart
throwing, so the riverbed and reed passes jitter their candidates around **precomputed zone
cells** (high-flow sub-water cells / waterline-band cells) instead. Pass order and budgets:
rockfall, riverbed, reeds, building ring (`buildingClutterCount`), road rim
(`roadEdgeClutterCount`) — each with its own budget so one kind cannot starve the others —
then the numerous tree-litter pass fills the remaining `maxItems`.

### Integration

`WorldGenParams::clutterCount` (default 700, 0 = off) caps the total via
`ClutterGenParams::maxItems`; `WorldGenParams::clutterParams` carries the per-kind budgets.
The scatter runs after the G38 road carving, so it reads the final terrain and the final road
mask. `examples/worldgen_screenshot` bakes all items into **one static mesh** (per-item
transform applied, vertex colors carry the kind) — the whole scatter costs a single draw call.

## G39b — ambient life sims (`lifegen.hpp`)

### Shared wind field (`WindField`)

`sample(x, y, z, t) = baseDir·(baseSpeed + gustAmp·gust) + fbm turbulence`, where
`gust = 0.5 + 0.5·sin(2π·(pos·dir)/gustWavelength − 2π·t·gustSpeed/gustWavelength)` —
a **traveling gust front**: the wind peaks at `x = λ/4 + gustSpeed·t` along the wind direction,
so a gust visibly sweeps across the landscape. `WorldGenParams::wind` is echoed as
`GeneratedWorld::windParams` for the app; the app mirrors the same formula in the vertex
shader (grass blades, bushes, tree crowns) and in the particle integrate, so when a gust
passes, the grass bends first, then the branches, and leaves fly off in exactly that direction.

### Boids (`BoidsSim`) — bird flocks, fish schools

The classic three rules per neighbor within `neighborDist`: **separation** (push apart),
**alignment** (match velocity), **cohesion** (pull to the local centroid), plus advection by
the shared wind (`windWeight`), a cruise-speed band around `speed`, a soft bounding sphere
(`center`, `radius`) and `maxForce` clamping. Fixed-step, deterministic: `step(dt, wind, t)`.

### Curl-noise insects (`InsectSwarm`, `curlNoise3`)

`curlNoise3` is the curl (∇×) of a simplex3 vector potential via central differences —
divergence-free by construction, so advected particles follow swirling, never head-on
colliding paths: butterflies over a meadow, midges over a swamp. Each insect is advected
through the field at `speed`, plus wind, clamped to the soft sphere and the ground layer
(`y ≥ 0.2`).

### Ambient particles (`AmbientParticleSim`, `ambientKindFor`)

Biome → species mapping (`ambientKindFor`): desert/rock → sand, snow/taiga → snow dust,
coast → spray (humid) or sand, forest/jungle → seeds (humid) or leaves, grassland → pollen
(humid) or leaves. Each kind has its own sink/rise speed (pollen/seed rise, leaf/snow sink,
sand/spray stay low), sideways flutter and wind advection; dead particles respawn at the top
of the spawn box (`respawn`), so the alive count stays constant — a continuous atmosphere.

### Life zones (`LifeZone`) derived by worldgen

`generateWorld(Landscape)` derives zones from the world maps (on by default,
`WorldGenParams::lifeZones`): a **bird flock** at the lake centroid, a **fish school** at the
strongest sub-water flow cell, an **insect swarm** over a meadow/forest cell, **fireflies** in
the wettest forest, and **ambient particles** at a representative cell of the dominant biome
(kind via `ambientKindFor`). The app spawns GPU particles / instanced critters at these
positions and drives them with the CPU sims above at a fixed dt.

## Tests

`tests/test_procedural_clutter.cpp` (causal weights, placement bounds, parent-child radius,
north-side mushrooms, cluster mask, determinism, maxItems cap) and
`tests/test_procedural_lifegen.cpp` (wind determinism + gust-front travel, boids
separation/alignment/cohesion + bounds + determinism, curl-field divergence ≈ 0, particle
respawn/bounds/kind mapping, determinism), plus `testLandscapeAmbientLife` in
`tests/test_procedural_worldgen.cpp` for the pipeline integration.
