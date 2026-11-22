#include "fuhglgraphics/scene/node.hpp"

#include <cmath>
#include <random>

namespace fuhglgraphics::scene {

// --- Instancing -----------------------------------------------------------------------

std::vector<fuhgl::math::Mat4> Instancing::makeLocalInstances() const {
    std::vector<fuhgl::math::Mat4> out;
    const int n = count > 0 ? count : 0;
    out.reserve(static_cast<std::size_t>(n));
    if (layout == InstanceLayout::Grid) {
        const int cols = n > 0 ? static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))) : 1;
        const int rows = n > 0 ? (n + cols - 1) / cols : 1;
        for (int i = 0; i < n; ++i) {
            const int ix = i % cols;
            const int iz = i / cols;
            const float x = cols > 1 ? -extent.x * 0.5f + static_cast<float>(ix) * extent.x / static_cast<float>(cols - 1)
                                     : 0.f;
            const float z = rows > 1 ? -extent.z * 0.5f + static_cast<float>(iz) * extent.z / static_cast<float>(rows - 1)
                                     : 0.f;
            out.push_back(fuhgl::math::translate({x, 0.f, z}));
        }
    } else {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dx(-extent.x * 0.5f, extent.x * 0.5f);
        std::uniform_real_distribution<float> dy(-extent.y * 0.5f, extent.y * 0.5f);
        std::uniform_real_distribution<float> dz(-extent.z * 0.5f, extent.z * 0.5f);
        for (int i = 0; i < n; ++i)
            out.push_back(fuhgl::math::translate({dx(rng), dy(rng), dz(rng)}));
    }
    return out;
}

// --- Node ------------------------------------------------------------------------------

Node::Node(std::string name) : name_(std::move(name)) {}

fuhgl::math::Mat4 Node::localMatrix() const {
    return fuhgl::math::translate(translation) * fuhgl::math::toMat4(fuhgl::math::normalize(rotation)) *
           fuhgl::math::scale(scale);
}

Node& Node::addChild(std::unique_ptr<Node> child) {
    child->setParent(this);
    Node& ref = *child;
    children_.push_back(std::move(child));
    return ref;
}

Node& Node::createChild(std::string name) {
    return addChild(std::make_unique<Node>(std::move(name)));
}

bool Node::detach(const Component* c) {
    for (auto it = components_.begin(); it != components_.end(); ++it) {
        if (it->get() == c) {
            const_cast<Component*>(c)->setNode(nullptr);
            components_.erase(it);
            return true;
        }
    }
    return false;
}

void Node::updateWorld(const fuhgl::math::Mat4& parentWorld) {
    worldMatrix_ = parentWorld * localMatrix();
    for (auto& ch : children_) ch->updateWorld(worldMatrix_);
    computeAabb();
}

void Node::computeAabb() {
    hasAabb_ = false;
    aabbMin_ = {0.f, 0.f, 0.f};
    aabbMax_ = {0.f, 0.f, 0.f};
    auto unionPoint = [&](const fuhgl::math::Vec3& p) {
        if (!hasAabb_) {
            aabbMin_ = aabbMax_ = p;
            hasAabb_ = true;
        } else {
            aabbMin_ = {std::min(aabbMin_.x, p.x), std::min(aabbMin_.y, p.y), std::min(aabbMin_.z, p.z)};
            aabbMax_ = {std::max(aabbMax_.x, p.x), std::max(aabbMax_.y, p.y), std::max(aabbMax_.z, p.z)};
        }
    };
    for (const auto& comp : components_) {
        const MeshComponent* mc = dynamic_cast<const MeshComponent*>(comp.get());
        if (!mc) continue;
        for (const MeshEntry& e : mc->meshes) {
            const fuhgl::math::Mat4 m = worldMatrix_ * e.localOffset;
            if (e.hasAabb) {
                // transform the 8 corners of the local AABB
                for (int i = 0; i < 8; ++i) {
                    const fuhgl::math::Vec3 c{(i & 1) ? e.aabbMax.x : e.aabbMin.x,
                                              (i & 2) ? e.aabbMax.y : e.aabbMin.y,
                                              (i & 4) ? e.aabbMax.z : e.aabbMin.z};
                    const fuhgl::math::Vec4 w = m * fuhgl::math::Vec4{c.x, c.y, c.z, 1.f};
                    unionPoint({w.x, w.y, w.z});
                }
            } else if (e.mesh) {
                const fuhgl::math::Vec4 s = fuhgl::math::transformSphere(m, e.mesh->boundingSphere());
                unionPoint({s.x - s.w, s.y - s.w, s.z - s.w});
                unionPoint({s.x + s.w, s.y + s.w, s.z + s.w});
            }
        }
    }
    // G13: a water surface contributes its grid AABB padded by the max wave height
    for (const auto& comp : components_) {
        const WaterComponent* wc = dynamic_cast<const WaterComponent*>(comp.get());
        if (!wc) continue;
        const float margin = wc->surface.maxWaveHeight();
        const fuhgl::math::Vec4 s =
            fuhgl::math::transformSphere(worldMatrix_, wc->surface.mesh().boundingSphere());
        unionPoint({s.x - s.w, s.y - s.w - margin, s.z - s.w});
        unionPoint({s.x + s.w, s.y + s.w + margin, s.z + s.w});
    }
    for (const auto& ch : children_) {
        if (ch->hasAabb()) {
            unionPoint(ch->aabbMin_);
            unionPoint(ch->aabbMax_);
        }
    }
}

bool Node::visibleIn(const fuhgl::math::Frustum& frustum) const {
    if (!hasAabb_) return true;
    return frustum.intersectsAabb(aabbMin_, aabbMax_);
}

} // namespace fuhglgraphics::scene
