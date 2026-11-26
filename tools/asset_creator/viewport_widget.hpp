#pragma once

/// @file viewport_widget.hpp
/// @brief G12 asset creator — view (MVC): the central 3D viewport. A QOpenGLWidget that
/// renders the document's scene::Scene through the G3 render::DeferredRenderer and blits
/// the result to the widget. Mouse: LMB orbit, RMB pan, wheel zoom (P1; gizmo/selection
/// are P2). Rendering is on-demand (update() when the model changes) — paintGL re-renders
/// only when the widget is visible.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/cam/first_person.hpp>

#include <QOpenGLWidget>
#include <QPoint>

class AssetDocument;

class ViewportWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);

    void setDocument(AssetDocument* doc); ///< switch to another document (re-renders)
    AssetDocument* document() const { return doc_; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateOrbitCamera();
    int pixelWidth() const { return static_cast<int>(width() * devicePixelRatioF()); }
    int pixelHeight() const { return static_cast<int>(height() * devicePixelRatioF()); }

    AssetDocument* doc_ = nullptr;
    fuhglgraphics::render::DeferredRenderer renderer_;
    fuhglgraphics::scene::FrameData frame_;
    fuhgl::FirstPersonCamera camera_;
    bool glReady_ = false;

    // orbit camera state
    float orbitAzimuth_ = fuhgl::math::radians(35.f);
    float orbitElevation_ = fuhgl::math::radians(18.f);
    float orbitDistance_ = 14.f;
    fuhgl::math::Vec3 orbitTarget_{0.f, 1.f, 0.f};
    QPoint lastMousePos_;
    bool orbiting_ = false; ///< LMB held
    bool panning_ = false;   ///< RMB held
};
