# FuhGlGraphics

**Ein plattformübergreifendes 2D/3D-Grafik-Framework mit Asset-Importer, aufgebaut auf
[FuhGLCore](https://github.com/FuhMhanShu/FuhGLCore).** FuhGlGraphics ist die
Anwendungsschicht über dem GL-/Fenster-/Math-Kern von FuhGLCore: darauf kommen ein neuer
Deferred-Renderer, glTF-2.0-Asset-Loading, ein Szenengraph, GPU-Partikel, Animation,
Light-Probes, Decals und Engine-Infrastruktur (Asset-Cache, JSON-Konfig, minimaler
Qt6-Editor) — siehe die vereinbarten Anforderungen [G1–G11](docs/REQUIREMENTS.md).

🇬🇧 [English version of this README](README.md)

## Abhängigkeiten

- **FuhGLCore (direkt)** — GL-Loading, Windowing, Mathe, Pipelines. CMake-Auflösung in
  dieser Reihenfolge: `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<Pfad>` (lokales Checkout,
  `add_subdirectory`) → `find_package(FuhGLCore)` (installiert) → FetchContent-Fallback
  von `github.com/FuhMhanShu/FuhGLCore` (`FUHGLGRAPHICS_FUHGLCORE_GIT_TAG`, default
  `main`). Wird als `FuhGLCore::fuhglcore` (PUBLIC) gelinkt. In qmake-Projekten:
  `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" ...` + `include($$FUHGLCORE_DIR/fuhglcore.pri)`.
- **Qt6 (direkt, optional)** — nur für Editoren/Tools (`editor/`, gebaut mit
  `FUHGLGRAPHICS_WITH_QT=ON`, default). CMake: `find_package(Qt6 COMPONENTS Widgets
  OpenGLWidgets)`; qmake: `QT += widgets openglwidgets`. Der Bibliothekskern bleibt
  Qt-frei; mit `-DFUHGLGRAPHICS_WITH_QT=OFF` headless bauen.
- Sonst nur die C++17-Standardbibliothek (eigener minimaler JSON-Parser, eigene
  Test-Fixtures — kein nlohmann, keine vendored CC-BY-Sample-Assets).

## Status

Skeleton + Requirements liegen vor ([docs/REQUIREMENTS.md](docs/REQUIREMENTS.md), G1–G11);
die Implementierung startet mit **G3** (Deferred-Renderer) und **G4** (glTF-2.0-Asset-Loading).
Baseline: OpenGL 3.3 Core (GL 4.3 gatet GPU-Partikel/Skinning, macOS ≤ 4.1), übernommen
von FuhGLCore.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFUHGLGRAPHICS_WITH_QT=OFF \
    -DFUHGLGRAPHICS_FUHGLCORE_DIR=/path/to/FuhGLCore   # optional, siehe oben
cmake --build build
ctest --test-dir build --output-on-failure -L unit
cmake --install build --prefix <prefix>   # stellt find_package(FuhGlGraphics) bereit
```

Optionen: `-DFUHGLGRAPHICS_WITH_QT=ON|OFF` (default ON),
`-DFUHGLGRAPHICS_BUILD_TESTS=ON` (default), `-DFUHGLGRAPHICS_ENABLE_HARDENING=ON` (default;
Compiler-/Linker-Härtung, siehe [DEVELOPMENT.md](docs/DEVELOPMENT.md)),
`-DFUHGLGRAPHICS_FUHGLCORE_DIR=<Pfad>`, `-DFUHGLGRAPHICS_FUHGLCORE_GIT_TAG=<Ref>`.

Installiertes Paket konsumieren:

```cmake
find_package(FuhGlGraphics REQUIRED)
target_link_libraries(your_app PRIVATE FuhGlGraphics::fuhglgraphics)
```

**qmake-Alternative** (Qt-Buildsystem): `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore"
FuhGlGraphics.pro && make` baut die statische Bibliothek `libfuhglgraphics.a`.
`FUHGLGRAPHICS_WITH_QT=0` (qmake-Variable) baut den Qt-freien Headless-Kern.

## Dokumentation

- [Dokumentationsübersicht / Language picker](docs/README.md)
- [User Guide (Englisch)](docs/USER_GUIDE.md) — Konzepte (beschreibender Teil)
- [Tutorials (Deutsch)](docs/de/tutorials/README.md) · [Tutorials (English)](docs/en/tutorials/README.md)
- [Development (Englisch)](docs/DEVELOPMENT.md) — Layout, Konventionen, Build, Tests, CI (technischer Teil)
- [Requirements (Englisch)](docs/REQUIREMENTS.md) — die vereinbarten Anforderungen G1–G11
- API-Referenz — Doxygen-HTML (lokal: `cd docs && doxygen Doxyfile`)

## Security

Sicherheitslücke gefunden? Bitte privat melden (keine öffentlichen Issues) —
siehe [SECURITY.md](SECURITY.md).

## Lizenz

MIT — siehe [LICENSE](LICENSE).
