# FuhGlGraphics

**A cross-platform 2D/3D graphics framework with an asset importer, built on top of
[FuhGLCore](https://github.com/FuhMhanShu/FuhGLCore).** FuhGlGraphics is the application
layer above FuhGLCore's GL/window/math core: it adds a new deferred renderer, glTF 2.0
asset loading, a scene graph, GPU particles, animation, light probes, decals and engine
infrastructure (asset cache, JSON config, minimal Qt6 editor) — see the agreed
requirements [G1–G11](docs/REQUIREMENTS.md).

> 🇩🇪 **Deutsche Version:** [README.de.md](README.de.md) · Deutsche Doku: [docs/de/](docs/de/tutorials/README.md)

## Dependencies

- **FuhGLCore (direct)** — GL loading, windowing, math, pipelines. Resolved by CMake in
  this order: `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>` (local checkout, `add_subdirectory`)
  → `find_package(FuhGLCore)` (installed) → FetchContent fallback from
  `github.com/FuhMhanShu/FuhGLCore` (`FUHGLGRAPHICS_FUHGLCORE_GIT_TAG`, default `main`).
  Linked as `FuhGLCore::fuhglcore` (PUBLIC). In qmake projects:
  `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" ...` + `include($$FUHGLCORE_DIR/fuhglcore.pri)`.
- **Qt6 (direct, optional)** — only for editors/tools (`editor/`, built with
  `FUHGLGRAPHICS_WITH_QT=ON`, default). CMake: `find_package(Qt6 COMPONENTS Widgets
  OpenGLWidgets)`; qmake: `QT += widgets openglwidgets`. The library core stays Qt-free;
  build with `-DFUHGLGRAPHICS_WITH_QT=OFF` for headless use.
- Otherwise only the C++17 standard library (own minimal JSON parser, own test fixtures —
  no nlohmann, no vendored CC-BY sample assets).

## Status

Skeleton + requirements are in place ([docs/REQUIREMENTS.md](docs/REQUIREMENTS.md), G1–G11);
implementation starts with **G3** (deferred renderer) and **G4** (glTF 2.0 asset loading).
Baseline: OpenGL 3.3 core (GL 4.3 gates GPU particles/skinning, macOS ≤ 4.1), inherited
from FuhGLCore.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFUHGLGRAPHICS_WITH_QT=OFF \
    -DFUHGLGRAPHICS_FUHGLCORE_DIR=/path/to/FuhGLCore   # optional, see above
cmake --build build
ctest --test-dir build --output-on-failure -L unit
cmake --install build --prefix <prefix>   # provides find_package(FuhGlGraphics)
```

Options: `-DFUHGLGRAPHICS_WITH_QT=ON|OFF` (default ON),
`-DFUHGLGRAPHICS_BUILD_TESTS=ON` (default), `-DFUHGLGRAPHICS_ENABLE_HARDENING=ON` (default;
compiler/linker hardening flags, see [DEVELOPMENT.md](docs/DEVELOPMENT.md)),
`-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>`, `-DFUHGLGRAPHICS_FUHGLCORE_GIT_TAG=<ref>`.

Consume the installed package:

```cmake
find_package(FuhGlGraphics REQUIRED)
target_link_libraries(your_app PRIVATE FuhGlGraphics::fuhglgraphics)
```

**qmake alternative** (Qt build system): `qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore"
FuhGlGraphics.pro && make` builds the static library `libfuhglgraphics.a`.
`FUHGLGRAPHICS_WITH_QT=0` (qmake var) builds the Qt-free headless core.

## Documentation

- [Documentation overview / Sprachauswahl](docs/README.md)
- [User guide](docs/USER_GUIDE.md) — concepts (the descriptive part, English)
- [Tutorials (English)](docs/en/tutorials/README.md) · [Tutorials (Deutsch)](docs/de/tutorials/README.md)
- [Development](docs/DEVELOPMENT.md) — layout, conventions, build, tests, CI (the technical part, English)
- [Requirements](docs/REQUIREMENTS.md) — the agreed requirements G1–G11 (English)
- API reference — Doxygen HTML (locally: `cd docs && doxygen Doxyfile`)

## Security

Found a vulnerability? Please report it privately (no public issues) —
see [SECURITY.md](SECURITY.md).

## License

MIT — see [LICENSE](LICENSE).
