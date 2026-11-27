#pragma once

/// @file viewport_controller.hpp
/// @brief G12 asset creator — ViewportController (MVC controller): orbit camera via mouse
/// (left-drag rotate, right/middle-drag pan, wheel zoom) and click-picking (scene::pick)
/// on the ViewportWidget; reports the picked node through nodePicked().

#include "viewport_widget.hpp"

#include <fuhglgraphics/scene/scene.hpp>

#include <QObject>

#include <functional>
#include <memory>

class QUndoCommand;

namespace assetcreator {

class ViewportController : public QObject {
    Q_OBJECT
public:
    ViewportController(ViewportWidget* viewport, AssetDocument* doc, QObject* parent = nullptr);

    /// @brief Installs the event filter on the GL widget.
    void install();

    /// @brief P5: a finished gizmo drag pushes one undo command (wired by MainWindow).
    std::function<void(std::unique_ptr<QUndoCommand>)> pushCommand;

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
    bool gizmoDragging_ = false; ///< P5: left-drag on a gizmo part instead of orbit/pick
    QPoint pressPos_;
    QPoint lastPos_;
};

} // namespace assetcreator
