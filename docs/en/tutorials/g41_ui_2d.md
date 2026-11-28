# Tutorial G41 — 2D engine / UI layer (`ui::UiRoot`, `ui::Batch2D`, widgets, binding, G3 integration)

> 🇩🇪 **Deutsche Version:** [docs/de/tutorials/g41_ui_2d.md](../../de/tutorials/g41_ui_2d.md)

**Goal:** a complete, Qt-free 2D engine on top of FuhGLCore — batched rendering, text,
input, layouts, widgets (checkbox, list, text field, …), two-way data binding to a backend,
and two ways to integrate it into the G3 deferred 3D renderer: a screen-space overlay
post pass and a menu on a 3D screen model (world-space UI with click picking).

All in the new `ui/` module (`include/fuhglgraphics/ui/`, `src/ui/`, namespace
`fuhglgraphics::ui`). GL 3.3 baseline, no new dependencies, PIMPL + `bool`+log like the
rest of the library.

## Architecture

```
Event (input.hpp) ──▶ UiRoot::dispatch ──▶ Widget::onEvent (hit-test, capture, focus)
                         │                        │
                         │                        ▼
                         │              Property<T> / Signal (binding.hpp)  ◀── backend model
                         │                        │
                         ▼                        ▼
              UiRoot::render ──▶ Batch2D (batch2d.hpp) ──▶ any framebuffer
                                               │
                        ┌──────────────────────┴───────────────────────┐
                        ▼                                              ▼
        makeOverlayPass (overlay.hpp):                     WorldSurface (overlay.hpp):
        G40 post pass after the tonemap                   UI → offscreen texture → albedo
        (screen-space HUD, LDR)                           of a 3D screen mesh; pickUv()
                                                          maps scene::pick hits to UV
```

## 1. Batch2D — the renderer

One dynamic VBO, one GLSL 330 program; everything queued per frame and drawn in as few
draw calls as texture switches allow. Coordinates are pixels, (0,0) top-left, y down.

```cpp
fuhglgraphics::ui::Batch2D batch;
batch.create();                       // needs a current GL context
batch.begin(1280, 720);               // viewport + ortho (y down) + blending
batch.fillRect({10, 10, 200, 100}, {0.2f, 0.2f, 0.25f, 1.f}, nullptr, {}, 6.f); // rounded
batch.outlineRect({10, 10, 200, 100}, {1, 1, 1, 1}, 1.f);
batch.line(0, 0, 100, 50, {1, 0, 0, 1}, 2.f);
fuhglgraphics::ui::Font font{1.5f};   // 8 px cells * 1.5 = 12 px line height
batch.text(font, "Hello", 20, 20, {1, 1, 1, 1});
batch.pushScissor({10, 10, 200, 100}); // clip (stacked, intersected)
batch.fillRect({0, 0, 500, 500}, {0, 1, 0, 0.5f});
batch.popScissor();
batch.end();                          // flush + restore GL state
```

Text uses the embedded public-domain 8x8 font atlas (`fuhgl::render::font8x8Glyph`,
uploaded by `Batch2D::create()`); `Font::measure()` / `Font::wrap()` give metrics and
word wrap (pure CPU, unit-tested).

## 2. Input + UiRoot — event dispatch, focus, capture

The app translates its native events (Qt, game loop, tests) into `ui::Event` and calls
`UiRoot::dispatch()`:

```cpp
fuhglgraphics::ui::UiRoot ui;
ui.create();                          // creates the Batch2D
ui.setSize(1280, 720);
auto& panel = ui.root.add<ui::Panel>();   // build the tree under ui.root
// ... add widgets, set layouts ...
ui.layout();                          // layout pass (hit-testing works from here, no GL)

ui::Event e;
e.type = ui::EventType::MouseButton; e.x = 50; e.y = 60;
e.button = ui::MouseButton::Left; e.down = true;
ui.dispatch(e);                       // → hit-test → focus → widget->onEvent
// per frame:
ui.update(dt);
ui.render();                          // layout + batch.begin/render/end
```

`UiRoot` handles hover, mouse capture (the widget that got mouse-down receives move/up —
slider drags), click focus, Tab cycling between `focusable` widgets, Escape (unfocus) and
routes keys/chars to the focused widget.

## 3. Widgets + layouts

```cpp
panel.title = "Settings";
panel.setLayout(std::make_unique<ui::VBoxLayout>());   // or HBoxLayout / GridLayout / FreeLayout (default)
auto& label = panel.add<ui::Label>();  label.text = "Volume";
auto& cb    = panel.add<ui::Checkbox>(); cb.label = "Enabled";
auto& sl    = panel.add<ui::Slider>();  // sl.value: Property<float> in [0,1]
auto& le    = panel.add<ui::LineEdit>(); // caret, selection, clipboard, validator, committed
auto& lb    = panel.add<ui::ListBox>(); lb.items = {"a", "b", "c"}; // wheel scroll, lb.selected
auto& btn   = panel.add<ui::Button>();  btn.text = "OK"; btn.clicked.connect([]{ ... });
```

`rect` is relative to the parent's content area; layouts compute it each frame
(`sizeHint.prefW/prefH/minW/minH/stretch`). `Panel` draws background + title bar and lays
out below it. Colors/metrics come from `ui.theme` (`Theme`, default dark).

## 4. Backend ↔ GUI data binding (Qt-free)

A backend value lives in exactly one `ui::Property<T>`; widgets expose their own
`Property<T>`; couple them with `bindTwoWay`:

```cpp
ui::Property<float> exposure{1.0f};          // the backend model value
ui::bindTwoWay(exposure, sl.value);           // two-way, loop-free (change-only + re-entrancy guard)
// one-way (model → widget):  ui::bindOneWay(exposure, progressBar.value);
// react to changes:         exposure.onChanged().connect([]{ ... });
```

`Property::set()` fires only on an actual change and queues re-entrant writes, so two-way
bindings cannot ping-pong. Widget edits (LineEdit commit, checkbox toggle, …) land in the
property; backend changes (MCP tools, undo stack, weather system) land in the widgets —
one source of truth.

## 5. Integration into the G3 deferred renderer

**Screen-space overlay** (HUD over the tonemapped frame, unaffected by DoF/bloom/TAA) —
a G40 custom post pass:

```cpp
fuhglgraphics::render::DeferredRenderer renderer;
renderer.create(w, h, 0);
renderer.addPostPass(fuhglgraphics::ui::makeOverlayPass(hudUi)); // hudUi: ui::UiRoot
// per frame: renderer.render(scene, ...);  — the "ui" pass draws the HUD last
```

**World-space UI** (menu on a 3D screen model) — render the UI into a texture and bind it
as the screen mesh's albedo; map clicks with `WorldSurface::pickUv`:

```cpp
fuhglgraphics::ui::WorldSurface surface;
surface.create(512, 300);
// per frame, before renderer.render():
surface.render(menuUi);                                   // UI → surface texture
auto& se = screenNode.attach<scene::MeshComponent>().addMesh(&screenMesh);
se.albedoTexture = &surface.texture();
se.meshData = &screenData;                                // CPU triangles for picking
// on a mouse click at viewport pixel (px, py):
float u, v;
if (ui::WorldSurface::pickUv(scene, cam, vpW, vpH, px, py, &screenNode, u, v)) {
    ui::Event e; e.type = ui::EventType::MouseButton; e.button = ui::MouseButton::Left;
    e.down = true; e.x = u * surface.width(); e.y = v * surface.height();
    menuUi.dispatch(e);                                   // ... and the release event likewise
}
```

`pickUv` reuses G18 `scene::pick`, re-runs the Möller–Trumbore test on the hit triangle in
world space to recover the barycentric coordinates and interpolates the mesh UVs.

## 6. Example + tests

- `examples/ui_screenshot` (`fuhglgraphics_ui_screenshot --out ui.png`): 3D scene with a
  monitor whose screen shows the menu (WorldSurface), a HUD overlay pass, and a menu
  slider bound to the renderer exposure. Headless → PNG (`docs/images/g41_ui.png`).
- `tests/test_ui.cpp`: pure-CPU unit tests (Rect, Font wrap, Property/Binding loop-freedom,
  layouts, hit-testing, dispatch, all widgets) — run via `fuhglgraphics_tests`.
- `tests/gl_smoke_ui.cpp`: headless GL smoke test (WorldSurface pixels, overlay pass
  pixels in `output()`, `pickUv` center → uv ≈ (0.5, 0.5)); skips with exit 77 without GL.

## Extension ideas

TTF atlas backend behind `ui::Font` (rasterized via Qt when `FUHGLGRAPHICS_WITH_QT=ON`),
scrollbars/draggable windows, UI layout serialization via `io::json` (G19), MCP tools that
drive UI state (`mcp::ToolHost` → `Property` → widgets).
