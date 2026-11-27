#pragma once

/// @file commands.hpp
/// @brief G12 asset creator — Controller-side edits as QUndoCommands (MVC): every model
/// change goes through the document's QUndoStack, marks it dirty and emits changed().
/// LambdaCommand keeps the command set compact; structural commands keep ownership
/// (takeChild/addChild) so undo restores the exact subtree.

#include "asset_document.hpp"

#include <QUndoCommand>

#include <functional>

namespace assetcreator {

/// @brief Generic undoable edit: redo()/undo() lambdas + dirty/changed notification.
class LambdaCommand : public QUndoCommand {
public:
    LambdaCommand(AssetDocument* doc, const QString& text, std::function<void()> redoFn,
                  std::function<void()> undoFn)
        : QUndoCommand(text), doc_(doc), redoFn_(std::move(redoFn)), undoFn_(std::move(undoFn)) {}
    void redo() override {
        redoFn_();
        doc_->setDirty(true);
    }
    void undo() override {
        undoFn_();
        doc_->setDirty(true);
    }

private:
    AssetDocument* doc_;
    std::function<void()> redoFn_, undoFn_;
};

/// @brief Adds a child node to `parent` (undo removes it again).
std::unique_ptr<QUndoCommand> makeAddNodeCommand(AssetDocument* doc,
                                                 fuhglgraphics::scene::Node* parent,
                                                 const QString& name);

/// @brief Removes `node` from its parent (undo re-attaches the taken subtree).
std::unique_ptr<QUndoCommand> makeRemoveNodeCommand(AssetDocument* doc,
                                                    fuhglgraphics::scene::Node* node);

/// @brief Sets TRS of `node` (any subset; old values captured for undo).
std::unique_ptr<QUndoCommand> makeSetTrsCommand(AssetDocument* doc, fuhglgraphics::scene::Node* node,
                                                const fuhgl::math::Vec3& translation,
                                                const fuhgl::math::Quat& rotation,
                                                const fuhgl::math::Vec3& scale);

/// @brief P5 gizmo drag: redo applies the drag end TRS, undo restores the start TRS captured
/// at beginDrag (the node was already mutated live during the drag).
std::unique_ptr<QUndoCommand> makeGizmoDragCommand(AssetDocument* doc,
                                                   fuhglgraphics::scene::Node* node,
                                                   const fuhgl::math::Vec3& startT,
                                                   const fuhgl::math::Quat& startR,
                                                   const fuhgl::math::Vec3& startS,
                                                   const fuhgl::math::Vec3& endT,
                                                   const fuhgl::math::Quat& endR,
                                                   const fuhgl::math::Vec3& endS);

/// @brief Enables/disables one render::Effect on `node`.
std::unique_ptr<QUndoCommand> makeSetEffectCommand(AssetDocument* doc,
                                                   fuhglgraphics::scene::Node* node,
                                                   fuhglgraphics::render::Effect effect, bool on);

/// @brief Sets one EffectParams field on `node` by name (value as JSON).
std::unique_ptr<QUndoCommand> makeSetEffectParamCommand(AssetDocument* doc,
                                                        fuhglgraphics::scene::Node* node,
                                                        const QString& name,
                                                        const fuhglgraphics::io::Json& value);

/// @brief Attaches a generated MeshData as a new child node (MCP procedural tool result).
/// Returns nullptr when the host cannot resolve GL meshes.
std::unique_ptr<QUndoCommand> makeAttachMeshCommand(AssetDocument* doc,
                                                    fuhglgraphics::scene::Node* parent,
                                                    const QString& name,
                                                    const fuhgl::render::MeshData& md,
                                                    const fuhgl::math::Vec3& translation);

/// @brief Adds a shape node (box/quad/sphere) via the document's shape resolver.
std::unique_ptr<QUndoCommand> makeAddShapeCommand(AssetDocument* doc,
                                                  fuhglgraphics::scene::Node* parent,
                                                  const QString& shape, const QString& name);

/// @brief Adds a light node (directional|point).
std::unique_ptr<QUndoCommand> makeAddLightCommand(AssetDocument* doc,
                                                  fuhglgraphics::scene::Node* parent,
                                                  const QString& type, const QString& name);

/// @brief Loads a glTF asset under `parent` (needs a current GL context at redo time).
std::unique_ptr<QUndoCommand> makeLoadGltfCommand(AssetDocument* doc,
                                                  fuhglgraphics::scene::Node* parent,
                                                  const QString& path);

/// @brief Adds a G13 water node (WaterComponent) — needs a current GL context at redo time.
std::unique_ptr<QUndoCommand> makeAddWaterCommand(AssetDocument* doc,
                                                  fuhglgraphics::scene::Node* parent,
                                                  const QString& name, int resolution, float size);

} // namespace assetcreator
