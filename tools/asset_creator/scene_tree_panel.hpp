#pragma once

/// @file scene_tree_panel.hpp
/// @brief G12 asset creator — SceneTreePanel (view): QTreeView over SceneTreeModel with a
/// context menu (add child node / shape / light / water, remove, load glTF here). Emits
/// commands through the document's QUndoStack via the callbacks set by MainWindow.

#include "asset_document.hpp"
#include "scene_tree_model.hpp"

#include <QTreeView>

#include <functional>

namespace assetcreator {

class SceneTreePanel : public QTreeView {
    Q_OBJECT
public:
    SceneTreePanel(AssetDocument* doc, QWidget* parent = nullptr);

    SceneTreeModel* model() { return model_; }
    fuhglgraphics::scene::Node* selectedNode() const;

    /// @brief Called by MainWindow to (re)apply a command and refresh.
    std::function<void(std::unique_ptr<QUndoCommand>)> pushCommand;

signals:
    void nodeSelected(fuhglgraphics::scene::Node* node);

private slots:
    void onContextMenu(const QPoint& pos);
    void onActivated(const QModelIndex& index);

private:
    AssetDocument* doc_;
    SceneTreeModel* model_;
};

} // namespace assetcreator
