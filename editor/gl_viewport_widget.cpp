/// @file gl_viewport_widget.cpp — implementation of editor::GlViewportWidget, see
/// include/fuhglgraphics/editor/gl_viewport_widget.hpp.

#include <fuhglgraphics/editor/gl_viewport_widget.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <chrono>

namespace fuhglgraphics::editor {

struct GlViewportWidget::Impl {
    InitializeHandler initializeHandler;
    ResizeHandler resizeHandler;
    PaintHandler paintHandler;

    int maxFps = 0;               ///< frame-rate cap, 0 = unlimited
    int msaaSamples = 0;
    bool vsyncEnabled = true;
    int requestedGlMajor = 4;
    int requestedGlMinor = 3;

    bool initialized = false;     ///< initializeGL() ran with a valid context
    bool dirty = false;           ///< a requestRender() is pending
    QElapsedTimer frameClock;     ///< started in initializeGL()
    qint64 lastFrameClockMs = -1; ///< frameClock value at the previous paintGL()
    double fpsSmoothed = 0.0;     ///< EMA of 1000 / inter-frame ms
    double frameMsSmoothed = 0.0; ///< EMA of inter-frame ms
};

GlViewportWidget::GlViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent), impl_(std::make_unique<Impl>()) {
    QSurfaceFormat requestedFormat;
    requestedFormat.setRenderableType(QSurfaceFormat::OpenGL);
    requestedFormat.setVersion(impl_->requestedGlMajor, impl_->requestedGlMinor);
    requestedFormat.setProfile(QSurfaceFormat::CoreProfile);
    requestedFormat.setDepthBufferSize(24);
    requestedFormat.setStencilBufferSize(8);
    requestedFormat.setSamples(impl_->msaaSamples);
    requestedFormat.setSwapInterval(impl_->vsyncEnabled ? 1 : 0);
    setFormat(requestedFormat); // must happen before the widget (or its window) is first shown
}

GlViewportWidget::~GlViewportWidget() = default;

void GlViewportWidget::setInitializeHandler(InitializeHandler handler) {
    impl_->initializeHandler = std::move(handler);
}
void GlViewportWidget::setResizeHandler(ResizeHandler handler) {
    impl_->resizeHandler = std::move(handler);
}
void GlViewportWidget::setPaintHandler(PaintHandler handler) {
    impl_->paintHandler = std::move(handler);
}

void GlViewportWidget::setMaxFps(int maxFps) {
    impl_->maxFps = std::max(0, maxFps);
    scheduleFrame(); // re-evaluate a pending frame against the new budget
}
int GlViewportWidget::maxFps() const { return impl_->maxFps; }

void GlViewportWidget::setVSyncEnabled(bool enabled) {
    impl_->vsyncEnabled = enabled;
    QSurfaceFormat requestedFormat = format();
    requestedFormat.setSwapInterval(enabled ? 1 : 0);
    setFormat(requestedFormat);
}
bool GlViewportWidget::isVSyncEnabled() const { return impl_->vsyncEnabled; }

void GlViewportWidget::setMsaaSamples(int samples) {
    impl_->msaaSamples = std::max(0, samples);
    QSurfaceFormat requestedFormat = format();
    requestedFormat.setSamples(impl_->msaaSamples);
    setFormat(requestedFormat);
}
int GlViewportWidget::msaaSamples() const { return impl_->msaaSamples; }

void GlViewportWidget::setRequestedGlVersion(int major, int minor) {
    impl_->requestedGlMajor = major;
    impl_->requestedGlMinor = minor;
    QSurfaceFormat requestedFormat = format();
    requestedFormat.setVersion(major, minor);
    setFormat(requestedFormat);
}

void GlViewportWidget::requestRender() {
    impl_->dirty = true;
    scheduleFrame();
}
bool GlViewportWidget::isDirty() const { return impl_->dirty; }

QImage GlViewportWidget::grabFrame() { return grabFramebuffer(); }

double GlViewportWidget::lastFps() const { return impl_->fpsSmoothed; }
double GlViewportWidget::lastFrameMs() const { return impl_->frameMsSmoothed; }

void GlViewportWidget::initializeGL() {
    Impl* d = impl_.get();
    QOpenGLContext* glContext = context();
    if (glContext == nullptr || !glContext->isValid()) {
        qWarning() << "GlViewportWidget: no valid GL context, requested" << format().majorVersion()
                   << "." << format().minorVersion() << "core, samples" << format().samples();
        return;
    }
    const QSurfaceFormat actualFormat = glContext->format();
    qInfo() << "GlViewportWidget: GL" << actualFormat.majorVersion() << "."
            << actualFormat.minorVersion()
            << (actualFormat.profile() == QSurfaceFormat::CoreProfile ? "core" : "compat")
            << "profile, samples" << actualFormat.samples()
            << ", swapInterval" << actualFormat.swapInterval();
    connect(glContext, &QOpenGLContext::aboutToBeDestroyed, this, [this]() { emit glAboutToBeDestroyed(); });
    d->frameClock.start();
    d->lastFrameClockMs = -1;
    d->initialized = true;
    if (d->initializeHandler) {
        std::string errorMessage;
        if (!d->initializeHandler(errorMessage))
            qWarning() << "GlViewportWidget: initialize handler failed:"
                       << QString::fromStdString(errorMessage);
    }
    emit glInitialized();
    requestRender();
}

void GlViewportWidget::resizeGL(int /*width*/, int /*height*/) {
    // The handlers work in device pixels: the FBO is logical size * devicePixelRatioF().
    const qreal dpr = devicePixelRatioF();
    const int deviceWidth = qRound(width() * dpr);
    const int deviceHeight = qRound(height() * dpr);
    if (impl_->resizeHandler)
        impl_->resizeHandler(deviceWidth, deviceHeight);
    impl_->dirty = true; // content must be redrawn at the new size
    scheduleFrame();
}

void GlViewportWidget::paintGL() {
    Impl* d = impl_.get();
    const qint64 nowMs = d->frameClock.elapsed();
    if (d->lastFrameClockMs >= 0) {
        const double frameMs = double(nowMs - d->lastFrameClockMs);
        // EMA so single hitches don't dominate the reported stats (G10 debug HUD)
        d->frameMsSmoothed =
            d->frameMsSmoothed <= 0.0 ? frameMs : 0.9 * d->frameMsSmoothed + 0.1 * frameMs;
        d->fpsSmoothed = d->frameMsSmoothed > 0.0 ? 1000.0 / d->frameMsSmoothed : 0.0;
    }
    d->lastFrameClockMs = nowMs;
    d->dirty = false;
    if (d->paintHandler)
        d->paintHandler();
    emit frameRendered(d->fpsSmoothed, d->frameMsSmoothed);
    if (d->dirty) // re-requested from inside the paint handler (animation loop)
        scheduleFrame();
}

void GlViewportWidget::showEvent(QShowEvent* event) {
    QOpenGLWidget::showEvent(event);
    scheduleFrame(); // a frame that became dirty while hidden paints now
}

void GlViewportWidget::hideEvent(QHideEvent* event) {
    QOpenGLWidget::hideEvent(event);
    // nothing to stop explicitly: scheduleFrame() checks isVisible() before update()
}

void GlViewportWidget::scheduleFrame() {
    if (!impl_->dirty || !impl_->initialized || !isVisible())
        return;
    const qint64 minIntervalMs = impl_->maxFps > 0 ? std::max<qint64>(1, 1000 / impl_->maxFps) : 0;
    const qint64 sinceLastFrameMs = impl_->lastFrameClockMs < 0
                                        ? minIntervalMs
                                        : impl_->frameClock.elapsed() - impl_->lastFrameClockMs;
    const qint64 waitMs = minIntervalMs - sinceLastFrameMs;
    if (waitMs <= 0) {
        update();
    } else {
        // re-check on wakeup: hidden or torn-down widgets must not repaint
        QTimer::singleShot(std::chrono::milliseconds(waitMs), this, [this]() {
            if (impl_->dirty && impl_->initialized && isVisible())
                update();
        });
    }
}

} // namespace fuhglgraphics::editor
