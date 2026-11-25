#include "scene_tree_panel.hpp"

#include "commands.hpp"

#include <fuhglgraphics/scene/node.hpp>

#include <QFileDialog>
#include <QMenu>

namespace assetcreator {

using namespace fuhglgraphics;

SceneTreePanel::SceneTreePanel(AssetDocument* doc, QWidget* parent)
    : QTreeView(parent), doc_(doc) {
    model_ = new SceneTreeModel(&doc->scene(), this);
    setModel(model_);
    setHeaderHidden(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeView::customContextMenuRequested, this, &SceneTreePanel::onContextMenu);
    connect(this, &QTreeView::activated, this, &SceneTreePanel::onActivated);
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        emit nodeSelected(selectedNode());
    });
    connect(doc_, &AssetDocument::changed, this, [this]() { model_->reset(); expandAll(); });
    connect(doc_, &AssetDocument::documentReset, this, [this]() { model_->reset(); expandAll(); });
}

scene::Node* SceneTreePanel::selectedNode() const {
    const QModelIndex idx = currentIndex();
    return model_->nodeOf(idx);
}

void SceneTreePanel::onActivated(const QModelIndex& index) {
    emit nodeSelected(model_->nodeOf(index));
}

void SceneTreePanel::onContextMenu(const QPoint& pos) {
    scene::Node* node = selectedNode();
    scene::Node* parent = node ? node : &doc_->scene().root();
    QMenu menu(this);
    QAction* addChild = menu.addAction(tr("Add child node"));
    QAction* addBox = menu.addAction(tr("Add box"));
    QAction* addQuad = menu.addAction(tr("Add quad"));
    QAction* addSphere = menu.addAction(tr("Add sphere"));
    QAction* addLight = menu.addAction(tr("Add light"));
    QAction* addWater = menu.addAction(tr("Add water (G13)"));
    menu.addSeparator();
    QAction* loadGltf = menu.addAction(tr("Load glTF here..."));
    QAction* remove = nullptr;
    if (node && node != &doc_->scene().root()) {
        menu.addSeparator();
        remove = menu.addAction(tr("Remove node"));
    }
    QAction* picked = menu.exec(viewport()->mapToGlobal(pos));
    if (!picked || !pushCommand) return;
    const int index = static_cast<int>(doc_->scene().root().childCount()) + 1;
    if (picked == addChild)
        pushCommand(makeAddNodeCommand(doc_, parent, QStringLiteral("node%1").arg(index)));
    else if (picked == addBox)
        pushCommand(makeAddShapeCommand(doc_, parent, "box", QStringLiteral("box%1").arg(index)));
    else if (picked == addQuad)
        pushCommand(makeAddShapeCommand(doc_, parent, "quad", QStringLiteral("quad%1").arg(index)));
    else if (picked == addSphere)
        pushCommand(makeAddShapeCommand(doc_, parent, "sphere", QStringLiteral("sphere%1").arg(index)));
    else if (picked == addLight)
        pushCommand(makeAddLightCommand(doc_, parent, "directional", QStringLiteral("light%1").arg(index)));
    else if (picked == addWater)
        pushCommand(makeAddWaterCommand(doc_, parent, QStringLiteral("water%1").arg(index), 96, 120.f));
    else if (picked == loadGltf) {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Load glTF asset"), doc_->projectDir(), tr("glTF assets (*.gltf *.glb)"));
        if (!path.isEmpty()) pushCommand(makeLoadGltfCommand(doc_, parent, path));
    } else if (picked == remove && node) {
        pushCommand(makeRemoveNodeCommand(doc_, node));
    }
}

} // namespace assetcreator
