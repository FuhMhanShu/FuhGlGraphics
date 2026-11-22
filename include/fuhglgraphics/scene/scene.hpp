#pragma once

/// @file scene.hpp
/// @brief G9 scene graph: Scene owns the root Node, updates world transforms + world AABBs
/// and fills a FrameData (fuhgl::render::SceneView + parallel materialIds / castShadow
/// vectors + merged Effects) that is rendered by the G3 DeferredRenderer:
///
///   fuhglgraphics::scene::Scene scene;
///   ... build nodes, attach components ...
///   fuhglgraphics::scene::FrameData frame;
///   scene.fillFrame(frame, &camera, 1280, 720);
///   frame.effects.applyTo(renderer);
///   renderer.render(frame.view, &frame.materialIds, &frame.castShadow);

#include "effects.hpp"
#include "node.hpp"

#include <fuhgl/math/frustum.hpp>
#include <fuhgl/render/scene.hpp>

#include <cstdint>
#include <vector>

namespace fuhglgraphics::particles {
class ParticleSystem;
}

namespace fuhglgraphics::probes {
class LightProbeGrid;
}

namespace fuhglgraphics::scene {

/// @brief Everything one frame needs: the FuhGLCore SceneView plus the parallel per-item
/// vectors for the G3 renderer, the merged Effects of the whole graph, the G5 particle
/// systems collected from ParticleEmitterComponents (rendered when Effect::Particles is
/// on) and the G7 light-probe grid of the first LightProbeComponent (rendered when
/// Effect::LightProbes is on).
struct FrameData {
    fuhgl::render::SceneView view;          ///< items + lights + camera + size
    std::vector<std::uint32_t> materialIds; ///< parallel to view.items (G-buffer MaterialId)
    std::vector<bool> castShadow;           ///< parallel to view.items (shadow depth pass)
    Effects effects;                        ///< merged union of all node effects
    std::vector<const particles::ParticleSystem*> particleSystems; ///< G5 emitters
    const probes::LightProbeGrid* lightProbes = nullptr;           ///< G7 probe grid
};

/// @brief The G9 scene graph (root node + frame filling with frustum culling).
class Scene {
public:
    Scene();

    Node& root() { return root_; }
    const Node& root() const { return root_; }

    /// @brief Recomputes world matrices and world AABBs of the whole graph.
    void updateWorldTransforms();

    /// @brief Fills `frame` from the graph: sets view.camera/width/height/aspect, traverses
    /// the nodes (frustum culling when `cull` and a camera is available), appends draw
    /// items + lights, fills materialIds/castShadow and merges all node Effects into
    /// frame.effects.
    /// @param camera explicit camera; when nullptr, the first CameraComponent in the graph
    /// is used (frame.view.camera stays nullptr when there is none).
    void fillFrame(FrameData& frame, const fuhgl::Camera* camera, int width, int height,
                   bool cull = true);

private:
    void fillNode(const Node& node, FrameData& frame, const fuhgl::math::Frustum* frustum) const;

    Node root_{"root"};
};

} // namespace fuhglgraphics::scene
