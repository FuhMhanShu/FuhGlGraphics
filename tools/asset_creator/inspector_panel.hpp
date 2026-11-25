#pragma once

/// @file inspector_panel.hpp
/// @brief G12 asset creator — InspectorPanel (view): edits the selected node — name, TRS,
/// the per-node effect list (all render::Effect toggles) and the common EffectParams —
/// plus the G13 water parameters when the node has a WaterComponent. Every edit goes
/// through MainWindow's pushCommand (QUndoStack).

#include "asset_document.hpp"

#include <fuhglgraphics/scene/scene.hpp>

#include <QWidget>

#include <functional>
#include <memory>

class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QCheckBox;
class QFormLayout;
class QVBoxLayout;

namespace assetcreator {

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    InspectorPanel(AssetDocument* doc, QWidget* parent = nullptr);

    std::function<void(std::unique_ptr<QUndoCommand>)> pushCommand;

    void setNode(fuhglgraphics::scene::Node* node); ///< rebuild the form for the selection

private:
    void rebuild();

    AssetDocument* doc_;
    fuhglgraphics::scene::Node* node_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QListWidget* effectsList_ = nullptr;
    QVBoxLayout* paramsLayout_ = nullptr;
    QWidget* waterBox_ = nullptr;
    QDoubleSpinBox* trsBoxes_[2][3] = {}; ///< [0=translation 1=scale][xyz]
    QDoubleSpinBox* quatBoxes_[4] = {};  ///< rotation quat [x y z w]
    bool updating_ = false;
};

} // namespace assetcreator
