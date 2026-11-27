#pragma once

/// @file asset_browser_panel.hpp
/// @brief G12 asset creator — AssetBrowserPanel (view): QFileSystemModel on the project
/// folder, filtered to *.gltf *.glb *.png *.fasset; double-click / drop loads a glTF under
/// the selected node (via MainWindow's pushCommand). P5: a proxy model decorates .gltf/.glb
/// rows with rendered thumbnails from <project>/.cache/thumbs (see thumbnailer.hpp).

#include "asset_document.hpp"

#include <QTreeView>

#include <functional>
#include <memory>

class QFileSystemModel;
class QIdentityProxyModel;

namespace assetcreator {

class AssetBrowserPanel : public QTreeView {
    Q_OBJECT
public:
    AssetBrowserPanel(AssetDocument* doc, QWidget* parent = nullptr);

    std::function<void(std::unique_ptr<QUndoCommand>)> pushCommand;
    std::function<fuhglgraphics::scene::Node*()> selectedNodeFn;

    void setProjectDir(const QString& dir);

    /// @brief P5: renders missing glTF thumbnails below the project dir (needs a current GL
    /// context — the caller makes the viewport context current first) and refreshes the
    /// icons. Returns the number of newly rendered thumbnails.
    int generateThumbnails(int size = 128);
    /// @brief Re-reads the thumbnail icons (after generateThumbnails()).
    void refreshIcons();

private slots:
    void onActivated(const QModelIndex& index);

private:
    AssetDocument* doc_;
    QFileSystemModel* model_;
    QIdentityProxyModel* proxy_;
    QString projectDir_;
};

} // namespace assetcreator
