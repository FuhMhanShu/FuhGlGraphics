#pragma once

/// @file asset_browser_panel.hpp
/// @brief G12 asset creator — AssetBrowserPanel (view): QFileSystemModel on the project
/// folder, filtered to *.gltf *.glb *.png *.fasset; double-click / drop loads a glTF under
/// the selected node (via MainWindow's pushCommand).

#include "asset_document.hpp"

#include <QTreeView>

#include <functional>
#include <memory>

class QFileSystemModel;

namespace assetcreator {

class AssetBrowserPanel : public QTreeView {
    Q_OBJECT
public:
    AssetBrowserPanel(AssetDocument* doc, QWidget* parent = nullptr);

    std::function<void(std::unique_ptr<QUndoCommand>)> pushCommand;
    std::function<fuhglgraphics::scene::Node*()> selectedNodeFn;

    void setProjectDir(const QString& dir);

private slots:
    void onActivated(const QModelIndex& index);

private:
    AssetDocument* doc_;
    QFileSystemModel* model_;
};

} // namespace assetcreator
