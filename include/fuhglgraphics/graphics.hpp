#pragma once

// FuhGlGraphics umbrella header.
//
// Module map (see docs/REQUIREMENTS.md §3):
//   render/   G3   deferred renderer on top of FuhGLCore (implemented; shadows on the
//                   FuhGLCore F45 shadow kit)
//   io/       G4   glTF 2.0 asset loading (implemented: json.hpp, gltf.hpp)
//   scene/    G9   scene graph + components + per-node effects/instancing (implemented)
//   particles/ G5  GPU particle system, compute on GL >= 4.3 + CPU fallback (implemented)
//   anim/     G6   keyframe AnimationPlayer + skeletal skinning (CPU palette) (implemented)
//   probes/   G7   SH L2 light-probe grid, CPU bake + 3D-texture sampling (implemented)
//   water/    G13  realistic water: Gerstner waves + planar reflection + foam (implemented)
//   procedural/ G24-G31  procedural generation: seeded CPU noise (G24), marching cubes (G25),
//                   L-systems (G26), wave function collapse (G27), voronoi (G28),
//                   SDF/3D fractals (G29), procedural creatures (G30), world generator
//                   helper generateWorld (G31: landscape or city from one params struct)
//   mcp/      G12  MCP layer (Qt-free): JSON-RPC 2.0 helpers (jsonrpc.hpp), the tool
//                   catalog with procedural (G24-G31) + deferred-render + G13 water tools
//                   (tool_catalog.hpp) and the stdio MCP server (mcp_server.hpp)
//   io/asset_cache.hpp  G11 on-disk asset cache keyed by content hash
//   weather/  G38  weather system: CPU state machine with time-based transitions (sun path,
//                   clouds, rain, thunderstorm, lightning schedule) + rain-emitter/bolt
//                   helpers + renderer param mapping; render side: Effect::Sky/Lightning in
//                   the G3 renderer, CPU sky twins in render/sky.hpp (implemented, A2-A9)
//   editor/   G2+G11  Qt6 editor code (only built with FUHGLGRAPHICS_WITH_QT=1):
//                   gl_viewport_widget.hpp = GL viewport widget (fps limiter, frame stats)
//   tools/    G12  fuhglgraphics_mcp_server (Qt-free stdio MCP server) +
//                   fuhglgraphics_asset_creator (Qt6, WITH_QT + BUILD_TOOLS only)
//
// The library core stays Qt-free; build with -DFUHGLGRAPHICS_WITH_QT=OFF for
// headless use.

#include <fuhglgraphics/version.hpp>

#include <fuhglgraphics/render/effects.hpp>
#include <fuhglgraphics/render/gbuffer.hpp>
#include <fuhglgraphics/render/decals.hpp>
#include <fuhglgraphics/render/debug_hud.hpp>
#include <fuhglgraphics/render/deferred_renderer.hpp>
#include <fuhglgraphics/render/ssao.hpp>
#include <fuhglgraphics/render/ssr.hpp>
#include <fuhglgraphics/render/bloom.hpp>
#include <fuhglgraphics/render/volumetric.hpp>
#include <fuhglgraphics/render/wetness.hpp>
#include <fuhglgraphics/render/tonemap.hpp>
#include <fuhglgraphics/render/parallax.hpp>
#include <fuhglgraphics/render/hbao.hpp>
#include <fuhglgraphics/render/sky.hpp> // G38 procedural-sky CPU twins (includes weather/weather.hpp)

#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/effects.hpp>
#include <fuhglgraphics/scene/node.hpp>
#include <fuhglgraphics/scene/scene.hpp>
#include <fuhglgraphics/scene/picking.hpp>
#include <fuhglgraphics/scene/serialize.hpp>

#include <fuhglgraphics/io/json.hpp>
#include <fuhglgraphics/io/gltf.hpp>
#include <fuhglgraphics/io/asset_cache.hpp>

#include <fuhglgraphics/mcp/jsonrpc.hpp>
#include <fuhglgraphics/mcp/tool_catalog.hpp>
#include <fuhglgraphics/mcp/mcp_server.hpp>

#include <fuhglgraphics/particles/particle_system.hpp>

#include <fuhglgraphics/anim/animation.hpp>
#include <fuhglgraphics/anim/gpu_skinner.hpp>

#include <fuhglgraphics/probes/light_probes.hpp>

#include <fuhglgraphics/water/water_surface.hpp>

#include <fuhglgraphics/procedural/noise.hpp>
#include <fuhglgraphics/procedural/marching_cubes.hpp>
#include <fuhglgraphics/procedural/lsystem.hpp>
#include <fuhglgraphics/procedural/wfc.hpp>
#include <fuhglgraphics/procedural/voronoi.hpp>
#include <fuhglgraphics/procedural/sdf.hpp>
#include <fuhglgraphics/procedural/creature.hpp>
#include <fuhglgraphics/procedural/worldgen.hpp>
#include <fuhglgraphics/procedural/house.hpp>
#include <fuhglgraphics/procedural/citygen.hpp>

#include <fuhglgraphics/weather/weather.hpp>

// NOTE: editor/gl_viewport_widget.hpp (WITH_QT only) is deliberately NOT included here:
// its Qt/GL headers would leak GL macros (e.g. GL_UNSIGNED_INT) into every core TU and
// clash with FuhGLCore's fuhgl::gl constants. Editor code includes it directly:
//   #include <fuhglgraphics/editor/gl_viewport_widget.hpp>
