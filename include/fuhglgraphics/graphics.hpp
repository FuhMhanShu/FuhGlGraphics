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
//   editor/   G2+G11  minimal Qt6 editor (only built with FUHGLGRAPHICS_WITH_QT=1)
// Additionally planned: G10 quick upgrades, G11 engine infrastructure.
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

#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/effects.hpp>
#include <fuhglgraphics/scene/node.hpp>
#include <fuhglgraphics/scene/scene.hpp>
#include <fuhglgraphics/scene/picking.hpp>
#include <fuhglgraphics/scene/serialize.hpp>

#include <fuhglgraphics/io/json.hpp>
#include <fuhglgraphics/io/gltf.hpp>

#include <fuhglgraphics/particles/particle_system.hpp>

#include <fuhglgraphics/anim/animation.hpp>
#include <fuhglgraphics/anim/gpu_skinner.hpp>

#include <fuhglgraphics/probes/light_probes.hpp>

#include <fuhglgraphics/water/water_surface.hpp>
