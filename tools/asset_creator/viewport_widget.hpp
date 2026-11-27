#pragma once

/// @file viewport_widget.hpp
/// @brief G12 asset creator — Viewport (MVC view): wraps editor::GlViewportWidget and renders
/// the document's scene::Scene through the G3 DeferredRenderer each paint (on demand via
/// requestRender()), blitting the renderer output to the widget's default framebuffer.
/// Owns the orbit camera state used by ViewportController.

#include "asset_document.hpp"
#include "gizmo.hpp"

#include <fuhglgraphics/editor/gl_viewport_widget.hpp>
#include <fuhglgraphics/render/deferred_renderer.hpp>

#include <fuhgl/cam/first_person.hpp>

#include <QElapsedTimer>
#include <QWidget>

#include <map>
#include <memory>

namespace assetcreator {

class ViewportWidget : public QWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(AssetDocument* doc, QWidget* parent = nullptr);

    fuhglgraphics::editor::GlViewportWidget* glWidget() { return gl_; }
    fuhgl::FirstPersonCamera& camera() { return camera_; }
    fuhglgraphics::render::DeferredRenderer* renderer() { return renderer_.get(); }
    bool glReady() const { return glReady_; }

    /// @brief Orbit state (the controller edits this; the widget applies it to the camera).
    struct Orbit {
        float yaw = 0.6f, pitch = 0.35f;   ///< radians
        float distance = 14.f;             ///< world units from target
        fuhgl::math::Vec3 target{0.f, 1.f, 0.f};
    };
    Orbit& orbit() { return orbit_; }
    void applyOrbitToCamera();

    int msaaSamples() const { return msaa_; }
    void setMsaaSamples(int s) { msaa_ = s; }
    float renderScale() const { return renderScale_; }
    void setRenderScale(float s);

    void requestRender() { gl_->requestRender(); }

    /// @brief P5 gizmo: the manipulator follows this node (nullptr hides it).
    GizmoOverlay& gizmo() { return gizmo_; }
    void setGizmoTarget(fuhglgraphics::scene::Node* n) { gizmo_.setTarget(n); requestRender(); }

    /// @brief P5 animation playback: advances every AnimationComponent in the document scene
    /// per painted frame and re-renders continuously; the node TRS at play start is restored
    /// on stop.
    void setAnimationPlaying(bool on);
    bool animationPlaying() const { return animPlaying_; }
    /// @brief Total number of animation clips in the document scene (0 = nothing to play).
    int animationClipCount() const;

    /// @brief TRS snapshot entry (animation playback restore on stop).
    struct Trs {
        fuhgl::math::Vec3 t;
        fuhgl::math::Quat r;
        fuhgl::math::Vec3 s;
    };

signals:
    void frameRendered(double fps, double frameMs);
    void glReadyChanged(bool ready);

private:
    void stepAnimation(float dt);

    AssetDocument* doc_;
    fuhglgraphics::editor::GlViewportWidget* gl_;
    std::unique_ptr<fuhglgraphics::render::DeferredRenderer> renderer_;
    GizmoOverlay gizmo_;
    fuhgl::FirstPersonCamera camera_;
    Orbit orbit_;
    bool glReady_ = false;
    int msaa_ = 0;
    float renderScale_ = 1.f;
    int devW_ = 0, devH_ = 0;
    // P5 animation playback state
    bool animPlaying_ = false;
    QElapsedTimer animClock_;
    std::map<fuhglgraphics::scene::Node*, Trs> animSnapshot_;
};

} // namespace assetcreator
