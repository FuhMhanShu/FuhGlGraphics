#pragma once

/// @file gizmo.hpp
/// @brief G12 asset creator P5 — GizmoOverlay: a translate/rotate/scale manipulator for the
/// selected scene node, rendered as extra draw items appended to the document's FrameData
/// (so it shares the G3 G-buffer depth and is correctly occluded). The gizmo geometry lives
/// in its own scene::Scene (never serialized, never shown in the scene tree); picking runs
/// against that scene via scene::pick (G18). Dragging mutates the target node's TRS directly
/// (live), the controller pushes one undo command with the captured start TRS on release.

#include <fuhglgraphics/scene/scene.hpp>

#include <fuhgl/cam/camera.hpp>
#include <fuhgl/math.hpp>
#include <fuhgl/render/material.hpp>
#include <fuhgl/render/mesh.hpp>

#include <memory>
#include <string>
#include <vector>

namespace assetcreator {

class GizmoOverlay {
public:
    enum class Mode { Translate, Rotate, Scale };

    GizmoOverlay() = default;

    /// @brief Builds the gizmo part meshes (FuhGLCore solids: arrows = cylinder|cone, rings =
    /// torus, center = cube) and the internal gizmo scene. Needs a current GL context.
    bool create();
    /// @brief Frees the GL meshes. Needs a current GL context.
    void release();
    bool created() const { return created_; }

    void setTarget(fuhglgraphics::scene::Node* n) { target_ = n; }
    fuhglgraphics::scene::Node* target() const { return target_; }
    void setMode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }

    /// @brief Repositions the gizmo at the target's world transform (screen-space constant
    /// size: scaled by the camera distance). Call after the document scene's
    /// updateWorldTransforms(), before appendToFrame()/pickPart().
    void updateForTarget(const fuhgl::Camera& cam);

    /// @brief Appends the visible gizmo parts as draw items (emissive flat materials,
    /// no shadow casting) to `frame`. Call after updateForTarget().
    void appendToFrame(fuhglgraphics::scene::FrameData& frame, const fuhgl::Camera& cam, int w, int h);

    /// @brief Picks a gizmo part under pixel (px, py) — returns the part name ("x", "ry", "c",
    /// ...) or "" when none. Updates the gizmo transforms first.
    std::string pickPart(const fuhgl::Camera& cam, int w, int h, float px, float py);

    /// @brief Starts a drag on `part` (from pickPart) — captures the target's start TRS.
    void beginDrag(const std::string& part, const fuhgl::Camera& cam, int w, int h, float px, float py);
    /// @brief Mutates the target node's TRS from the current mouse ray (live, no undo).
    void dragTo(const fuhgl::Camera& cam, int w, int h, float px, float py);
    void endDrag() { dragging_ = false; activePart_.clear(); }
    bool dragging() const { return dragging_; }
    const std::string& activePart() const { return activePart_; }

    // start TRS captured at beginDrag — the controller builds the undo command from these
    fuhgl::math::Vec3 dragStartT{0.f, 0.f, 0.f};
    fuhgl::math::Quat dragStartR{};
    fuhgl::math::Vec3 dragStartS{1.f, 1.f, 1.f};

private:
    struct Part {
        std::string name;                 ///< "x".."z", "rx".."rz", "sx".."sz", "c"
        fuhgl::render::Mesh* mesh = nullptr;
        const fuhgl::render::MeshData* meshData = nullptr; ///< for scene::pick
        fuhgl::math::Quat rot{};          ///< local orientation of the part
        fuhgl::math::Vec3 color{1.f, 1.f, 1.f};
        char axis = 'c';                  ///< 'x'/'y'/'z' for axis parts, 'c' = center
        char kind = 't';                  ///< 't' translate, 'r' rotate, 's' scale, 'a' all-modes center
    };
    const Part* findPart(const std::string& name) const;
    fuhglgraphics::scene::Node* partNode(const std::string& name) const;

    fuhglgraphics::scene::Scene scene_;       ///< gizmo-only scene (parts as children of root)
    std::vector<Part> parts_;
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> meshes_;
    std::vector<std::unique_ptr<fuhgl::render::MeshData>> meshData_;
    fuhglgraphics::scene::Node* target_ = nullptr;
    Mode mode_ = Mode::Translate;
    bool created_ = false;

    // drag state
    bool dragging_ = false;
    std::string activePart_;
    fuhgl::math::Vec3 center_{0.f, 0.f, 0.f};   ///< target world position at beginDrag
    fuhgl::math::Vec3 axisWorld_{0.f, 1.f, 0.f};///< drag axis in world space
    float startAlong_ = 0.f;                    ///< axis parameter of the initial hit
    fuhgl::math::Vec3 startPoint_{0.f, 0.f, 0.f};///< initial hit point (center/plane drags)
    float startAngle_ = 0.f;                    ///< initial angle around the axis (rotate)
};

} // namespace assetcreator
