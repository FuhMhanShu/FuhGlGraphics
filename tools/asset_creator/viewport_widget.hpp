#pragma once

/// @file viewport_widget.hpp
/// @brief G12 asset creator — Viewport (MVC view): wraps editor::GlViewportWidget and renders
/// the document's scene::Scene through the G3 DeferredRenderer each paint (on demand via
/// requestRender()), blitting the renderer output to the widget's default framebuffer.
/// Owns the orbit camera state used by ViewportController.

#include "asset_document.hpp"

#include <fuhglgraphics/editor/gl_viewport_widget.hpp>
#include <fuhglgraphics/render/deferred_renderer.hpp>

#include <fuhgl/cam/first_person.hpp>

#include <QWidget>

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

signals:
    void frameRendered(double fps, double frameMs);
    void glReadyChanged(bool ready);

private:
    AssetDocument* doc_;
    fuhglgraphics::editor::GlViewportWidget* gl_;
    std::unique_ptr<fuhglgraphics::render::DeferredRenderer> renderer_;
    fuhgl::FirstPersonCamera camera_;
    Orbit orbit_;
    bool glReady_ = false;
    int msaa_ = 0;
    float renderScale_ = 1.f;
    int devW_ = 0, devH_ = 0;
};

} // namespace assetcreator
