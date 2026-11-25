#include "viewport_controller.hpp"

#include <fuhglgraphics/scene/picking.hpp>

#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>

namespace assetcreator {

using namespace fuhglgraphics;

ViewportController::ViewportController(ViewportWidget* viewport, AssetDocument* doc, QObject* parent)
    : QObject(parent), vp_(viewport), doc_(doc) {}

void ViewportController::install() { vp_->glWidget()->installEventFilter(this); }

bool ViewportController::eventFilter(QObject* watched, QEvent* event) {
    auto* glw = vp_->glWidget();
    if (watched != glw) return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        pressPos_ = me->position().toPoint();
        lastPos_ = me->position().toPoint();
        moved_ = false;
        dragging_ = (me->button() == Qt::LeftButton);
        panning_ = (me->button() == Qt::RightButton || me->button() == Qt::MiddleButton);
        return true;
    }
    if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint delta = me->position().toPoint() - lastPos_;
        lastPos_ = me->position().toPoint();
        if ((dragging_ || panning_) && (me->position().toPoint() - pressPos_).manhattanLength() > 3) moved_ = true;
        ViewportWidget::Orbit& orbit = vp_->orbit();
        if (dragging_ && moved_) {
            orbit.yaw -= static_cast<float>(delta.x()) * 0.008f;
            orbit.pitch = std::max(-1.5f, std::min(1.5f, orbit.pitch + static_cast<float>(delta.y()) * 0.008f));
            vp_->requestRender();
        } else if (panning_ && moved_) {
            const fuhgl::math::Vec3 fwd = fuhgl::math::normalize(
                fuhgl::math::Vec3{std::sin(orbit.yaw), 0.f, -std::cos(orbit.yaw)});
            const fuhgl::math::Vec3 right = fuhgl::math::normalize(fuhgl::math::cross(fwd, {0.f, 1.f, 0.f}));
            const float k = orbit.distance * 0.0016f;
            orbit.target = orbit.target - right * (static_cast<float>(delta.x()) * k) +
                           fuhgl::math::Vec3{0.f, 1.f, 0.f} * (static_cast<float>(delta.y()) * k);
            vp_->requestRender();
        }
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!moved_ && me->button() == Qt::LeftButton) {
            // click without drag -> pick
            scene::PickHit hit;
            const int w = glw->width(), h = glw->height();
            if (w > 0 && h > 0 &&
                scene::pick(doc_->scene(), &vp_->camera(), w, h,
                            static_cast<float>(me->position().x()), static_cast<float>(me->position().y()), hit) &&
                hit.hit) {
                emit nodePicked(const_cast<scene::Node*>(hit.node));
                emit statusMessage(QStringLiteral("picked %1 (t=%2)")
                                       .arg(QString::fromStdString(hit.node->name()))
                                       .arg(hit.distance, 0, 'f', 2));
            } else {
                emit nodePicked(nullptr);
            }
        }
        dragging_ = panning_ = false;
        return true;
    }
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        ViewportWidget::Orbit& orbit = vp_->orbit();
        const float factor = we->angleDelta().y() > 0 ? 0.9f : 1.1f;
        orbit.distance = std::max(1.f, std::min(500.f, orbit.distance * factor));
        vp_->requestRender();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace assetcreator
