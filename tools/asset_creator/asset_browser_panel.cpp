#include "asset_browser_panel.hpp"

#include "commands.hpp"

#include <QFileSystemModel>
#include <QFileInfo>

namespace assetcreator {

AssetBrowserPanel::AssetBrowserPanel(AssetDocument* doc, QWidget* parent)
    : QTreeView(parent), doc_(doc) {
    model_ = new QFileSystemModel(this);
    model_->setNameFilters({"*.gltf", "*.glb", "*.png", "*.fasset"});
    model_->setNameFilterDisables(false);
    setModel(model_);
    setRootIndex(model_->setRootPath(doc_->projectDir()));
    setHeaderHidden(true);
    setAcceptDrops(true);
    connect(this, &QTreeView::activated, this, &AssetBrowserPanel::onActivated);
    connect(doc_, &AssetDocument::documentReset, this, [this]() {
        setRootIndex(model_->setRootPath(doc_->projectDir()));
    });
}

void AssetBrowserPanel::setProjectDir(const QString& dir) {
    setRootIndex(model_->setRootPath(dir));
}

void AssetBrowserPanel::onActivated(const QModelIndex& index) {
    if (!pushCommand || !selectedNodeFn) return;
    const QString path = model_->filePath(index);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "gltf" && suffix != "glb") return;
    scene::Node* parent = selectedNodeFn();
    if (!parent) parent = &doc_->scene().root();
    pushCommand(makeLoadGltfCommand(doc_, parent, path));
}

} // namespace assetcreator
