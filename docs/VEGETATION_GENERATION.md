# FuhGlGraphics — Ecophysiological Vegetation Generation (G37) design

Status: **implemented in `procedural/vegetation.{hpp,cpp}`.** Language of this document:
English (docs at this level are English-only, see [README.md](README.md)). The *algorithms*
below are deliberately **language- and engine-agnostic**: no code, no frameworks — only
mathematics, data structures and step-by-step procedures.

## 0. Scope and relation to existing modules

G26 `procedural::LSystem` / `procedural::plant.hpp` `PlantGenerator` grow plants from fixed
rule sets per species. G37 instead derives the whole plant — skeleton, crown shape, leaf
morphology, bark — from the **same environmental parameters that drive terrain and buildings**
(G31 `GeneratedWorld::temperature` / `humidity` climate maps, plus wind exposure and stand
density). Same environment, completely different but biologically plausible plants.

| Pipeline stage | Existing module | G37 adds |
|---|---|---|
| Seeded 3D noise (simplex) | G24 `procedural/noise.hpp` | bark crack noise (vertically stretched simplex), seeded attractors |
| Mesh output | `fuhgl::render::MeshData` (11 floats/vertex) | per-vertex growth attributes packed into uv (age, wet side) |
| L-system plants | G26 `procedural/plant.hpp` | Space Colonization skeleton + pipe-model radii instead of fixed-angle rewriting |
| World integration | G31 `procedural/worldgen.hpp` | landscape trees are grown by `VegetationGenerator` from the per-cell climate maps |

End-to-end pipeline (each stage a pure function, deterministic per seed, pure CPU, no GL):

```
EcoEnvironment ─▶ 1. deriveGrowthForm (climate→geometry) ─▶ 2. spaceColonization (light attractors,
   (temp, humidity,      │                                        self-pruning, wind)
    wind, density)       ▼
                  3. applyPipeModel (Da Vinci R^n = Σ r_i^n)
                              ─▶ 4. mesh: branch cylinders + leaf kites/needles/thorns
                                    with baked tints + VegetationMaterial (shader contract)
```

## 1. Light & space competition — Space Colonization Algorithm

Instead of growing L-systems at fixed angles, G37 distributes invisible **light attractors**
(points) in the crown volume where sunlight falls, and branch tips grow actively toward them,
consuming them:

- Attractors are sampled in a disc of radius `crownRadius` between `crownBase` and
  `maxHeight`. The y-distribution exponent `1 − 0.8·density` biases attractors toward the top
  with rising stand density `d` — in a dense forest light comes only from above, so
  attractors concentrate in a small top cap.
- Each iteration, every living tip finds its nearest attractor. If the distance <
  `killDistance`, the attractor is consumed (killed); otherwise the tip grows one `step`
  toward it, spawning a side branch with probability `branchProbability` at
  `branchAngleDeg`.
- **Competition, open vs. dense:** a lone tree on a meadow gets light from all sides
  (attractors fill the whole crown envelope, `crownRadius` full) → broad, spreading crown
  like an old oak. A tree in a dense stand has attractors only near the top and a shrunken
  crown radius → the trunk shoots straight up (`+up` bias `0.3·density`, apical dominance)
  and only a small crown forms at the top.
- **Self-pruning:** the highest living tip (the leader) never dies; every other tip in the
  shaded zone (below `crownBase + 0.25·(crownTop−crownBase)`) that captures no light for
  `pruneGrace` iterations dies. Additionally, after growth, all nodes below `pruneBelow`
  die — except the trunk, defined as the chain from the highest node to the root.

## 2. Climate-to-geometry rules (`deriveGrowthForm`)

`EcoEnvironment{temperature t, humidity h, windExposure w, windDir, standDensity d, season,
seed}` (all in [0,1], `t`/`h` identical to the G31 climate maps) maps to a `GrowthForm`:

- **Humidity (precipitation/evaporation):** very wet (`h > 0.72`) → huge thin leaves with
  **drip tips** (`DripTip`: leaf tip elongated ~2.8× and drooping, rain runs off, no mold).
  Dry (`h < 0.28`) → small thick waxy leaves (`leafThickness = 1`, reduced evaporation);
  very dry (`h < 0.12`, warm) → **thorns** or a **succulent** (thick water-storing trunk,
  `leaf = None`). Leaf size scales with humidity in between.
- **Temperature & snow:** cold (`t < 0.32`) → **needle tree** (`Needle`), conical crown
  (`crownRadius × 0.55`), branches droop downward (`branchAngle ≈ 115°`, `droop > 0`) so
  heavy snow slides off. Warm/frost-free → broadleaf (`Broad`) with wide, nearly horizontal
  branches (`branchAngle ≈ 70°`).
- **Wind exposure:** a constant wind vector `windLean = windDir · w · 0.22 · maxHeight` is
  added (scaled by relative height) to every growth direction, and `maxHeight` is reduced by
  up to 55%. At `w > 0.75` the tree becomes a crooked, stocky "wind-flagged" tree or flat
  ground cover (extra height cut, wider crown, strong droop).
- **Stand density:** crown base rises (`crownBase = lerp(1, 0.45·maxHeight, d)`), self-pruning
  activates (`d > 0.45`), `pruneBelow = d · 0.55 · maxHeight`.

## 3. Hydraulic architecture — Da Vinci rule / pipe model

So a procedural tree does not look like an artificial wire frame, radii follow the **Da Vinci
rule (pipe model theory)**: the cross-section of a parent branch equals the sum of the
cross-sections of all smaller branches leaving it,

```
R^n = r_1^n + r_2^n + … + r_k^n        (n ≈ 2, i.e. area-preserving)
```

`applyPipeModel` walks the skeleton bottom-up (nodes are created children-after-parent, so
reverse creation order) and assigns each node `radius = (Σ children radii^n)^(1/n)`; leaf
tips keep the bud radius `tipRadius`. Trunks and branches therefore taper at every fork with
statically plausible proportions. A succulent multiplies trunk radii by up to `1 + 2.2`.

## 4. Procedural leaf & bark texturing (shader contract + baked vertex tints)

Environment and growth parameters drive the material. The generator emits per-vertex growth
attributes in the standard `MeshData` layout — **uv.x = age** (0 = root/old, 1 = youngest tip;
branches: uv.x = radius/rootRadius) and **uv.y = wet-side factor** `max(0, −dot(normal,
windDir))` — plus a `VegetationMaterial` with the shader parameters, and bakes the same tints
into the vertex colors as the stock-renderer fallback (the G-buffer multiplies vColor into
albedo):

- **Leaf age & sun:** young outer shoots (age → 1) are light yellow-green, old inner leaves
  (age → 0) dark and saturated: `leafTint(m, age, autumn) = lerp(lerp(leafOld, leafYoung,
  age), autumnRamp(autumn), autumn)`.
- **Autumn / drought:** a threshold shifts chlorophyll green over yellow → orange → red-brown:
  `autumn = max(season, 0.85·smoothstep(0.30, 0.05, humidity))`, ramp in `autumnRampColor`.
- **Subsurface scattering:** thin leaves let sunlight shine through (backlighting) —
  engine-side via `render::Effect::Subsurface` (G33) with `MeshEntry::subsurface =
  material.sss` (rides in G-buffer pbr.a as thickness); `sss = lerp(0.85, 0.15,
  leafThickness)` so thin tropical leaves glow more than thick waxy ones.
- **Bark by age & humidity:** young wood is smooth greenish-gray, with rising radius the
  bark cracks via vertically stretched simplex noise `simplex3(x·s, y·s/stretch, z·s)`
  (`barkCrackScale`, `barkCrackStretch`), darkened per vertex. On the wet weather side moss
  grows: `mossMask = mossCoverage · wetSideFactor(normal, windDir)` with
  `mossCoverage ≈ 0.9·humidity`.

A custom leaf/bark shader evaluates the same formulas from the vertex attributes
(`uv.x`, `uv.y`, object-space position); the demo `examples/vegetation_showcase` shows the
stock path (vertex-color tints + G33 SSS on the leaf mesh).

## 5. Integration

`procedural::generateWorld` (G31) landscape scatter now samples its own climate maps at each
poisson-disk tree position: `EcoEnvironment.temperature/humidity` from the per-cell maps,
`windExposure` from slope + altitude (ridges/peaks are windy), `windDir` from a seeded
prevailing direction, `standDensity` from `treeMinDist`. Cold taiga cells grow needle trees
with conical crowns, wet tropical cells drip-tip broadleaf, dry cells thorns/succulents,
windy ridges leaning flagged trees. City park bushes stay G26 `PlantGenerator` bushes.

## 6. Verification

`tests/test_procedural_vegetation.cpp` (label "unit"): climate→geometry rule table, open vs.
dense competition (crown width, pruned lower half, height), pipe-model identity
`R² = Σ r_i²` at every internal node, wind lean direction + reduced height, determinism per
seed, mesh validity, leaf/bark tint helpers, succulent = no leaves. Demo:
`examples/vegetation_showcase` (six environments side by side, G33 SSS backlighting),
CI renders `docs/images/ci/g37_vegetation_*.png`.
