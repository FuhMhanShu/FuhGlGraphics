# Tutorial G4 — glTF 2.0 loader (`fuhglgraphics::io`)

> 🇩🇪 **Deutsche Version:** [docs/de/tutorials/g4_gltf_loader.md](../../de/tutorials/g4_gltf_loader.md)

**Goal:** load a glTF 2.0 model (.gltf JSON or .glb binary) into the framework and render
it with the G3 deferred renderer through the G9 scene graph — textures included.

## What is implemented

- `io::loadGltf(path, asset, &err)` — pure CPU, no GL context needed. Own minimal JSON
  parser + base64 (`io/json.hpp`, no nlohmann). Supports: buffers (external `.bin`,
  embedded base64 `data:` URIs, GLB BIN chunk), bufferViews + accessors (all component
  types 5120–5126, normalized, strided, sparse), mesh primitives (mode TRIANGLES /
  TRIANGLE_STRIP / TRIANGLE_FAN; attributes POSITION, NORMAL, TEXCOORD_0, COLOR_0 + indices;
  → FuhGLCore `render::MeshData`, 11 floats/vertex), materials via
  `KHR_materials_pbrMetallicRoughness` (baseColor/emissive/metallic/roughness factors,
  baseColor + normal textures, alphaMode, doubleSided), images (external files or
  GLB-embedded; PNG/JPEG decoded to RGBA8 via Qt `QImage` when built `WITH_QT=ON`,
  otherwise skipped with a log and the material falls back to its color factors),
  samplers, nodes (TRS; `matrix` is decomposed to TRS), scenes.
  Skipped with a log: skins, animations, morph targets, Draco/meshopt, points/lines.
- `io::instantiateGltf(asset, scene, gpu, parent)` — needs a current GL context. Maps the
  default scene onto the G9 scene graph: one `scene::Node` per glTF node (TRS preserved,
  children preserved), each mesh node gets a `MeshComponent` with one `MeshEntry` per
  primitive (GL `render::Mesh`, preset `render::Material` with albedo/normal textures,
  `castShadow = true`, running material ID).
- Errors follow the repo convention: `bool` + error string, never exceptions.
- No third-party assets are vendored in the repo (requirement N7) — tests use generated
  fixtures (`tests/gltf_fixture.hpp`), the demo asset below is fetched at tutorial time.

## Demo asset (CC0)

The tutorial/demo uses a Kenney model (Creative Commons Zero, CC0) from the
[`shorepine/kenney`](https://github.com/shorepine/kenney) mirror, pinned to commit
`3694c6879e487c108f55677be7dd2ca75b07cc3b` (see `LICENSE.txt` there: "Creative Commons
Zero, CC0"):

```bash
git clone --filter=blob:none --no-checkout https://github.com/shorepine/kenney.git /tmp/kenney
git -C /tmp/kenney sparse-checkout set 3d/pirate
git -C /tmp/kenney checkout 3694c6879e487c108f55677be7dd2ca75b07cc3b
# /tmp/kenney/3d/pirate/ship-pirate-medium.glb + chest.glb + Textures/colormap.png
# (external image URIs resolve relative to the .glb, so keep Textures/ next to it)
```

## Code

```cpp
#include <fuhglgraphics/graphics.hpp>

// 1) parse (no GL needed)
fuhglgraphics::io::GltfAsset asset;
std::string err;
if (!fuhglgraphics::io::loadGltf("ship-pirate-medium.glb", asset, &err)) {
    std::fprintf(stderr, "loadGltf failed: %s\n", err.c_str());
    return 1;
}

// 2) instantiate into the G9 scene graph (needs a current GL >= 3.3 context)
fuhglgraphics::scene::Scene scene;
fuhglgraphics::io::GltfGpuResources gpu;
fuhglgraphics::scene::Node& model = scene.root().createChild("model");
fuhglgraphics::io::instantiateGltf(asset, scene, gpu, &model, &err);
// model.translation / model.scale — normalize the model to taste (see examples/gltf_demo)

// 3) render like any G9 scene
fuhglgraphics::scene::FrameData frame;
scene.fillFrame(frame, nullptr, 1280, 720);
frame.effects.applyTo(renderer);                        // G3 DeferredRenderer
renderer.render(frame.view, &frame.materialIds, &frame.castShadow);
```

## Ready-made demo tool

`examples/gltf_demo` does all of the above plus staging (ground plaza, sun + fill light,
gradient sky cubemap + IBL environment, CSM shadows, god rays) and writes a PNG headless:

```bash
cmake --build build --target fuhglgraphics_gltf_demo   # WITH_QT=ON build -> textures
FUHGL_BACKEND=surfaceless ./build/examples/fuhglgraphics_gltf_demo \
    --model /tmp/kenney/3d/pirate/ship-pirate-medium.glb \
    --out ship.png --width 1280 --height 720 --frames 24
# flags: --no-shadows --no-ibl --no-rays --no-sky --no-tex, --azimuth/--elevation/--sun-*
```

## Result (docs/images, rendered headless on Mesa llvmpipe)

- [g4_gltf_ship.png](../../images/g4_gltf_ship.png) — ship, all effects (CSM shadows + IBL +
  TAA + tonemap)
- [g4_gltf_ship_no_shadows.png](../../images/g4_gltf_ship_no_shadows.png) — shadows off
- [g4_gltf_ship_no_ibl.png](../../images/g4_gltf_ship_no_ibl.png) — IBL off
- [g4_gltf_chest.png](../../images/g4_gltf_chest.png) — second model (chest.glb), close-up

## Tests

- `tests/test_io.cpp` (label `unit`): JSON parser, base64, `loadGltf` on generated
  .gltf/.glb fixtures (data-URI + BIN chunk), error cases, matrix-node decomposition;
  with `WITH_QT=ON` also a PNG decode roundtrip via `QImage`.
- `tests/gl_smoke_gltf.cpp` (label `gl_context`, skips with 77 without GL): loads the
  fixture .glb, instantiates, renders through the G3 renderer and checks lit pixels,
  per-item material IDs, the node world transform on draw items and material presets.
