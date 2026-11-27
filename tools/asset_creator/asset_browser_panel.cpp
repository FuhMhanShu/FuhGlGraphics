#include "asset_browser_panel.hpp"

#include "commands.hpp"
#include "thumbnailer.hpp"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QIcon>
#include <QIdentityProxyModel>

namespace assetcreator {

namespace {

/// @brief P5: decorates .gltf/.glb rows with their cached thumbnail (thumbnailer.hpp).
class ThumbnailProxyModel : public QIdentityProxyModel {
public:
    ThumbnailProxyModel(QFileSystemModel* src, QObject* parent) : QIdentityProxyModel(parent), src_(src) {}
    void setProjectDir(const QString& dir) { projectDir_ = dir; }
    QVariant data(const QModelIndex& index, int role) const override {
        if (role == Qt::DecorationRole && index.column() == 0) {
            const QString path = src_->filePath(mapToSource(index));
            const QString suffix = QFileInfo(path).suffix().toLower();
            if (suffix == "gltf" || suffix == "glb") {
                const QString thumb = thumbnailer::thumbnailPathFor(path, projectDir_);
                if (QFileInfo::exists(thumb)) return QIcon(thumb);
            }
        }
        return QIdentityProxyModel::data(index, role);
    }

private:
    QFileSystemModel* src_;
    QString projectDir_;
};

} // namespace

AssetBrowserPanel::AssetBrowserPanel(AssetDocument* doc, QWidget* parent)
    : QTreeView(parent), doc_(doc) {
    model_ = new QFileSystemModel(this);
    model_->setNameFilters({"*.gltf", "*.glb", "*.png", "*.fasset"});
    model_->setNameFilterDisables(false);
    proxy_ = new ThumbnailProxyModel(model_, this);
    proxy_->setSourceModel(model_);
    setModel(proxy_);
    setIconSize(QSize(56, 56));
    setProjectDir(doc_->projectDir());
    setHeaderHidden(true);
    setAcceptDrops(true);
    connect(this, &QTreeView::activated, this, &AssetBrowserPanel::onActivated);
    connect(doc_, &AssetDocument::documentReset, this, [this]() { setProjectDir(doc_->projectDir()); });
}

void AssetBrowserPanel::setProjectDir(const QString& dir) {
    projectDir_ = dir;
    static_cast<ThumbnailProxyModel*>(proxy_)->setProjectDir(dir);
    setRootIndex(proxy_->mapFromSource(model_->setRootPath(dir)));
}

int AssetBrowserPanel::generateThumbnails(int size) {
    return thumbnailer::generateMissing(projectDir_, size); // needs a current GL context
}

void AssetBrowserPanel::refreshIcons() {
    const int rows = proxy_->rowCount();
    if (rows > 0) emit proxy_->dataChanged(proxy_->index(0, 0), proxy_->index(rows - 1, 0));
}

void AssetBrowserPanel::onActivated(const QModelIndex& index) {
    if (!pushCommand || !selectedNodeFn) return;
    const QString path = model_->filePath(proxy_->mapToSource(index));
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "gltf" && suffix != "glb") return;
    fuhglgraphics::scene::Node* parent = selectedNodeFn();
    if (!parent) parent = &doc_->scene().root();
    pushCommand(makeLoadGltfCommand(doc_, parent, path));
}

} // namespace assetcreator
