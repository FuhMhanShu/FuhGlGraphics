# FuhGlGraphics — Development (technical)

This is the *development-technical* part: layout, conventions, build, tests, CI.
The descriptive part is the [README](../README.md) and [USER_GUIDE.md](USER_GUIDE.md);
the agreed requirements are in [REQUIREMENTS.md](REQUIREMENTS.md).

## Repository layout

```
include/fuhglgraphics/   public API (headers)
  version.hpp             version constants + versionString()
  graphics.hpp            umbrella header (module map)
  render/                 G3 deferred renderer (implemented: deferred_renderer.hpp,
                          effects.hpp, gbuffer.hpp)
  io/                     G4 glTF 2.0 loader (planned)
  scene/                  G9 scene graph (implemented: component.hpp, effects.hpp,
                          node.hpp, scene.hpp)
  editor/                 G2+G11 minimal Qt6 editor (planned, WITH_QT only)
src/                      implementations (PIMPL targets)
  version.cpp
  render/                 G3 implementation + GLSL (deferred_renderer.cpp, deferred_glsl.hpp)
  scene/                  G9 implementation (component.cpp, effects.cpp, node.cpp, scene.cpp)
examples/                 screenshot tools (render demo scenes headless to PNG):
                          screenshot/ = G3 demo (flat SceneView), scene_screenshot/ = G9
                          demo (scene graph + per-node effects), landscape_screenshot/ =
                          G9 landscape (terrain + trees, cloud sky cubemap, sun, fog,
                          god rays)
tests/                    unit tests + gl smoke tests (own mini runner, no framework)
cmake/                    package config template
FuhGlGraphics.pro         qmake project (static library, alternative to CMake)
README.md / README.de.md  project readme (English / German)
SECURITY.md               vulnerability reporting policy
docs/                     README.md (language picker), USER_GUIDE.md, DEVELOPMENT.md,
                          REQUIREMENTS.md (English-only), en/tutorials/, de/tutorials/
                          (tutorials in English and German), Doxyfile,
                          images/ (screenshots), doxygen/ (gitignored)
.github/workflows/ci.yml
```

## Architecture & conventions

- **C++17.** English comments/Doxygen, compact code, descriptive variable names.
- **MVC pattern** where it applies (editor: model = scene/asset data, view = viewport/
  panels, controller = gizmo/camera/selection input).
- **PIMPL by default** for classes that wrap GL/Qt resources (public header declares
  `struct Impl; std::unique_ptr<Impl>`, the `.cpp` defines it).
- **No exceptions** for domain errors: fallible APIs return `bool` + info-log string.
- **Namespace `fuhglgraphics`** (`fuhglgraphics::render`, `fuhglgraphics::io`, …).
- **Qt only in `editor/`** — the library core builds Qt-free
  (`-DFUHGLGRAPHICS_WITH_QT=OFF`).
- GL loading, windowing and math come from FuhGLCore — do not re-implement them here.

## Build options

| Option | Values | Default |
|---|---|---|
| `FUHGLGRAPHICS_WITH_QT` | ON/OFF | ON |
| `FUHGLGRAPHICS_BUILD_TESTS` | ON/OFF | ON |
| `FUHGLGRAPHICS_BUILD_EXAMPLES` | ON/OFF | ON |
| `FUHGLGRAPHICS_ENABLE_HARDENING` | ON/OFF | ON |
| `FUHGLGRAPHICS_FUHGLCORE_DIR` | path | empty |
| `FUHGLGRAPHICS_FUHGLCORE_GIT_TAG` | git ref | `main` |

`FUHGLGRAPHICS_ENABLE_HARDENING=ON` (GNU/Clang only) adds compiler/linker hardening
flags, each applied only when the toolchain supports it (`check_cxx_compiler_flag` /
`check_linker_flag`): `-fstack-protector-strong` everywhere, and on Linux
`-fstack-clash-protection`, `-D_FORTIFY_SOURCE=2` in optimized configs
(Release/RelWithDebInfo/MinSizeRel), plus full RELRO (`-z relro -z now`) and
`-z noexecstack` at link time (PUBLIC, so consumers inherit them).

### FuhGLCore resolution (G1)

CMake resolves the direct FuhGLCore dependency in this order:

1. `-DFUHGLGRAPHICS_FUHGLCORE_DIR=<path>` → `add_subdirectory` of the local checkout
   (its `FUHGL_BUILD_EXAMPLES`/`FUHGL_BUILD_TESTS` are forced OFF),
2. `find_package(FuhGLCore QUIET)` → an installed FuhGLCore package,
3. FetchContent fallback: clone `https://github.com/FuhMhanShu/FuhGLCore.git` at
   `FUHGLGRAPHICS_FUHGLCORE_GIT_TAG` (default `main`).

The library links `FuhGLCore::fuhglcore` PUBLIC; the installed
`FuhGlGraphicsConfig.cmake` re-resolves FuhGLCore (and Qt6 when built WITH_QT) via
`find_dependency`, so `find_package(FuhGlGraphics)` → `FuhGlGraphics::fuhglgraphics`
works for consumers.

**qmake alternative** (same sources, Qt build system): `qmake6
"FUHGLCORE_DIR=/path/to/FuhGLCore" FuhGlGraphics.pro && make` builds the static library
`libfuhglgraphics.a`. `FuhGlGraphics.pro` includes `$$FUHGLCORE_DIR/fuhglcore.pri` (which
compiles the FuhGLCore sources into the target and `error()`s when `FUHGLCORE_DIR` is
unset — qmake var > environment variable). `FUHGLGRAPHICS_WITH_QT=1` (default) adds
`QT += widgets openglwidgets` + `automoc` + `FUHGLGRAPHICS_WITH_QT=1`; `0` builds the
Qt-free headless core. Source lists use per-directory globs (no `**` — that expands to
nothing on some Qt6 qmake versions).

## Tests

- `fuhglgraphics_tests` — headless unit tests (own mini runner, no framework), ctest
  label `unit`. Per GL feature there is also a `gl_smoke_*` binary with label
  `gl_context` (headless surfaceless EGL via `FUHGL_BACKEND=surfaceless`; skips visibly
  with exit code 77 — `SKIP_RETURN_CODE 77` — when no GL context is available).
  G3: `gl_smoke_render.cpp` (extended G-buffer, material IDs, MSAA path, render scale).
  G9: `gl_smoke_scene.cpp` (scene graph → FrameData → renderer: frame contents, instancing
  upload, material-ID readback, normal-map path, effect params changing the output).
  Unit tests for the pure parts live in `tests/test_render.cpp` / `tests/test_scene.cpp`.
- `examples/screenshot` — renders the G3 demo scene headless into a PNG;
  `examples/scene_screenshot` — the G9 demo scene (scene graph) with CLI effect switches;
  `examples/landscape_screenshot` — the G9 landscape (vegetation, cloud sky cubemap,
  sun disk, fog, god rays; `--no-rays/--no-fog/--no-clouds/--no-shadows/--no-ibl`)
  (all used for the reference images in `docs/images/`, locally and by the CI
  `docs-screenshots` job).

Run: `ctest --test-dir build --output-on-failure` (add `-L gl_context` where a GL
implementation is installed, e.g. `libegl1 libgl1 libgl1-mesa-dri` + Mesa llvmpipe,
with `-DFUHGL_LINUX_BACKEND=EGL` so the surfaceless backend is compiled in).

## CI (`.github/workflows/ci.yml`)

- `build-and-test` matrix: ubuntu-latest with `FUHGLGRAPHICS_WITH_QT=ON` (installs
  `qt6-base-dev`), windows-latest and macos-latest with `OFF`; configure → build →
  `ctest -L unit`. FuhGLCore is resolved via the FetchContent fallback.
- `gl-smoke` (ubuntu-latest): installs Mesa EGL (`libegl1 libgl1 libgl1-mesa-dri
  libegl-mesa0`), configures with `-DFUHGLGRAPHICS_WITH_QT=OFF -DFUHGL_LINUX_BACKEND=EGL`
  (the EGL backend provides the surfaceless headless mode) and runs `ctest -L unit` and
  `FUHGL_BACKEND=surfaceless ctest -L gl_context` on llvmpipe.
- `docs-screenshots` (ubuntu-latest): same Mesa setup, builds `fuhglgraphics_screenshot`
  + `fuhglgraphics_scene_screenshot` and renders the G3 reference images and the G9
  scene-graph effect comparisons into `docs/images/ci/`, uploaded as the artifact
  `g3-screenshots` (the reviewed copies are checked in under `docs/images/`).
- Workflow-level hardening: top-level `permissions: contents: read` (least privilege),
  `concurrency` with `cancel-in-progress`, and all third-party actions pinned to
  immutable commit SHAs (e.g. `actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683
  # v4.2.2`) so a repointed tag cannot change what CI executes.

## Adding a feature

1. Branch `vibe/<slug>-<id>` from `main`.
2. Public API in `include/fuhglgraphics/…` (Doxygen comments, English), PIMPL impl in
   `src/`, MVC in the editor where applicable.
3. Unit test (+ `gl_smoke` where a GL context helps) in `tests/`.
4. Tutorial in `docs/en/tutorials/` in English, then translate into
   `docs/de/tutorials/` (same file name; cross-links at the top of each file, see the
   `docs/README.md` language picker).
5. CI green, then PR to `main`.
6. Update `REQUIREMENTS.md` / `USER_GUIDE.md` / `README.md` / `README.de.md` /
   `CHANGELOG.md`.
