# FuhGlGraphics — Asset Creator (G12) design

Status: **design agreed, implementation after review.** Language of this document:
English (docs at this level are English-only, see [README.md](README.md)).
Code, comments and API docs stay English; the tool UI is multilingual (see §2).

## 1. Goal

A Qt6 desktop tool — the **asset creator** — to **create, load and edit assets**:

- a **data-structures area** (scene tree + inspector + asset browser),
- a **viewport with editing tools** that renders through the G3 `DeferredRenderer`
  of this library (`fuhglgraphics::render`),
- a **chat area** to talk to an LLM over **MCP** (Model Context Protocol).

The tool is a separate executable on top of the library; the library core stays
Qt-free (`-DFUHGLGRAPHICS_WITH_QT=OFF` still builds headless).

Planned layout (mockup — left: scene tree + inspector + asset browser, center: G3
renderer viewport with gizmo toolbar, right: MCP chat):

![Asset creator UI mockup](images/g12_asset_creator_mockup.png)

## 2. Agreed decisions

| Topic | Decision |
|---|---|
| Location | `tools/asset_creator/` in this repo; executable target `fuhglgraphics_asset_creator`, links `FuhGlGraphics::fuhglgraphics` + `Qt6::Widgets Qt6::OpenGLWidgets` (AUTOMOC); qmake alternative `tools/asset_creator/asset_creator.pro` |
| Build | new CMake option `FUHGLGRAPHICS_BUILD_TOOLS` (default ON when `FUHGLGRAPHICS_WITH_QT=ON`) → `add_subdirectory(tools)`; CI (ubuntu, `qt6-base-dev`) builds the tool, win/mac keep building the Qt-free core |
| Project file | own **`.fasset` JSON** (`io::Json` + new `io::writeJson`); glTF/texture references stored as paths; glb export is a later phase |
| MCP | **stdio transport** with a **configurable server command** (settings dialog, never hardcoded; no secrets in the project file — API keys come from the environment). JSON-RPC 2.0, line-delimited; handshake `initialize` → `notifications/initialized`, then `tools/list` / `tools/call`; server→client requests (e.g. `sampling/createMessage`) and HTTP/streamable transport are later phases |
| MCP server for testing | recommendation for a Mistral subscription: run a small own MCP server (Python or Node) that reads `MISTRAL_API_KEY` from the env and exposes the Mistral chat API as an MCP tool; any other MCP server works the same via the server-command setting — no vendor lock-in |
| UI language | **multilingual, English default**, German as the first translation (`QTranslator` + `.ts` files); code/comments/docs stay English |
| Conventions | C++17, MVC (model = asset/scene data, view = panels/viewport, controller = input/chat), PIMPL for GL/Qt-resource classes, bool + log instead of exceptions for domain errors, compact documented code, descriptive variable names (see [DEVELOPMENT.md](DEVELOPMENT.md)) |

## 3. Architecture (MVC)

```
tools/asset_creator/
  main.cpp                    QApplication, MainWindow, --project x.fasset CLI
  mainwindow.{hpp,cpp}        View: QMainWindow, 3 QDockWidgets (data structures |
                              viewport | chat), menus/toolbar (new/open/save, undo/redo,
                              connect MCP)
  viewport_widget.{hpp,cpp}   View: QOpenGLWidget — initializeGL() -> DeferredRenderer::
                              create(w,h,msaa); resizeGL() -> resize(); paintGL() ->
                              Scene::updateWorldTransforms/fillFrame, effects.applyTo,
                              renderer.render(frame.view, &materialIds, &castShadow,
                              &particleSystems), blit output FBO to the default FB;
                              render on demand (dirty flag) + QTimer while animating
  asset_document.{hpp,cpp}    Model: owns scene::Scene + GL resources (io::instantiateGltf),
                              QUndoStack, dirty flag; new/open/save .fasset via
                              scene::serialize + io::writeJson; load .gltf/.glb via io::loadGltf
  scene_tree_model.{hpp,cpp}  Model adapter: QAbstractItemModel over scene::Node*
                              (internalPointer); refreshed after each QUndoCommand
                              (the Qt-free core has no change signals)
  scene_tree_panel.{hpp,cpp}  View: QTreeView + context menu (add/remove/rename node,
                              attach child, load glTF here)
  inspector_panel.{hpp,cpp}   View: properties of the selection — node TRS, MeshEntry
                              (albedo color, metallic/roughness/AO, texture paths,
                              castShadow, materialId), LightComponent, per-node Effects +
                              EffectParams, Instancing; edits go back as QUndoCommands
  asset_browser_panel.{hpp,cpp} + asset_browser_model  View/Model: QFileSystemModel on the
                              project asset folder, filter *.gltf *.glb *.png *.fasset;
                              double-click / drag&drop -> LoadGltfCommand; thumbnails later
  commands.{hpp,cpp}          Controller/Model: QUndoCommands (AddNode, RemoveNode,
                              SetTrs, SetMaterial, AttachComponent, LoadGltf, ...)
                              applied to the model + viewport->update()
  viewport_controller.{hpp,cpp}  Controller: orbit camera (fuhgl::Camera), picking ray
                              vs. node world AABBs for selection, keyboard translation;
                              3D gizmo (translate/rotate/scale axes) in a later phase
  chat_panel.{hpp,cpp}        View: QListWidget (user/assistant/tool/system roles),
                              input line, status label, MCP settings dialog
  chat_controller.{hpp,cpp}   Controller: ChatPanel <-> McpClient; tools/list cached on
                              connect; chat message sent as tools/call to the configured
                              chat tool (args: message + history); content[] text parts and
                              tool calls shown in the panel; tool results carrying an
                              `assetAction` JSON are translated locally into QUndoCommands
                              (LLM -> editor bridge)
  mcp/mcp_client.{hpp,cpp}    Controller: QProcess spawns the MCP server (stdio),
                              JSON-RPC 2.0 line-delimited on stdin/stdout, async Qt
                              signals (messageReceived, toolResult, error); framing/parsing
                              via the Qt-free fuhglgraphics::mcp::jsonrpc helpers
```

## 4. Library extensions (Qt-free, unit-testable)

- `io::writeJson(const io::Json&, std::string&, std::string* err)` in
  `include/fuhglgraphics/io/json.hpp` / `src/io/json.cpp` — the existing parser has only a
  reader; MCP requests and `.fasset` saving need a writer. Test: roundtrip in
  `tests/test_io.cpp` (label `unit`).
- `include/fuhglgraphics/scene/serialize.hpp` — `serializeScene(const scene::Scene&, io::Json&)`
  / `deserializeScene(...)`: node tree (name, TRS, component parameters, material factors,
  texture/asset paths, effects, instancing). GL objects (`Mesh*`, `Texture*`) are stored as
  path references and recreated on load. Test: `tests/test_scene_serialize.cpp` (`unit`).
- `include/fuhglgraphics/mcp/jsonrpc.hpp` — Qt-free JSON-RPC 2.0 message build/parse helpers
  on top of `io::Json` (request/response/notification, id matching). Test:
  `tests/test_mcp.cpp` (`unit`).

All three keep the `WITH_QT=OFF` CI jobs green (no Qt includes).

## 5. The three areas

1. **Data structures (left dock):** scene tree (`SceneTreeModel`) + inspector +
   asset browser. Selection is shared between tree and viewport via the controller.
   Every edit goes through a QUndoCommand → model changes `scene::Scene` → viewport
   marks dirty.
2. **Viewport (center):** `ViewportWidget` with the G3 `DeferredRenderer` (MSAA, render
   scale, per-node effects like `examples/scene_screenshot`). Editing tools: selection,
   orbit camera, axis translation, component attach, glTF drag&drop; 3D gizmo later.
3. **Chat (right dock):** `ChatPanel` + `ChatController` + `McpClient` (stdio). The MCP
   server hosts the LLM and optionally asset tools; answers/tool calls appear in the
   chat history, asset actions are applied as commands on the model.

## 6. `.fasset` project format (sketch)

```json
{
  "format": "fuhglgraphics-asset",
  "version": 1,
  "name": "my_asset",
  "assets": [ { "id": "ship", "kind": "gltf", "path": "assets/ship.glb" } ],
  "scene": { "nodes": [ { "name": "root-node",
      "translation": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1],
      "components": [ { "type": "mesh", "asset": "ship", "albedo": [1,1,1],
                        "metallic": 0.0, "roughness": 0.5, "castShadow": true } ],
      "effects": { "list": ["shadows"], "params": {} },
      "instancing": { "enabled": false, "count": 0 },
      "children": [] } ] }
}
```

## 7. MCP client design

- **Transport (phase 1):** stdio — `QProcess` starts the configured server command;
  JSON-RPC 2.0 messages as single lines on stdout/stdin.
- **Handshake:** `initialize` (protocolVersion, capabilities) → `notifications/initialized`;
  then `tools/list` (cached, shown in the chat panel), `tools/call` for chat + asset tools.
- **Chat flow:** user message → `tools/call { name: <chatTool>, arguments: { message,
  history } }` → response `content[]` text parts rendered as assistant messages; tool calls
  and results rendered as tool entries; errors as system entries.
- **LLM → editor bridge:** tool results may carry an `assetAction` object
  (e.g. `{ "op": "addNode", "name": "box", "translation": [0,1,0] }`); `ChatController`
  maps known ops onto QUndoCommands.
- **Security:** server command + env come from the settings dialog / env only; no
  credentials in `.fasset`; child process gets a clean, explicit environment.
- **Reference dev server:** `tools/asset_creator/mcp_server.py` — Python 3 stdlib only
  (no pip), speaks MCP over stdio (line-delimited JSON-RPC 2.0). Tools: `chat` (Mistral
  API via `MISTRAL_API_KEY`/`MISTRAL_MODEL` env, offline stub without a key),
  `list_assets`, `inspect_gltf` (.gltf/.glb parse), `render_preview` (runs the compiled
  `fuhglgraphics_gltf_demo` headless through the G3 renderer, returns the PNG as MCP
  image content), `asset_action` (returns the `assetAction` JSON the editor maps onto
  QUndoCommands), `editor_command` (appends to `EDITOR_BRIDGE_FILE` for direct control,
  e.g. via QFileSystemWatcher). Optional `--http <port>` loopback mode
  (127.0.0.1 only: `/health`, `/tools`, `POST /call`). Tests:
  `python3 tools/asset_creator/test_mcp_server.py`.
- **Later:** HTTP/streamable transport via `Qt6::Network`; server→client
  `sampling/createMessage` for real LLM sampling; client-side MCP tools.

## 8. Build / CI / tests / docs

- CMake `FUHGLGRAPHICS_BUILD_TOOLS` (default ON with `WITH_QT`) → `tools/`;
  target links `fuhglgraphics` + `Qt6::Widgets Qt6::OpenGLWidgets`; qmake `.pro`.
- CI: ubuntu job (`qt6-base-dev`) additionally builds `fuhglgraphics_asset_creator`;
  smoke = build + `--version` under xvfb. win/mac: unchanged (Qt-free core).
- Tests: `tests/test_io.cpp` (writeJson), `tests/test_scene_serialize.cpp`,
  `tests/test_mcp.cpp` — all label `unit`, no GL, no Qt.
- Docs: this design doc; after implementation: REQUIREMENTS G12, USER_GUIDE,
  DEVELOPMENT layout, tutorial `docs/en/tutorials/g12_asset_creator.md` +
  `docs/de/tutorials/`, README/README.de, CHANGELOG.

## 9. Phases

1. **P1 skeleton:** CMake/qmake target, `io::writeJson`, `scene/serialize`,
   MainWindow + `ViewportWidget` + new/open/save `.fasset` — runnable tool rendering a scene.
2. **P2 data structures:** SceneTreeModel, inspector, selection, QUndoCommands,
   context menu, primitives (box/plane/sphere from `MeshData`).
3. **P3 assets:** asset browser, glTF drag&drop, project asset folder.
4. **P4 MCP chat:** `mcp::jsonrpc` + `McpClient` (stdio), ChatPanel/ChatController,
   settings dialog, tool-result → command bridge.
5. **P5 polishing:** 3D gizmo, thumbnails, animation playback in the viewport,
   glb export, CI smoke.

Delivery: branch `vibe/<slug>-<id>`, one PR per phase after review (per
[DEVELOPMENT.md](DEVELOPMENT.md) "Adding a feature").

## 10. Open points

- 3D gizmo rendering path (overlay meshes in a second `SceneView` vs. FuhGLCore blitter).
- Thumbnail generation (separate small render vs. cached PNG in the asset cache, G11).
- Exact MCP chat-tool contract (name/arguments) — defined by the configured server;
  the tool stays generic and shows `tools/list` results.
