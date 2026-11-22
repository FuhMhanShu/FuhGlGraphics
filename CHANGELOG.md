# Changelog

All notable changes to FuhGlGraphics are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) (the CMake project version is
`0.1.0` with same-major compatibility).

## [Unreleased]

### Added

- Initial skeleton with agreed requirements G1–G11 in
  [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) plus the dependency wiring:
  CMake ≥ 3.20 static library `fuhglgraphics` (alias/export
  `FuhGlGraphics::fuhglgraphics`, install + `find_package(FuhGlGraphics)`),
  direct FuhGLCore dependency resolved via
  `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>` (add_subdirectory) →
  `find_package(FuhGLCore)` → FetchContent fallback from
  `github.com/FuhMhanShu/FuhGLCore` (`FUHGLGRAPHICS_FUHGLCORE_GIT_TAG`, default
  `main`); optional Qt6 (`FUHGLGRAPHICS_WITH_QT=ON` default, OFF = Qt-free
  headless core); options `FUHGLGRAPHICS_BUILD_TESTS` /
  `FUHGLGRAPHICS_ENABLE_HARDENING`; version API
  (`include/fuhglgraphics/version.hpp`, `src/version.cpp`) + umbrella header
  `graphics.hpp`; mini test runner with unit tests (`ctest -L unit`);
  docs (README.md/README.de.md, docs/ with language picker, USER_GUIDE.md,
  DEVELOPMENT.md, Doxyfile, en/de tutorial placeholders), CI matrix
  (Linux with Qt6, win/mac Qt-free), SECURITY.md, .gitignore.
- qmake support: `FuhGlGraphics.pro` builds the standalone static library
  (`qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" FuhGlGraphics.pro && make` →
  `libfuhglgraphics.a`), pulling in FuhGLCore via `fuhglcore.pri`
  (`FUHGLCORE_DIR` as qmake var or environment, `error()` when unset);
  `FUHGLGRAPHICS_WITH_QT=1` (default) adds `QT += widgets openglwidgets` +
  `automoc` + `FUHGLGRAPHICS_WITH_QT=1`, `0` builds the Qt-free headless core.
  Source lists use per-directory globs (no `**`, which expands to nothing on
  some Qt6 qmake versions).
