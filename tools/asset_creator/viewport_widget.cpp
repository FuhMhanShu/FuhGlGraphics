#include "viewport_widget.hpp"

#include "asset_document.hpp"

#include <fuhgl/cam/first_person.hpp>
#include <fuhgl/gl/framebuffer.hpp>

#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(320, 240);
    camera_.fovY = fuhgl::math::radians(55.f);
    updateOrbitCamera();
}

void ViewportWidget::setDocument(AssetDocument* doc) {
    doc_ = doc;
    update();
}

void ViewportWidget::initializeGL() {
    if (!renderer_.create(pixelWidth(), pixelHeight(), 0)) {
        glReady_ = false;
        return;
    }
    renderer_.skyTop = {0.25f, 0.45f, 0.9f};
    renderer_.skyBottom = {0.85f, 0.88f, 0.95f};
    if (doc_)
        doc_->instantiateGpuResources(); // needs the now-current GL context
    glReady_ = true;
    update();
}

void ViewportWidget::resizeGL(int w, int h) {
    if (glReady_)
        renderer_.resize(static_cast<int>(w * devicePixelRatioF()),
                         static_cast<int>(h * devicePixelRatioF()));
}

void ViewportWidget::paintGL() {
    if (!glReady_ || !doc_)
        return;
    if (doc_->gpuResourcesPending())
        doc_->instantiateGpuResources(); // document was opened before the GL context existed

    auto& scene = doc_->scene();
    scene.updateWorldTransforms();

    // FrameData accumulates — reset it every frame
    frame_ = fuhglgraphics::scene::FrameData{};
    scene.fillFrame(frame_, &camera_, pixelWidth(), pixelHeight());
    frame_.effects.applyTo(renderer_);
    renderer_.render(frame_.view, &frame_.materialIds, &frame_.castShadow, &frame_.particleSystems);

    // present: blit the renderer's LDR output into the widget's default framebuffer
    fuhgl::Framebuffer::blitToDefault(renderer_.output(), pixelWidth(), pixelHeight());
}

void ViewportWidget::updateOrbitCamera() {
    const float ce = std::cos(orbitElevation_);
    camera_.position = orbitTarget_ + fuhgl::math::Vec3{
        orbitDistance_ * ce * std::sin(orbitAzimuth_),
        orbitDistance_ * std::sin(orbitElevation_),
        orbitDistance_ * ce * std::cos(orbitAzimuth_)};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(orbitTarget_ - camera_.position);
    camera_.yaw = std::atan2(d.x, -d.z);
    camera_.pitch = std::asin(d.y);
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    lastMousePos_ = event->pos();
    orbiting_ = event->button() == Qt::LeftButton;
    panning_ = event->button() == Qt::RightButton;
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - lastMousePos_;
    lastMousePos_ = event->pos();
    if (orbiting_) {
        orbitAzimuth_ -= delta.x() * 0.005f;
        orbitElevation_ = std::clamp(orbitElevation_ + delta.y() * 0.005f, -1.5f, 1.5f);
        updateOrbitCamera();
        update();
    } else if (panning_) {
        // pan the target in the camera's right/up plane, scaled by distance
        const float s = orbitDistance_ * 0.0016f;
        const fuhgl::math::Vec3 fwd = camera_.forward();
        const fuhgl::math::Vec3 right = fuhgl::math::normalize(fuhgl::math::cross(fwd, {0.f, 1.f, 0.f}));
        const fuhgl::math::Vec3 up = fuhgl::math::cross(right, fwd);
        orbitTarget_ = orbitTarget_ - right * (delta.x() * s) + up * (delta.y() * s);
        updateOrbitCamera();
        update();
    }
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) orbiting_ = false;
    if (event->button() == Qt::RightButton) panning_ = false;
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    const float factor = std::exp(-event->angleDelta().y() / 1200.f);
    orbitDistance_ = std::clamp(orbitDistance_ * factor, 1.f, 200.f);
    updateOrbitCamera();
    update();
}
