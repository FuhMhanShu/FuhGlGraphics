# Tutorial G39 — Ambient Life: kausales Clutter, Mikro-Fauna, atmosphärische Partikel

> 🇬🇧 **English version:** [docs/en/tutorials/g39_ambient_life.md](../../en/tutorials/g39_ambient_life.md)

**Ziel:** einer prozeduralen Landschaft echtes Leben einhauchen. Kleine Details dürfen nicht
gleichmäßig gestreut werden — sie entstehen *kausal* aus der Welt: Äste liegen nur unter
Bäumen, Kieselsteine nur im Flussbett, Geröll nur am Fuß von Klippen. Und die Luft bewegt sich:
ein gemeinsames Windfeld treibt Grashalme, Baumkronen und treibende Partikel gemeinsam an, sodass
Windböen in Reihenfolge durch die Szene ziehen.
Zwei Module (Design: [docs/AMBIENT_LIFE.md](../../AMBIENT_LIFE.md)):
`procedural::ClutterGenerator` (`procedural/clutter.hpp`, G39a) und die G39b-Sims in
`procedural/lifegen.hpp`. In der G31-Landschaft führt `generateWorld()` beides bereits aus:
`GeneratedWorld::clutter` enthält die gestreuten Objekte, `GeneratedWorld::lifeZones` die
Lebenszonen, `GeneratedWorld::windParams` das gemeinsame Windfeld.

## G39a — kausales Clutter (standalone)

```cpp
#include <fuhglgraphics/procedural/clutter.hpp>

using namespace fuhglgraphics::procedural;

ClutterInput in;
in.heightmap = &world.heightmap;   // normalisiert [0,1], zeilenweise x + gridW*z
in.gridW = world.gridW;  in.gridH = world.gridH;
in.sizeX = world.sizeX;  in.sizeZ = world.sizeZ;
in.heightScale = world.heightScale;
in.waterLevel = 0.30f;
in.flow = &world.flowmap;          // Erosions-/Fluss-Flow
in.humidity = &world.humidity;     // treibt Pilze an feuchten Nordseiten
in.biome = &world.biome;           // Nadelwald-Biom -> Tannenzapfen unter Bäumen
in.roadMask = &world.roadMask;     // G38-Straßenmaske -> Clutter am ausgefransten Rand
for (const auto& t : world.trees) in.treeSpots.push_back({t.position.x, t.position.z});
in.treeRadii.assign(world.trees.size(), 3.2f);
for (const auto& b : world.buildings) in.buildingSpots.push_back({b.position.x, b.position.z});

ClutterGenParams cp;               // Budgets pro Art (rockfallCount, pebbleCount, ...)
cp.seed = 42;
ClutterGenerator cg;
std::vector<ClutterItem> clutter = cg.generate(in, cp); // deterministisch pro Seed
```

Jedes `ClutterItem` = `kind` (Geröll, Kiesel, Treibholz, Schilf, Ast, Zapfen, Rinde, Pilz,
Laubhaufen, Unkrautbüschel, Kieshaufen, Dachziegel, Kiste) + lokales Mesh + Position/rotY/scale.
Alle Items in **ein statisches Mesh** backen (pro Item translate·rotateY·scale anwenden,
Vertex-Farben behalten) — die gesamte Streuung kostet genau einen Draw Call, genau wie in
`examples/worldgen_screenshot`.

Die kausalen Regeln, jede als testbare Gewichtungsfunktion exponiert:

- `rockfallWeight` — Steigungskarte (zentrale Differenzen): Geröllhalden wo die Hangneigung
  `cliffSlope` überschreitet (nichts im Wasser: `h < waterLevel+0.04` → 0).
- `riverbedWeight` — hoher Flow + Höhe an/unter der Wasserlinie → Kiesel, Treibholz wo der
  Flow sehr hoch ist.
- `waterlineWeight` — schmales Uferband mit etwas Flow → Schilf entlang der Ufer.
- `treeDistance` / Gebäudering — Parent-Child-Streu innerhalb des Streu-Radius jedes Baums;
  Pilze nur an der feuchten **Nordseite**, feuchtigkeitsgewichtet.
- `roadEdgeDistance` — nur der ausgefranste Straßenmasken-Rand (`0.03..0.6`), nie die Fahrbahn.
- `clusterMask` — hochfrequentes fbm durch `clusterThreshold`: Nester vs. kahler Boden.
  Kombiniert mit gewichteter Poisson-Disk-Streuung (`minDist`), damit Nester Mindestabstand haben.

## G39b — Lebens-Sims und das gemeinsame Windfeld

```cpp
#include <fuhglgraphics/procedural/lifegen.hpp>

WindField wind(world.windParams);           // Echo von WorldGenParams::wind
WindFieldParams wp;                          // baseDir/baseSpeed + wandernde Böenfront
                                              // (gustWavelength/gustSpeed) + fbm-Turbulenz
// wind.sample(x,y,z,t) im Vertex-Shader spiegeln für Gras-/Baum-Schwanken:
const fuhgl::math::Vec3 w = wind.sample(x, y, z, t);

BoidsSim voegel;                             // BoidsParams: center/radius/count/speed...
voegel.step(1.f/60.f, wind, t);              // Separation + Alignment + Cohesion + Wind

InsectSwarm muecken;                         // Curl-Noise-Advektion: taumelnd, nie kollidierend
muecken.step(1.f/60.f, wind, t);

AmbientParticleSim partikel;                 // Biom-gemappte Arten via ambientKindFor(biome, hum)
partikel.step(1.f/60.f, wind, t);            // Pollen/Samen steigen, Blätter/Schnee sinken, Respawn oben
```

Alle Sims sind fixed-step und deterministisch pro Seed — die Positionen pro Frame in das
G5-GPU-Partikelsystem oder winzige instanzierte Meshes füttern. `generateWorld(Landscape)`
leitet die `LifeZone`s bereits aus den Weltkarten ab: Vogelschwarm am See-Schwerpunkt,
Fischschwarm an der stärksten Unterwasser-Strömung, Insektenschwarm über einer Wiese,
Glühwürmchen im feuchtesten Wald, ambiente Partikel an einer Zelle des dominanten Bioms
(`ambientKindFor`: Wüste→Sand, Schnee/Taiga→Schneestaub, Küste→Gischt, Wald→Blätter/Samen,
Grünland→Pollen).

## Im Weltgenerator

```cpp
procedural::WorldGenParams p;
p.clutterCount = 700;        // G39a-Obergrenze (0 = aus), Knöpfe in p.clutterParams
p.lifeZones = true;          // G39b-LifeZone-Ableitung (Default an)
p.wind.baseDir = {1.f, 0.f, 0.25f};  p.wind.baseSpeed = 2.5f;  p.wind.gustAmp = 2.f;
procedural::GeneratedWorld w = procedural::generateWorld(p);
// w.clutter, w.lifeZones, w.windParams — app-seitig attachen/simulieren
```

Die Streuung läuft als Schritt 5 der Landschafts-Pipeline — nach dem G38-Road-Carving — und
liest damit das finale Terrain und die finale Straßenmaske; die Worldgen-Screenshots
(`g31_worldgen_landscape.png`, `_roads.png`, `_village.png`, `_panorama.png`) zeigen das Clutter
in die Szene gebacken.
