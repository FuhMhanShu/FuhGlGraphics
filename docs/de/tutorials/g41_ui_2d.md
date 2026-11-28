# Tutorial G41 — 2D-Engine / UI-Layer (`ui::UiRoot`, `ui::Batch2D`, Widgets, Binding, G3-Integration)

> 🇬🇧 **English version:** [docs/en/tutorials/g41_ui_2d.md](../../en/tutorials/g41_ui_2d.md)

**Ziel:** eine komplette, Qt-freie 2D-Engine auf Basis von FuhGLCore — gebatchtes Rendering,
Text, Eingabe, Layouts, Widgets (Checkbox, Liste, Textfeld, …), Zwei-Wege-Datenbindung zum
Backend und zwei Arten der Integration in den G3-Deferred-Renderer: Screen-Space-Overlay als
Post-Pass und ein Menü auf einem 3D-Bildschirm-Modell (World-Space-UI mit Klick-Picking).

Alles im neuen `ui/`-Modul (`include/fuhglgraphics/ui/`, `src/ui/`, Namespace
`fuhglgraphics::ui`). GL-3.3-Baseline, keine neuen Abhängigkeiten, PIMPL + `bool`+Log wie im
Rest der Bibliothek.

## Architektur

```
Event (input.hpp) ──▶ UiRoot::dispatch ──▶ Widget::onEvent (Hit-Test, Capture, Fokus)
                         │                        │
                         │                        ▼
                         │              Property<T> / Signal (binding.hpp)  ◀── Backend-Modell
                         │                        │
                         ▼                        ▼
              UiRoot::render ──▶ Batch2D (batch2d.hpp) ──▶ jedes gebundene Framebuffer
                                               │
                        ┌──────────────────────┴───────────────────────┐
                        ▼                                              ▼
        makeOverlayPass (overlay.hpp):                     WorldSurface (overlay.hpp):
        G40-Post-Pass nach dem Tonemap                    UI → Offscreen-Textur → Albedo
        (Screen-Space-HUD, LDR)                           eines 3D-Screen-Meshes; pickUv()
                                                          bildet scene::pick-Treffer auf UV ab
```

## 1. Batch2D — der Renderer

Ein dynamisches VBO, ein GLSL-330-Programm; alles wird pro Frame gequeued und mit so wenig
Draw Calls gezeichnet, wie Textur-Wechsel erlauben. Koordinaten in Pixeln, (0,0) links oben,
y nach unten.

```cpp
fuhglgraphics::ui::Batch2D batch;
batch.create();                       // braucht aktuellen GL-Kontext
batch.begin(1280, 720);               // Viewport + Ortho (y down) + Blending
batch.fillRect({10, 10, 200, 100}, {0.2f, 0.2f, 0.25f, 1.f}, nullptr, {}, 6.f); // gerundet
batch.outlineRect({10, 10, 200, 100}, {1, 1, 1, 1}, 1.f);
batch.line(0, 0, 100, 50, {1, 0, 0, 1}, 2.f);
fuhglgraphics::ui::Font font{1.5f};   // 8-px-Zellen * 1.5 = 12 px Zeilenhöhe
batch.text(font, "Hallo", 20, 20, {1, 1, 1, 1});
batch.pushScissor({10, 10, 200, 100}); // Clipping (Stack, geschnitten)
batch.fillRect({0, 0, 500, 500}, {0, 1, 0, 0.5f});
batch.popScissor();
batch.end();                          // Flush + GL-State wiederherstellen
```

Text nutzt den eingebetteten Public-Domain-8x8-Font-Atlas (`fuhgl::render::font8x8Glyph`,
von `Batch2D::create()` hochgeladen); `Font::measure()` / `Font::wrap()` liefern Metriken
und Wortumbruch (pure CPU, unit-getestet).

## 2. Eingabe + UiRoot — Event-Dispatch, Fokus, Capture

Die App übersetzt ihre nativen Events (Qt, Game-Loop, Tests) in `ui::Event` und ruft
`UiRoot::dispatch()`:

```cpp
fuhglgraphics::ui::UiRoot ui;
ui.create();                          // erzeugt das Batch2D
ui.setSize(1280, 720);
auto& panel = ui.root.add<ui::Panel>();   // Baum unter ui.root aufbauen
// ... Widgets hinzufügen, Layouts setzen ...
ui.layout();                          // Layout-Pass (Hit-Testing ab hier, ohne GL)

ui::Event e;
e.type = ui::EventType::MouseButton; e.x = 50; e.y = 60;
e.button = ui::MouseButton::Left; e.down = true;
ui.dispatch(e);                       // → Hit-Test → Fokus → widget->onEvent
// pro Frame:
ui.update(dt);
ui.render();                          // Layout + batch.begin/render/end
```

`UiRoot` behandelt Hover, Mouse-Capture (das Widget mit Mouse-Down bekommt Move/Up —
Slider-Drag), Klick-Fokus, Tab-Wechsel zwischen `focusable`-Widgets, Escape (Fokus weg)
und routet Tasten/Zeichen an das fokussierte Widget.

## 3. Widgets + Layouts

```cpp
panel.title = "Einstellungen";
panel.setLayout(std::make_unique<ui::VBoxLayout>());   // oder HBoxLayout / GridLayout / FreeLayout (Default)
auto& label = panel.add<ui::Label>();  label.text = "Lautstärke";
auto& cb    = panel.add<ui::Checkbox>(); cb.label = "Aktiv";
auto& sl    = panel.add<ui::Slider>();  // sl.value: Property<float> in [0,1]
auto& le    = panel.add<ui::LineEdit>(); // Caret, Selektion, Zwischenablage, Validator, committed
auto& lb    = panel.add<ui::ListBox>(); lb.items = {"a", "b", "c"}; // Wheel-Scroll, lb.selected
auto& btn   = panel.add<ui::Button>();  btn.text = "OK"; btn.clicked.connect([]{ ... });
```

`rect` ist relativ zum Content-Bereich des Parents; Layouts berechnen sie jeden Frame
(`sizeHint.prefW/prefH/minW/minH/stretch`). `Panel` zeichnet Hintergrund + Titelleiste und
layed out darunter aus. Farben/Metriken kommen aus `ui.theme` (`Theme`, Default = Dark).

## 4. Backend ↔ GUI Datenbindung (Qt-frei)

Ein Backend-Wert lebt in genau einem `ui::Property<T>`; Widgets exponieren ihr eigenes
`Property<T>`; Kopplung per `bindTwoWay`:

```cpp
ui::Property<float> exposure{1.0f};          // der Backend-Modellwert
ui::bindTwoWay(exposure, sl.value);           // Zwei-Wege, schleifenfrei (nur bei Änderung + Reentrancy-Guard)
// Ein-Wege (Modell → Widget):  ui::bindOneWay(exposure, progressBar.value);
// auf Änderungen reagieren:    exposure.onChanged().connect([]{ ... });
```

`Property::set()` feuert nur bei tatsächlicher Änderung und queued re-entrant Schreibzugriffe —
Zwei-Wege-Bindings können nicht ping-pongen. Widget-Änderungen (LineEdit-Commit, Checkbox-Toggle, …)
landen in der Property; Backend-Änderungen (MCP-Tools, Undo-Stack, Wettersystem) landen in den
Widgets — eine einzige Datenquelle.

## 5. Integration in den G3-Deferred-Renderer

**Screen-Space-Overlay** (HUD über dem tonemapped Frame, unbeeinflusst von DoF/Bloom/TAA) —
ein G40-Custom-Post-Pass:

```cpp
fuhglgraphics::render::DeferredRenderer renderer;
renderer.create(w, h, 0);
renderer.addPostPass(fuhglgraphics::ui::makeOverlayPass(hudUi)); // hudUi: ui::UiRoot
// pro Frame: renderer.render(scene, ...);  — der "ui"-Pass zeichnet das HUD zuletzt
```

**World-Space-UI** (Menü auf einem 3D-Bildschirm-Modell) — UI in eine Textur rendern und als
Albedo des Screen-Meshes binden; Klicks mit `WorldSurface::pickUv` mappen:

```cpp
fuhglgraphics::ui::WorldSurface surface;
surface.create(512, 300);
// pro Frame, vor renderer.render():
surface.render(menuUi);                                   // UI → Surface-Textur
auto& se = screenNode.attach<scene::MeshComponent>().addMesh(&screenMesh);
se.albedoTexture = &surface.texture();
se.meshData = &screenData;                                // CPU-Dreiecke fürs Picking
// bei Mausklick auf Viewport-Pixel (px, py):
float u, v;
if (ui::WorldSurface::pickUv(scene, cam, vpW, vpH, px, py, &screenNode, u, v)) {
    ui::Event e; e.type = ui::EventType::MouseButton; e.button = ui::MouseButton::Left;
    e.down = true; e.x = u * surface.width(); e.y = v * surface.height();
    menuUi.dispatch(e);                                   // ... und das Release-Event ebenso
}
```

`pickUv` nutzt das G18-`scene::pick`, führt den Möller–Trumbore-Test am getroffenen Dreieck
in Weltkoordinaten erneut aus (baryzentrische Koordinaten) und interpoliert die Mesh-UVs.

## 6. Beispiel + Tests

- `examples/ui_screenshot` (`fuhglgraphics_ui_screenshot --out ui.png`): 3D-Szene mit Monitor,
  dessen Bildschirm das Menü zeigt (WorldSurface), einem HUD-Overlay-Pass und einem Menü-Slider,
  der an die Renderer-Exposure gebunden ist. Headless → PNG (`docs/images/g41_ui.png`).
- `tests/test_ui.cpp`: pure-CPU-Unit-Tests (Rect, Font-Wrap, Property/Binding-Schleifenfreiheit,
  Layouts, Hit-Testing, Dispatch, alle Widgets) — laufen über `fuhglgraphics_tests`.
- `tests/gl_smoke_ui.cpp`: headless GL-Smoke-Test (WorldSurface-Pixel, Overlay-Pass-Pixel in
  `output()`, `pickUv` in der Mitte → UV ≈ (0.5, 0.5)); skippt mit Exit-Code 77 ohne GL.

## Erweiterungsideen

TTF-Atlas-Backend hinter `ui::Font` (über Qt rasterisieren bei `FUHGLGRAPHICS_WITH_QT=ON`),
Scrollbars/verschiebhare Fenster, UI-Layout-Serialisierung über `io::json` (G19), MCP-Tools,
die UI-Zustand treiben (`mcp::ToolHost` → `Property` → Widgets).
