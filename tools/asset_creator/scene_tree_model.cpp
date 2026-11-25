#include "scene_tree_model.hpp"

#include <fuhglgraphics/scene/node.hpp>

namespace assetcreator {

using namespace fuhglgraphics;

SceneTreeModel::SceneTreeModel(scene::Scene* scene, QObject* parent)
    : QAbstractItemModel(parent), scene_(scene) {}

void SceneTreeModel::reset() {
    beginResetModel();
    endResetModel();
}

QModelIndex SceneTreeModel::indexFor(scene::Node* node, scene::Node* parent) const {
    if (!node || node == &scene_->root()) return QModelIndex();
    int row = 0;
    if (parent) {
        const auto& children = parent->children();
        for (std::size_t i = 0; i < children.size(); ++i)
            if (children[i].get() == node) { row = static_cast<int>(i); break; }
    }
    return createIndex(row, 0, node);
}

QModelIndex SceneTreeModel::indexOf(scene::Node* node) const {
    if (!node) return QModelIndex();
    return indexFor(node, node->parent());
}

scene::Node* SceneTreeModel::nodeOf(const QModelIndex& index) const {
    if (!index.isValid()) return nullptr;
    return static_cast<scene::Node*>(index.internalPointer());
}

QModelIndex SceneTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (column != 0) return QModelIndex();
    scene::Node* p = parent.isValid() ? static_cast<scene::Node*>(parent.internalPointer())
                                      : &scene_->root();
    if (row < 0 || static_cast<std::size_t>(row) >= p->childCount()) return QModelIndex();
    return createIndex(row, 0, p->children()[static_cast<std::size_t>(row)].get());
}

QModelIndex SceneTreeModel::parent(const QModelIndex& index) const {
    if (!index.isValid()) return QModelIndex();
    scene::Node* n = static_cast<scene::Node*>(index.internalPointer());
    scene::Node* p = n->parent();
    if (!p || p == &scene_->root()) return QModelIndex();
    return indexFor(p, p->parent());
}

int SceneTreeModel::rowCount(const QModelIndex& parent) const {
    scene::Node* p = parent.isValid() ? static_cast<scene::Node*>(parent.internalPointer())
                                      : &scene_->root();
    return static_cast<int>(p->childCount());
}

int SceneTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant SceneTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) return QVariant();
    scene::Node* n = static_cast<scene::Node*>(index.internalPointer());
    return QString::fromStdString(n->name());
}

QVariant SceneTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section == 0)
        return tr("Scene");
    return QVariant();
}

} // namespace assetcreator
