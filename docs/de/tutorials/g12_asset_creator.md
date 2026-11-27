# Tutorial G12 — Asset-Creator-Qt-Tool + MCP (LLM-Brücke)

> 🇬🇧 **English version:** [docs/en/tutorials/g12_asset_creator.md](../../en/tutorials/g12_asset_creator.md)

**Ziel:** das G12-Tool `fuhglgraphics_asset_creator` (`tools/asset_creator/`, gebaut mit
`FUHGLGRAPHICS_WITH_QT=ON` + `FUHGLGRAPHICS_BUILD_TOOLS=ON`): Assets erstellen/laden/
bearbeiten mit einem Datenstruktur-Dock (Szenenbaum + Inspektor + Asset-Browser), einem
Viewport über den G3-`DeferredRenderer` und einem LLM-Chat über **MCP** — wobei die
MCP-Schnittstelle auch die **prozeduralen Funktionen (G24–G31)** und die
**Deferred-Render-Einstellungen (inkl. G13-Wasser)** zugreifbar macht.
Design: [docs/ASSET_CREATOR.md](../ASSET_CREATOR.md). Die Qt-freie MCP-Schicht liegt in der
Bibliothek: `include/fuhglgraphics/mcp/`.

## 1. Die MCP-Schicht (Qt-frei, unit-getestet — `mcp/`)

```
mcp/jsonrpc.hpp        JSON-RPC 2.0 Bauen/Parsen (Request/Notification/Response/Error),
                       Zeilen-Framing (extractLine), id-Matching — auf io::Json
mcp/tool_catalog.hpp   der Tool-Katalog: toolsListJson() (MCP tools/list) + callTool()
                       (tools/call-Dispatch) gegen einen ToolHost
mcp/mcp_server.hpp     stdio-MCP-Server: initialize / notifications/initialized / ping /
                       tools/list / tools/call (eine JSON-RPC-Nachricht pro Zeile)
```

`ToolHost` bindet, worauf ein Tool zugreifen darf: die `scene::Scene*`, das Render-Settings-
Ziel `scene::Effects*` (Effektliste + `EffectParams`), die aktive G13-`water::WaterSurface*`
und die GL-Resolver (`meshResolver` für Shape-Specs, `meshDataResolver` für erzeugte
`MeshData`, `waterCreate` für das Wasser-Gitter). Ohne GL-Resolver funktionieren die Tools
weiter — sie liefern die erzeugten **Daten** und antworten `"attached": false`.

### Tool-Gruppen

| Gruppe | Tools |
| --- | --- |
| Szene | `scene_list_nodes`, `scene_add_node`, `scene_remove_node`, `scene_set_trs`, `scene_add_shape` (box/quad/sphere), `scene_add_light` |
| Deferred-Render-Einstellungen | `render_list_effects` (alle `render::Effect` + supported-Flag), `render_set_effect {effect, enabled}`, `render_get_settings`, `render_set_param {name, value}` (jedes `EffectParams`-Feld, z. B. `bloomThreshold`, `tonemapOperator: "reinhard"`), `render_set_params {params:{...}}` |
| G13-Wasser | `water_add_to_scene`, `water_get_params`, `water_set_params` (Gerstner-`waves[]`, Farben, Schaum, Reflexionsstärke, ...) |
| Prozedural G24–G31 | `procedural_noise` (value/perlin/simplex × 2D/3D + fbm/ridged/turbulence), `procedural_marching_cubes` (Felder mandelbulb/sphere/box/blob), `procedural_lsystem` (tree/fern/bush), `procedural_wfc` (dungeon/city), `procedural_voronoi` (Zellen + Extrusion), `procedural_sdf` (mandelbulb/roundedBox/sphere/box/cylinder), `procedural_creature` (quadruped/reptile), `procedural_worldgen` (landscape/city) — je mit `attach: true`, um das Ergebnis als Szenen-Node einzufügen |

## 2. Standalone-MCP-Server (headless, Qt-frei)

`tools/mcp_server` baut `fuhglgraphics_mcp_server` — spricht MCP auf stdio, damit **jeder**
MCP-Client (oder der Asset-Creator über ein Server-Kommando) den Katalog nutzen kann; headless
bedient er Szene-/Render-/Wasser-/Prozedural-Daten-Tools (attach antwortet `attached: false`):

```bash
printf '%s\n' \
 '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
 '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
 '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"render_set_effect","arguments":{"effect":"bloom","enabled":true}}}' \
 '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"render_set_param","arguments":{"name":"bloomThreshold","value":2.5}}}' \
 '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"procedural_wfc","arguments":{"preset":"dungeon","width":6,"height":6,"seed":3}}}' \
 | ./fuhglgraphics_mcp_server
# id 3 -> {"effect":"bloom","enabled":true,"supported":true}
# id 4 -> {"name":"bloomThreshold","value":2.5}
# id 5 -> {"preset":"dungeon","cells":[{"x":0,"z":0,"tile":"pillar","rot":2}, ...], ...}
```

## 3. Das Asset-Creator-Tool

```bash
fuhglgraphics_asset_creator [--project scene.fasset] [--lang en|de]
```

- **Linkes Dock:** Szenenbaum (Kind/Shape/Licht/Wasser hinzufügen, glTF laden, entfernen),
  Inspektor (Name, TRS, per-Node-Effekt-Toggles, gängige Renderer-Parameter, G13-Wasser-Parameter),
  Asset-Browser (Projektordner; Doppelklick auf `.gltf/.glb` lädt unter die Selektion;
  P5: `.gltf/.glb`-Zeilen zeigen gerenderte **Thumbnails** aus `<projekt>/.cache/thumbs`).
- **Mitte:** G3-Viewport — Orbit-Kamera (links ziehen = drehen, rechts/mitte = pan, Rad = Zoom),
  Klick pickt (`scene::pick`); alle Edits laufen als `QUndoCommand` (Undo/Redo im Edit-Menü).
  P5: ein **3D-Gizmo** am selektierten Node (Ansicht-Menü oder `T`/`R`/`S` für
  Verschieben/Drehen/Skalieren — die farbigen Achsen/Ringe ziehen, der zentrale Würfel =
  Bewegung in der Bildebene / gleichmäßige Skalierung; jeder Drag ist ein undo-fähiger
  Command) und **Animations-Playback** (Ansicht → Animation abspielen oder `Leertaste`;
  die Node-TRS beim Start werden beim Stoppen wiederhergestellt).
- **Rechtes Dock:** MCP-Chat. Einstellungsdialog: Server-Kommando — externer MCP-Server
  (stdio, JSON-RPC 2.0; API-Keys kommen aus der Umgebung, nie aus der Projektdatei) oder
  `builtin`, das den In-Repo-Katalog **in-process** gegen das geöffnete Dokument serviert.
  Chat-Nachricht eingeben oder Tool direkt aufrufen: `/procedural_lsystem {"preset":"tree","attach":true}`.
  Tool-Ergebnisse mit einem `assetAction`-Objekt werden als undo-fähige Commands angewendet
  (LLM-→Editor-Brücke).
- **Projektdatei:** eigenes `.fasset`-JSON (`io::Json` + `scene::serialize` + `io::writeJson`);
  importierte Assets werden per Content-Hash in `<projekt>/.cache` gecacht (G11 `io::AssetCache`).
- **UI-Sprache:** Englisch Default, Deutsch als erste Übersetzung (`--lang de`, QTranslator).

## 4. Bibliothek-seitiges JSON für Render-Settings (G11-Config)

`scene::effectsToJson` / `scene::effectsFromJson` / `scene::setEffectParam` /
`scene::effectParamToJson` / `scene::effectParamNames` (in `scene/serialize.hpp`) bilden die
gesamten `Effects` (Liste + Parameter) von/nach JSON ab — genutzt von den MCP-Render-Tools,
von `.fasset`-Dateien und jeder JSON-Renderer-Config.

## 5. Tests / CI

- `tests/test_mcp.cpp` (Label `unit`, ohne GL/Qt): JSON-RPC-Roundtrips + Framing, der komplette
  Katalog (Szenen-/Render-/Wasser-/Prozedural-Tools inkl. Attach-Pfad mit GL-freiem Resolver),
  der stdio-Server Ende-zu-Ende über Stringstreams, `io::AssetCache`.
- CI `build-and-test`: stdio-Smoke von `fuhglgraphics_mcp_server` (initialize → tools/list →
  `render_set_param` / `procedural_noise` / `procedural_wfc`) und
  `xvfb-run fuhglgraphics_asset_creator --version` (Linux-WITH_QT-Job).
