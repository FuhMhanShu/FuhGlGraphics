#pragma once

/// @file node.hpp
/// @brief G9 scene-graph node: local TRS transform + world matrix, children, a polymorphic
/// component list (Component), a per-node Effects list (scene/effects.hpp) and an
/// instancing property. World AABBs are maintained for frustum culling (F34).

#include "component.hpp"
#include "effects.hpp"

#include <fuhgl/math.hpp>

#include <memory>
#include <string>
#include <vector>

namespace fuhglgraphics::scene {

/// @brief How instance transforms of an instanced node are laid out.
enum class InstanceLayout {
    Grid,   ///< regular grid in the local XZ plane over `extent`
    Random, ///< seeded random positions inside the local box `extent`
};

/// @brief Instancing property of a node (G9): when enabled, the node's MeshComponent is
/// drawn as `count` instances in one instanced draw call (F24 via Mesh::setInstanceData).
struct Instancing {
    bool enabled = false;
    int count = 0;                              ///< number of instances
    InstanceLayout layout = InstanceLayout::Grid;
    fuhgl::math::Vec3 extent{10.f, 10.f, 10.f}; ///< grid area / random box, centered at the node origin
    unsigned seed = 1;                          ///< random layout seed (deterministic)

    /// @brief The `count` local-space instance offset matrices (deterministic for Grid
    /// and for Random with a fixed seed). Pure — unit-testable without GL.
    std::vector<fuhgl::math::Mat4> makeLocalInstances() const;
};

/// @brief One node of the G9 scene graph.
class Node {
public:
    explicit Node(std::string name = {});
    ~Node() = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    // --- local transform (TRS) -------------------------------------------------------
    fuhgl::math::Vec3 translation{0.f, 0.f, 0.f};
    fuhgl::math::Quat rotation{};   ///< identity by default
    fuhgl::math::Vec3 scale{1.f, 1.f, 1.f};

    /// @brief Local model matrix = translate * rotate * scale.
    fuhgl::math::Mat4 localMatrix() const;
    /// @brief World matrix (valid after Scene::updateWorldTransforms() / fillFrame()).
    const fuhgl::math::Mat4& worldMatrix() const { return worldMatrix_; }

    // --- hierarchy -------------------------------------------------------------------
    Node* parent() const { return parent_; }
    /// @brief Adds a child node (takes ownership) and returns a reference to it.
    Node& addChild(std::unique_ptr<Node> child);
    /// @brief Creates and adds a named child.
    Node& createChild(std::string name = {});
    const std::vector<std::unique_ptr<Node>>& children() const { return children_; }
    std::size_t childCount() const { return children_.size(); }

    // --- components (polymorphic list) ------------------------------------------------
    /// @brief Attaches a component of type T (constructed with args...) and returns it.
    template <typename T, typename... Args>
    T& attach(Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>);
        auto c = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *c;
        ref.setNode(this);
        components_.push_back(std::move(c));
        return ref;
    }
    /// @brief Detaches (and destroys) the given component. Returns true when found.
    bool detach(const Component* c);
    /// @brief First component of type T (nullptr when none).
    template <typename T>
    T* find() const {
        for (const auto& c : components_)
            if (T* t = dynamic_cast<T*>(c.get())) return t;
        return nullptr;
    }
    /// @brief All components of type T.
    template <typename T>
    std::vector<T*> findAll() const {
        std::vector<T*> out;
        for (const auto& c : components_)
            if (T* t = dynamic_cast<T*>(c.get())) out.push_back(t);
        return out;
    }
    const std::vector<std::unique_ptr<Component>>& components() const { return components_; }

    // --- effects + instancing (per-node properties) -----------------------------------
    Effects effects;      ///< effect list + parameters; merged over all nodes per frame
    Instancing instancing; ///< instancing property (used by MeshComponent)

    // --- culling (world AABB, F34) ------------------------------------------------------
    /// @brief World-space AABB of this node's subtree (mesh components' bounding volumes
    /// unioned with the children's). hasAabb() is false when the subtree has no geometry.
    void worldAabb(fuhgl::math::Vec3& mn, fuhgl::math::Vec3& mx) const {
        mn = aabbMin_;
        mx = aabbMax_;
    }
    bool hasAabb() const { return hasAabb_; }
    /// @brief Conservative visibility test against a frustum (true when no AABB).
    bool visibleIn(const fuhgl::math::Frustum& frustum) const;

private:
    friend class Scene;
    void setParent(Node* p) { parent_ = p; }
    void updateWorld(const fuhgl::math::Mat4& parentWorld);
    void computeAabb(); ///< recursive: own mesh components + children

    std::string name_;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    std::vector<std::unique_ptr<Component>> components_;
    fuhgl::math::Mat4 worldMatrix_{};
    fuhgl::math::Vec3 aabbMin_{0.f, 0.f, 0.f};
    fuhgl::math::Vec3 aabbMax_{0.f, 0.f, 0.f};
    bool hasAabb_ = false;
};

} // namespace fuhglgraphics::scene
