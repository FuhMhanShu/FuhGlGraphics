# Tutorial G38 — Organisches Straßen-/Wegenetz (Least-Cost-Pfade + Terrain-Blending)

> 🇬🇧 **English version:** [docs/en/tutorials/g38_roads.md](../../en/tutorials/g38_roads.md)

**Ziel:** ein glaubwürdiges Verkehrsnetz erzeugen — Autobahnen/Handelsstraßen, Landstraßen,
Trampelpfade — das der Physik des Terrains folgt, statt als gerade Linie durchgezogen zu
werden: `procedural::RoadGenerator` in `procedural/roadgen.hpp` (Design:
[docs/ROAD_GENERATION.md](../../ROAD_GENERATION.md)). In der G31-Landschaft nutzt
`generateWorld()` das System bereits komplett: Terrain & Klima bestimmen die Siedlungen, die
Siedlungen werden vom Straßennetz verbunden, die Straßen fräsen ihre Terrasse ins Terrain,
die Vegetation weicht den Straßen aus (Unkraut an den Rändern), und `roadDistanceAt()`
liefert Kreaturen schnelle Routen.

## Verwendung (standalone)

```cpp
#include <fuhglgraphics/procedural/roadgen.hpp>

using namespace fuhglgraphics::procedural;

RoadInput in;
in.heightmap = &world.heightmap;      // normalisiert [0,1], row-major x + gridW*z
in.gridW = world.gridW;  in.gridH = world.gridH;
in.sizeX = world.sizeX;  in.sizeZ = world.sizeZ;
in.heightScale = world.heightScale;
in.waterLevel = 0.30f;                // normalisiert
in.flow = &world.flowmap;             // Erosions-/Fluss-Flow (Furten-Erkennung) — optional
in.biome = &world.biome;              // Biome-Codes (Vegetationskosten) — optional
in.sites = {{{-30.f, 0.f}, 0.9f, 0},  // Position, Wichtigkeit, id
             {{ 30.f, 0.f}, 0.9f, 1},
             {{  0.f, 35.f}, 0.4f, 2}};

RoadGenParams rp;
rp.seed = 42;
RoadGenerator rg;
RoadNetwork net = rg.generate(in, rp);   // 1. Routen legen + Masken stempeln
rg.carveTerrain(heightmap, net, rp);     // 2. Hanganschnitt/Damm in die Heightmap
rg.buildGeometry(net, in, rp);           // 3. Meshes + Props auf dem gecarvten Terrain
```

`generate()` routet die Hierarchie — **Highways** zwischen den wichtigen Sites (Prim-MST
über `importance ≥ highwayMinImportance`), **Landstraßen** von jedem weiteren Site zum
nächstgelegenen verbundenen, **Trampelpfade** zu zufälligen Land-Zielen in der Nähe — jede
Verbindung mit eigener A*-Suche.

## Die A*-Kostenfunktion (die Physik)

Pro Schritt der Länge `d` in Zelle `b` (Steigung = Welt-`dh/dx` der Heightmap):

```
step = d · (1 + slopeWeight · slope(b)²)               // quadratische Steigungskosten
if slope(b) > critSlope(class)  step *= criticalPenalty // >~15° bei Highways → Serpentinen
Wasserzelle:  seicht+schmal (Tiefe ≤ fordMaxDepth && flow ≤ fordMaxFlow) → d·fordCost
             (bevorzugter BRÜCKEN-Standort) — jede Wasserquerung wird als Brücken-/Steg-Deck
             über dem Wasser gerendert, nie als abgesenkte Furt
             sonst                                          → d·waterCost (Brücke falls erzwungen)
Sumpf (hoher Flow nahe Wasser) → ·swampCost;  Wald-Biome → ·forestCost;  Fels → ·rockCost
bestehende Straße (Maske m) → step *= 1 − existingRoadDiscount·m  // verschmelzen, nicht parallel
```

**Jeder** Pfadpunkt unterhalb des Wasserspiegels (oder in einer tiefen trockenen Schlucht)
bekommt `bridge = 1` — Straßen überqueren Wasser nur als Brücke. Der `BridgeGenerator`
(`procedural/bridgegen.hpp`) analysiert jeden Brückenlauf in `RoadPath::spans`: Ufer-Anker
(Widerlager) = erste stabile Punkte über `waterLevel + floodMargin`, Spannweite = Abstand der
Anker, Deckhöhe = `max(Ufer, waterLevel + deckClearance)` (+ parabolische Mittelerhöhung bei
schiffbaren Flüssen). Daraus wählt er den Brückentyp: < 10 m / Dorf / Wald → **Holzsteg**
(Pfähle + Querträger), 10–60 m / Stein-Region → **Steinbogenbrücke** (Sinus-Bögen + Pfeiler per
Raycast bis auf den Flussgrund), tiefe Schlucht → **Hängebrücke** (Pylone + Hauptseile als
Kettenlinie `y = a·cosh(x/a)` + Hänger), > 60 m / hoher Tech-Level → **Fachwerkbrücke**. Das
Deck ist ein extrudiertes Querschnitts-Profil mit Bordsteinen, Entwässerungsrinnen und
Brüstungen; Widerlager-Blöcke verankern die Brückenenden im Hang, das Ufer-Terrain wird per
Smoothstep an die Auffahrtshöhe angeglichen, und am Eingang stehen Props (Prellsteine,
Steinlöwen, Wachturm, Zollhäuschen). Im Lighting-Pass färbt ein Wasserlinien-Band
(Material-ID 10) die Pfeiler knapp über dem Wasserspiegel dunkel-nass mit Algenbelag. Das
Terrain-Carving spart Wasserzellen aus, das Flussbett unter der Brücke bleibt intakt.

## Geometrie & Blending

- Rohe A*-Gitterlinie → **Catmull-Rom-Spline**, resampelt auf ~1 Zelle + **Mindestkrümmungsradius-
  Clamp** pro Klasse (`minRadiusHighway/Country/Trail`).
- **Querschnitts-Extrusion** in `net.roadMesh` (Vertex-Colors = Material: Asphalt /
  Kopfstein / Schotter / Erde je nach Klasse + Biome; Reifenspuren abgedunkelt für Pfützen):
  Highway = gewölbte Fahrbahn + Bankette + Gräben; Landstraße = zwei Reifenspuren +
  Grasstreifen (`net.rutMask` markiert die Spuren); Trampelpfad = schmale Mulde.
  Brückenläufe → `BridgeGenerator::buildSpan()`: Deck-Profil (Bordsteine/Rinnen/Brüstungen) +
  Unterbau (Bögen/Fachwerk/Kettenlinie) + Pfeiler + Widerlager in `net.bridgeMesh`.
- `carveTerrain()` schneidet die **Terrasse** bergseitig ein und schüttet talseitig den
  **Damm** auf (Wasser und äußerer Ring bleiben unberührt).
- `net.mask` = Straßenmaske mit **Noise-ausgefransten Rändern** (Edge-Blending im
  Terrain-Shader + Scatter-Suppression); `net.dist` = Dijkstra-Distanzfeld →
  `net.roadDistanceAt(x, z)`.
- **Props** entlang der Spline mit Jitter: Meilensteine, Laternen nahe Siedlungen,
  Wegweiser an Kreuzungen, Steinmauern an Abhängen.

## Wichtigste Parameter (`RoadGenParams`)

```cpp
rp.highwayMinImportance = 0.7f;  // Wichtigkeits-Schwelle für Highway-Knoten
rp.desirePathsPerSite = 2;       // Trampelpfade pro Site
rp.slopeWeight = 26.f;           // quadratische Steigungskosten
rp.critSlopeHighway = 0.27f;     // ~15°; Country 0.45, Trail 0.95 (autom. Serpentinen)
rp.criticalPenalty = 90.f;
rp.waterCost = 500.f;  rp.fordMaxDepth = 0.025f;  rp.fordMaxFlow = 0.2f;  rp.fordCost = 5.f;
rp.existingRoadDiscount = 0.85f; // in bestehende Straßen einfädeln
rp.minRadiusHighway = 45.f;  rp.highwayWidth = 6.f;  rp.shoulderWidth = 1.6f;
rp.countryWidth = 3.6f;  rp.trailWidth = 1.3f;  rp.rutDepth = 0.09f;  rp.rutOffset = 0.3f;
rp.benchCut = 1.2f;  rp.embankment = 1.0f;  rp.blendMargin = 2.5f;  // Carving
rp.milestoneEvery = 26.f;  rp.lanternEvery = 18.f;  rp.wallMinDrop = 1.6f; // Props
rp.bridges.techLevel = 0.5f;   // Wohlstand/Tech-Level (worldgen: mittlere Siedlungs-Importance)
rp.bridges.woodSpanMax = 10.f; rp.bridges.archSpanMax = 60.f;  // Spannweiten-Schwellen
rp.bridges.floodMargin = 0.5f; rp.bridges.deckClearance = 0.7f; // Anker + Deck über Wasser
```

## Im World-Generator (G31-Landschaft)

```cpp
WorldGenParams p;
p.mode = WorldGenParams::Mode::Landscape;
p.settlementCount = 4;        // Weiler, platziert nach Terrain+Klima (0 = aus)
p.housesPerSettlement = 8;
p.roads.seed = 777;           // verschachtelte RoadGenParams für das Netz
const GeneratedWorld w = generateWorld(p);
// w.settlements     — Zentren/Wichtigkeit/Hausbereiche (Häuser in w.buildings)
// w.roads           — Straßenbänder (Landschaftsmodus), w.bridges — Brücken
// w.roadProps       — Meilensteine/Laternen/Wegweiser/Mauern als PlacedMesh
// w.roadMask / w.rutMask / w.bridgeMask / w.roadDistance  — Zellenkarten
w.roadDistanceAt(x, z);       // Distanz zur nächsten Straße — schnelle Kreaturen-Routen
```

## Screenshot

Die G31-Render zeigen das Feature komplett — Weiler, verbunden durch das Straßennetz, Brücken
nur über Wasser: [g31_worldgen_landscape.png](../../images/g31_worldgen_landscape.png)
(Übersicht am Hauptweiler) und [g31_worldgen_roads.png](../../images/g31_worldgen_roads.png)
(Straßenniveau entlang der Hauptstraße) — CI-gerenderte Referenzen aus dem PR-Lauf.

## Tests

`tests/test_procedural_roadgen.cpp`: A*-Umweg durch eine Gebirgslücke, Furt vs. tiefer Fluss,
Existing-Road-Marge-Rabatt, Spline-Endpunkte/Mindestradius, Hierarchie + Brückenflags +
Carving + Mesh-Extrusion + Determinismus. `testLandscapeSettlementsAndRoads` in
`tests/test_procedural_worldgen.cpp`: Siedlungen mit Häusern abseits des Straßenkorridors,
Vegetation auf der Straßenmaske unterdrückt, Abschalter (`settlementCount = 0`), Determinismus.
