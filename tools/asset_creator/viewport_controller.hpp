#pragma once

/// @file viewport_controller.hpp
/// @brief G12 asset creator — ViewportController (MVC controller): orbit camera via mouse
/// (left-drag rotate, right/middle-drag pan, wheel zoom) and click-picking (scene::pick)
/// on the ViewportWidget; reports the picked node through nodePicked().

#include "viewport_widget.hpp"

#include <fuhglgraphics/scene/scene.hpp>

#include <QObject>

namespace assetcreator {

class ViewportController : public QObject {
    Q_OBJECT
public:
    ViewportController(ViewportWidget* viewport, AssetDocument* doc, QObject* parent = nullptr);

    /// @brief Installs the event filter on the GL widget.
    void install();

signals:
    void nodePicked(fuhglgraphics::scene::Node* node);
    void statusMessage(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ViewportWidget* vp_;
    AssetDocument* doc_;
    bool dragging_ = false;
    bool panning_ = false;
    bool moved_ = false;
    QPoint pressPos_;
    QPoint lastPos_;
};

} // namespace assetcreator
