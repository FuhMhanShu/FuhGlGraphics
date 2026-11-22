#include "fuhglgraphics/scene/scene.hpp"

#include "fuhglgraphics/scene/component.hpp"
#include "fuhglgraphics/scene/node.hpp"

namespace fuhglgraphics::scene {

namespace {

/// @brief First CameraComponent with a camera in the subtree (depth-first).
const fuhgl::Camera* findCamera(const Node& node) {
    if (const CameraComponent* cc = node.find<CameraComponent>())
        if (cc->camera) return cc->camera;
    for (const auto& ch : node.children())
        if (const fuhgl::Camera* c = findCamera(*ch)) return c;
    return nullptr;
}

} // namespace

Scene::Scene() = default;

void Scene::updateWorldTransforms() {
    root_.updateWorld(fuhgl::math::Mat4{}); // identity root
}

void Scene::fillFrame(FrameData& frame, const fuhgl::Camera* camera, int width, int height, bool cull) {
    if (!camera) camera = findCamera(root_);
    updateWorldTransforms();
    frame = FrameData{}; // reset; tweak frame.view fields (env, fog, sun, ...) afterwards
    frame.view.camera = camera;
    frame.view.width = width;
    frame.view.height = height;
    frame.view.aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.f;

    fuhgl::math::Frustum frustum;
    const fuhgl::math::Frustum* frustumPtr = nullptr;
    if (cull && camera) {
        frustum = fuhgl::math::Frustum::fromViewProj(camera->viewProj(frame.view.aspect));
        frustumPtr = &frustum;
    }
    fillNode(root_, frame, frustumPtr);
}

void Scene::fillNode(const Node& node, FrameData& frame, const fuhgl::math::Frustum* frustum) const {
    if (frustum && !node.visibleIn(*frustum)) return; // subtree outside the frustum
    frame.effects.merge(node.effects);                // union; deeper/later nodes win the params
    for (const auto& c : node.components()) c->fill(node, frame);
    for (const auto& ch : node.children()) fillNode(*ch, frame, frustum);
}

} // namespace fuhglgraphics::scene
