#pragma once

/// @file gl_viewport_widget.hpp
/// @brief G2/G11 editor building block (WITH_QT only): `editor::GlViewportWidget` — a minimal
/// QOpenGLWidget that owns the GL context lifecycle and frame pacing, while the actual drawing
/// is delegated to caller-supplied handlers (MVC: the widget is the View, model/renderer are
/// injected via setInitializeHandler()/setResizeHandler()/setPaintHandler()).
///
/// Rendering is on demand: call requestRender() after model changes; the dirty flag schedules a
/// repaint via update(). Call requestRender() again at the end of the paint handler to animate
/// continuously. setMaxFps() caps the frame rate (paced QTimer, 0 = unlimited); VSync is the
/// surface format's swap interval (setVSyncEnabled()). Frame stats (smoothed fps / inter-frame
/// ms) are emitted via frameRendered() — input for the G10 debug HUD. grabFrame() returns a
/// screenshot of the presented frame (G10 CI perceptual diff).
///
/// Conventions: PIMPL (Impl in the .cpp), no exceptions — a failing initialize handler returns
/// false + error string and is logged via qWarning. Qt lives only in editor/ and
/// include/fuhglgraphics/editor/ (WITH_QT); the library core stays Qt-free, so this header
/// refuses to compile without FUHGLGRAPHICS_WITH_QT=1.

#if !defined(FUHGLGRAPHICS_WITH_QT)
#error "fuhglgraphics/editor/gl_viewport_widget.hpp requires FUHGLGRAPHICS_WITH_QT=1 (Qt6 Widgets + OpenGLWidgets)"
#endif

#include <QOpenGLWidget>

#include <functional>
#include <memory>
#include <string>

class QImage;

namespace fuhglgraphics::editor {

/// @brief Minimal GL viewport widget: owns the QOpenGLWidget context lifecycle + frame pacing,
/// delegates drawing to the injected PaintHandler (see @file doc).
class GlViewportWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    /// @brief Called once from initializeGL() with a current, valid context. Return false and
    /// fill @p errorMessage on failure (logged; the widget stays black).
    using InitializeHandler = std::function<bool(std::string& errorMessage)>;
    /// @brief Called from resizeGL() with the new framebuffer size in device pixels
    /// (logical widget size * devicePixelRatioF()).
    using ResizeHandler = std::function<void(int deviceWidth, int deviceHeight)>;
    /// @brief Called from paintGL() to draw one frame; the context is current and Qt has
    /// already bound the widget's FBO with the viewport set to its full size.
    using PaintHandler = std::function<void()>;

    explicit GlViewportWidget(QWidget* parent = nullptr);
    ~GlViewportWidget() override;

    void setInitializeHandler(InitializeHandler handler);
    void setResizeHandler(ResizeHandler handler);
    void setPaintHandler(PaintHandler handler);

    /// @brief Frame-rate cap in frames per second, 0 (default) = unlimited (render on demand
    /// only). Pacing re-evaluates on every requestRender(), so changes apply immediately.
    void setMaxFps(int maxFps);
    int maxFps() const;

    /// @brief VSync via the surface format's swap interval (default on). Applies to the
    /// widget's requested format (before first show); for an already-created context the
    /// platform decides whether a runtime change takes effect.
    void setVSyncEnabled(bool enabled);
    bool isVSyncEnabled() const;

    /// @brief MSAA sample count requested for the context (default 0 = off). Effective only
    /// before the first show — the context is created lazily on first show.
    void setMsaaSamples(int samples);
    int msaaSamples() const;

    /// @brief Requested GL version, default 4.3 core (the G5 compute path; the renderer uses
    /// whatever the driver grants). Effective only before the first show.
    void setRequestedGlVersion(int major, int minor);

    /// @brief Marks the frame dirty and schedules a repaint respecting maxFps(). Call after
    /// every model change; call again at the end of the paint handler to animate.
    void requestRender();
    /// @brief True between requestRender() and the next completed paintGL().
    bool isDirty() const;

    /// @brief Screenshot of the current framebuffer (QOpenGLWidget::grabFramebuffer); null
    /// image before the first presented frame.
    QImage grabFrame();

    /// @brief Smoothed frames per second (EMA over inter-frame time; 0 until the 2nd frame).
    double lastFps() const;
    /// @brief Smoothed inter-frame time in milliseconds (same EMA; 0 until the 2nd frame).
    double lastFrameMs() const;

signals:
    /// @brief Emitted once from initializeGL() after the context is valid and the initialize
    /// handler ran (context is current while emitted).
    void glInitialized();
    /// @brief Emitted when the widget's GL context is about to be destroyed
    /// (QOpenGLContext::aboutToBeDestroyed) — free the handlers' GL resources here. The
    /// context may not be current at emission: call makeCurrent() first when needed.
    void glAboutToBeDestroyed();
    /// @brief Emitted after every painted frame: smoothed fps and inter-frame milliseconds.
    void frameRendered(double fps, double frameMs);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Impl;                  ///< PIMPL: handlers + pacing state + stats (defined in the .cpp)
    std::unique_ptr<Impl> impl_;

    void scheduleFrame();         ///< repaints now or after the remaining frame budget (maxFps)
};

} // namespace fuhglgraphics::editor
