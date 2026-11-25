# Tutorial G12 — Asset creator Qt tool + MCP (LLM bridge)

> 🇩🇪 **Deutsche Version:** [docs/de/tutorials/g12_asset_creator.md](../../de/tutorials/g12_asset_creator.md)

**Goal:** the G12 tool `fuhglgraphics_asset_creator` (`tools/asset_creator/`, built with
`FUHGLGRAPHICS_WITH_QT=ON` + `FUHGLGRAPHICS_BUILD_TOOLS=ON`): create/load/edit assets with a
data-structures dock (scene tree + inspector + asset browser), a viewport rendered by the G3
`DeferredRenderer`, and an LLM chat over **MCP** — where the MCP surface also exposes the
**procedural functions (G24–G31)** and the **deferred-render settings (incl. G13 water)**.
Design: [docs/ASSET_CREATOR.md](../ASSET_CREATOR.md). The Qt-free MCP layer lives in the
library: `include/fuhglgraphics/mcp/`.

## 1. The MCP layer (Qt-free, unit-tested — `mcp/`)

```
mcp/jsonrpc.hpp        JSON-RPC 2.0 build/parse (request/notification/response/error),
                       line framing (extractLine), id matching — on top of io::Json
mcp/tool_catalog.hpp   the tool catalog: toolsListJson() (MCP tools/list) + callTool()
                       (tools/call dispatch) against a ToolHost
mcp/mcp_server.hpp     stdio MCP server: initialize / notifications/initialized / ping /
                       tools/list / tools/call (one JSON-RPC message per line)
```

`ToolHost` binds what a tool may touch: the `scene::Scene*`, the render-settings target
`scene::Effects*` (effect list + `EffectParams`), the active G13 `water::WaterSurface*`,
and the GL resolvers (`meshResolver` for shape specs, `meshDataResolver` for generated
`MeshData`, `waterCreate` for the water grid). Without GL resolvers the tools still work —
they return the generated **data** and answer `"attached": false`.

### Tool groups

| Group | Tools |
| --- | --- |
| Scene | `scene_list_nodes`, `scene_add_node`, `scene_remove_node`, `scene_set_trs`, `scene_add_shape` (box/quad/sphere), `scene_add_light` |
| Deferred-render settings | `render_list_effects` (all `render::Effect` + supported flag), `render_set_effect {effect, enabled}`, `render_get_settings`, `render_set_param {name, value}` (every `EffectParams` field, e.g. `bloomThreshold`, `tonemapOperator: "reinhard"`), `render_set_params {params:{...}}` |
| G13 water | `water_add_to_scene`, `water_get_params`, `water_set_params` (Gerstner `waves[]`, colors, foam, reflection strength, ...) |
| Procedural G24–G31 | `procedural_noise` (value/perlin/simplex × 2D/3D + fbm/ridged/turbulence), `procedural_marching_cubes` (mandelbulb/sphere/box/blob fields), `procedural_lsystem` (tree/fern/bush), `procedural_wfc` (dungeon/city), `procedural_voronoi` (cells + extrusion), `procedural_sdf` (mandelbulb/roundedBox/sphere/box/cylinder), `procedural_creature` (quadruped/reptile), `procedural_worldgen` (landscape/city) — each with `attach: true` to add the result as a scene node |

## 2. Standalone MCP server (headless, Qt-free)

`tools/mcp_server` builds `fuhglgraphics_mcp_server` — speaks MCP on stdio, so **any** MCP
client (or the asset creator via a server command) can use the catalog; headless it serves
scene/render/water/procedural-data tools (attach answers `attached: false` without GL):

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

## 3. The asset creator tool

```bash
fuhglgraphics_asset_creator [--project scene.fasset] [--lang en|de]
```

- **Left dock:** scene tree (add child/shape/light/water, load glTF, remove), inspector
  (name, TRS, per-node effect toggles, common renderer params, G13 water params), asset
  browser (project folder; double-click a `.gltf/.glb` to load it under the selection).
- **Center:** G3 viewport — orbit camera (left-drag rotate, right/middle-drag pan, wheel
  zoom), click to pick (`scene::pick`), all edits as `QUndoCommand`s (undo/redo in the Edit
  menu).
- **Right dock:** MCP chat. Settings dialog: server command — an external MCP server
  (stdio, JSON-RPC 2.0; API keys come from the environment, never from the project file)
  or `builtin`, which serves the in-repo catalog **in-process** against the open document.
  Type a message to chat, or call a tool directly: `/procedural_lsystem {"preset":"tree","attach":true}`.
  Tool results carrying an `assetAction` object are applied as undoable commands
  (LLM → editor bridge).
- **Project file:** own `.fasset` JSON (`io::Json` + `scene::serialize` + `io::writeJson`);
  imported assets are content-hash cached in `<project>/.cache` (G11 `io::AssetCache`).
- **UI language:** English default, German first translation (`--lang de`, QTranslator).

## 4. Library-side JSON for render settings (G11 config)

`scene::effectsToJson` / `scene::effectsFromJson` / `scene::setEffectParam` /
`scene::effectParamToJson` / `scene::effectParamNames` (in `scene/serialize.hpp`) map the
whole `Effects` (list + params) to/from JSON — used by the MCP render tools, by `.fasset`
files and by any JSON renderer config.

## 5. Tests / CI

- `tests/test_mcp.cpp` (label `unit`, no GL/Qt): jsonrpc round-trips + framing, the full
  catalog (scene/render/water/procedural tools incl. the attach path with a GL-free
  resolver), the stdio server end-to-end over stringstreams, `io::AssetCache`.
- CI `build-and-test`: stdio smoke of `fuhglgraphics_mcp_server` (initialize → tools/list
  → `render_set_param` / `procedural_noise` / `procedural_wfc`) and
  `xvfb-run fuhglgraphics_asset_creator --version` (Linux WITH_QT job).
