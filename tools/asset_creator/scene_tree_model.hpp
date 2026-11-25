#pragma once

/// @file scene_tree_model.hpp
/// @brief G12 asset creator — SceneTreeModel: QAbstractItemModel over scene::Node*
/// (internalPointer = the node). Refreshed with reset() after every QUndoCommand.

#include <fuhglgraphics/scene/scene.hpp>

#include <QAbstractItemModel>

namespace assetcreator {

class SceneTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit SceneTreeModel(fuhglgraphics::scene::Scene* scene, QObject* parent = nullptr);

    void reset(); ///< call after any structural change

    QModelIndex indexOf(fuhglgraphics::scene::Node* node) const;
    fuhglgraphics::scene::Node* nodeOf(const QModelIndex& index) const;

    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    static QModelIndex indexFor(fuhglgraphics::scene::Node* node,
                                fuhglgraphics::scene::Node* parent);
    fuhglgraphics::scene::Scene* scene_;
};

} // namespace assetcreator
