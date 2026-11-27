#include "gizmo.hpp"

#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/node.hpp>
#include <fuhglgraphics/scene/picking.hpp>

#include <fuhgl/render/solid.hpp>

#include <algorithm>
#include <cmath>

namespace assetcreator {

using namespace fuhglgraphics;

namespace {

fuhgl::math::Vec3 axisVec(char axis) {
    if (axis == 'x') return {1.f, 0.f, 0.f};
    if (axis == 'y') return {0.f, 1.f, 0.f};
    return {0.f, 0.f, 1.f};
}

/// @brief Any unit vector perpendicular to `axis` (for the rotate-plane basis).
fuhgl::math::Vec3 perpendicular(const fuhgl::math::Vec3& axis) {
    const fuhgl::math::Vec3 a = std::abs(axis.x) < 0.9f ? fuhgl::math::Vec3{1.f, 0.f, 0.f}
                                                        : fuhgl::math::Vec3{0.f, 1.f, 0.f};
    return fuhgl::math::normalize(fuhgl::math::cross(axis, a));
}

/// @brief Ray/plane intersection (plane through `point` with `normal`); false when parallel
/// or behind the ray origin.
bool rayPlane(const scene::Ray& r, const fuhgl::math::Vec3& point, const fuhgl::math::Vec3& normal,
              fuhgl::math::Vec3& out) {
    const float denom = fuhgl::math::dot(r.dir, normal);
    if (std::abs(denom) < 1e-6f) return false;
    const float t = fuhgl::math::dot(point - r.origin, normal) / denom;
    if (t < 0.f) return false;
    out = r.origin + r.dir * t;
    return true;
}

/// @brief Axis parameter s of the point on line (center + s*axis) closest to the ray
/// (exact line/ray closest point, guarded against a parallel ray).
float axisParamOfRay(const scene::Ray& r, const fuhgl::math::Vec3& center,
                     const fuhgl::math::Vec3& axis) {
    const float a = fuhgl::math::dot(axis, r.dir);
    const float b = fuhgl::math::dot(axis, r.origin - center);
    const float c = fuhgl::math::dot(r.dir, r.origin - center);
    if (std::abs(1.f - a * a) < 1e-4f) return b;
    const float t = -(b * a + c) / (1.f - a * a);
    return t * a - b;
}

} // namespace

bool GizmoOverlay::create() {
    if (created_) return true;
    // unit-size gizmo geometry from FuhGLCore solids (the gizmo root scales it on screen)
    auto arrow = fuhgl::render::translated(fuhgl::render::makeCylinder(0.035f, 0.8f), {0.f, 0.4f, 0.f}) |
                 fuhgl::render::translated(fuhgl::render::makeCone(0.09f, 0.25f), {0.f, 0.925f, 0.f});
    auto ring = fuhgl::render::makeTorus(0.85f, 0.05f);
    auto cube = fuhgl::render::makeCube({0.09f, 0.09f, 0.09f});

    auto addMesh = [this](const fuhgl::render::SolidPtr& s, int res) {
        meshData_.push_back(std::make_unique<fuhgl::render::MeshData>(s->buildMeshData(res, {1.f, 1.f, 1.f})));
        auto m = std::make_unique<fuhgl::render::Mesh>();
        m->setMeshData(*meshData_.back()); // needs a current GL context
        fuhgl::render::Mesh* ptr = m.get();
        meshes_.push_back(std::move(m));
        return ptr;
    };
    fuhgl::render::Mesh* arrowMesh = addMesh(arrow, 10);
    fuhgl::render::Mesh* ringMesh = addMesh(ring, 20);
    fuhgl::render::Mesh* cubeMesh = addMesh(cube, 6);
    const fuhgl::render::MeshData* arrowData = meshData_[0].get();
    const fuhgl::render::MeshData* ringData = meshData_[1].get();
    const fuhgl::render::MeshData* cubeData = meshData_[2].get();

    const fuhgl::math::Vec3 red{1.f, 0.25f, 0.25f}, green{0.35f, 0.9f, 0.35f},
        blue{0.3f, 0.5f, 1.f}, yellow{1.f, 0.85f, 0.25f};
    // arrow/ring meshes are modeled around +Y; rotate them onto the part axis
    const fuhgl::math::Quat rotX = fuhgl::math::fromAxisAngle(-fuhgl::math::kPi * 0.5f, {0.f, 0.f, 1.f});
    const fuhgl::math::Quat rotZ = fuhgl::math::fromAxisAngle(fuhgl::math::kPi * 0.5f, {1.f, 0.f, 0.f});

    parts_ = {
        {"x", arrowMesh, arrowData, rotX, red, 'x', 't'},
        {"y", arrowMesh, arrowData, fuhgl::math::Quat{}, green, 'y', 't'},
        {"z", arrowMesh, arrowData, rotZ, blue, 'z', 't'},
        {"c", cubeMesh, cubeData, fuhgl::math::Quat{}, yellow, 'c', 'a'},
        {"rx", ringMesh, ringData, rotX, red, 'x', 'r'},
        {"ry", ringMesh, ringData, fuhgl::math::Quat{}, green, 'y', 'r'},
        {"rz", ringMesh, ringData, rotZ, blue, 'z', 'r'},
        {"sx", arrowMesh, arrowData, rotX, red, 'x', 's'},
        {"sy", arrowMesh, arrowData, fuhgl::math::Quat{}, green, 'y', 's'},
        {"sz", arrowMesh, arrowData, rotZ, blue, 'z', 's'},
    };
    for (const Part& p : parts_) {
        scene::Node& n = scene_.root().createChild(p.name);
        n.rotation = p.rot;
        scene::MeshEntry& e = n.attach<scene::MeshComponent>().addMesh(p.mesh);
        e.meshData = p.meshData;   // G18 picking on the gizmo scene
        e.castShadow = false;
        e.materialId = 0;
        e.albedo = {0.f, 0.f, 0.f};
        e.emissive = p.color;      // flat unlit gizmo colors
        e.roughness = 1.f;
    }
    created_ = true;
    return true;
}

void GizmoOverlay::release() {
    scene_.root().clearChildren();
    parts_.clear();
    meshes_.clear();    // GL meshes — needs a current GL context
    meshData_.clear();
    target_ = nullptr;
    dragging_ = false;
    created_ = false;
}

const GizmoOverlay::Part* GizmoOverlay::findPart(const std::string& name) const {
    for (const Part& p : parts_)
        if (p.name == name) return &p;
    return nullptr;
}

scene::Node* GizmoOverlay::partNode(const std::string& name) const {
    for (const auto& ch : scene_.root().children())
        if (ch->name() == name) return ch.get();
    return nullptr;
}

void GizmoOverlay::updateForTarget(const fuhgl::Camera& cam) {
    if (!created_ || !target_) return;
    const fuhgl::math::Mat4& wm = target_->worldMatrix(); // valid after the doc scene's update
    const fuhgl::math::Vec3 pos{wm(3, 0), wm(3, 1), wm(3, 2)};
    const float k = std::max(0.05f, fuhgl::math::length(cam.eye() - pos) * 0.14f); // screen-constant size
    scene::Node& root = scene_.root();
    root.translation = pos;
    root.rotation = target_->rotation;
    root.scale = {k, k, k};
    for (const Part& p : parts_) {
        scene::Node* n = partNode(p.name);
        if (!n) continue;
        const bool show = p.kind == 'a' ||
                          (mode_ == Mode::Translate && p.kind == 't') ||
                          (mode_ == Mode::Rotate && p.kind == 'r') ||
                          (mode_ == Mode::Scale && p.kind == 's');
        n->scale = show ? fuhgl::math::Vec3{1.f, 1.f, 1.f} : fuhgl::math::Vec3{0.f, 0.f, 0.f};
    }
    scene_.updateWorldTransforms();
}

void GizmoOverlay::appendToFrame(scene::FrameData& frame, const fuhgl::Camera& cam, int w, int h) {
    if (!created_ || !target_) return;
    scene::FrameData g;
    scene_.fillFrame(g, &cam, w, h, false);
    frame.view.items.insert(frame.view.items.end(), g.view.items.begin(), g.view.items.end());
    frame.materialIds.insert(frame.materialIds.end(), g.materialIds.begin(), g.materialIds.end());
    frame.castShadow.insert(frame.castShadow.end(), g.castShadow.begin(), g.castShadow.end());
    frame.skins.insert(frame.skins.end(), g.skins.begin(), g.skins.end());
    frame.subsurface.insert(frame.subsurface.end(), g.subsurface.begin(), g.subsurface.end());
}

std::string GizmoOverlay::pickPart(const fuhgl::Camera& cam, int w, int h, float px, float py) {
    if (!created_ || !target_) return {};
    updateForTarget(cam);
    scene::PickHit hit;
    if (scene::pick(scene_, &cam, w, h, px, py, hit) && hit.hit && hit.node) return hit.node->name();
    return {};
}

void GizmoOverlay::beginDrag(const std::string& part, const fuhgl::Camera& cam, int w, int h,
                             float px, float py) {
    const Part* p = findPart(part);
    if (!p || !target_) return;
    dragging_ = true;
    activePart_ = part;
    dragStartT = target_->translation;
    dragStartR = target_->rotation;
    dragStartS = target_->scale;
    const fuhgl::math::Mat4& wm = target_->worldMatrix();
    center_ = {wm(3, 0), wm(3, 1), wm(3, 2)};
    axisWorld_ = p->axis == 'c' ? fuhgl::math::Vec3{0.f, 0.f, 0.f}
                                : fuhgl::math::normalize(target_->rotation * axisVec(p->axis));
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    const scene::Ray r0 = scene::rayFromScreen(cam, aspect, w, h, px, py);
    if (mode_ == Mode::Rotate && p->axis != 'c') {
        fuhgl::math::Vec3 ip;
        if (rayPlane(r0, center_, axisWorld_, ip)) {
            startPoint_ = ip;
            const fuhgl::math::Vec3 u = perpendicular(axisWorld_);
            const fuhgl::math::Vec3 v = fuhgl::math::cross(axisWorld_, u);
            startAngle_ = std::atan2(fuhgl::math::dot(ip - center_, v), fuhgl::math::dot(ip - center_, u));
        }
    } else if (p->axis != 'c') {
        startAlong_ = axisParamOfRay(r0, center_, axisWorld_);
    } else {
        const fuhgl::math::Vec3 n = fuhgl::math::normalize(cam.eye() - center_);
        fuhgl::math::Vec3 ip;
        startPoint_ = rayPlane(r0, center_, n, ip) ? ip : center_;
    }
}

void GizmoOverlay::dragTo(const fuhgl::Camera& cam, int w, int h, float px, float py) {
    if (!dragging_ || !target_) return;
    const Part* p = findPart(activePart_);
    if (!p) return;
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    const scene::Ray r = scene::rayFromScreen(cam, aspect, w, h, px, py);
    if (mode_ == Mode::Rotate && p->axis != 'c') {
        fuhgl::math::Vec3 ip;
        if (!rayPlane(r, center_, axisWorld_, ip)) return;
        const fuhgl::math::Vec3 u = perpendicular(axisWorld_);
        const fuhgl::math::Vec3 v = fuhgl::math::cross(axisWorld_, u);
        const float a = std::atan2(fuhgl::math::dot(ip - center_, v), fuhgl::math::dot(ip - center_, u));
        target_->rotation =
            fuhgl::math::normalize(fuhgl::math::fromAxisAngle(a - startAngle_, axisWorld_) * dragStartR);
    } else if (p->axis != 'c' && mode_ == Mode::Translate) {
        const float s = axisParamOfRay(r, center_, axisWorld_);
        target_->translation = dragStartT + axisWorld_ * (s - startAlong_);
    } else if (p->axis != 'c' && mode_ == Mode::Scale) {
        const float s = axisParamOfRay(r, center_, axisWorld_);
        float f = std::abs(startAlong_) > 1e-4f ? s / startAlong_ : 1.f + s;
        f = std::max(0.05f, f);
        target_->scale = dragStartS;
        target_->scale[p->axis == 'x' ? 0 : (p->axis == 'y' ? 1 : 2)] *= f;
    } else if (p->axis == 'c') {
        const fuhgl::math::Vec3 n = fuhgl::math::normalize(cam.eye() - center_);
        fuhgl::math::Vec3 ip;
        if (!rayPlane(r, center_, n, ip)) return;
        if (mode_ == Mode::Translate) {
            target_->translation = dragStartT + (ip - startPoint_);
        } else { // uniform scale from the radial distance in the camera plane
            const float d0 = fuhgl::math::length(startPoint_ - center_);
            const float f = d0 > 1e-4f ? std::max(0.05f, fuhgl::math::length(ip - center_) / d0) : 1.f;
            target_->scale = dragStartS * f;
        }
    }
}

} // namespace assetcreator
