#pragma once

/// @file mainwindow.hpp
/// @brief G12 asset creator — MainWindow (view): QMainWindow with three docks
/// (data structures: scene tree + inspector + asset browser | viewport | MCP chat),
/// menus (File/Edit/MCP/Help). Owns the AssetDocument (model), ViewportWidget (view),
/// ViewportController + ChatController (controllers) and wires selection/commands.

#include "asset_browser_panel.hpp"
#include "asset_document.hpp"
#include "chat_controller.hpp"
#include "chat_panel.hpp"
#include "inspector_panel.hpp"
#include "scene_tree_panel.hpp"
#include "viewport_controller.hpp"
#include "viewport_widget.hpp"
#include "mcp/mcp_client.hpp"

#include <QMainWindow>

class QAction;

namespace assetcreator {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openProject(const QString& path); ///< --project CLI (defers until the GL context is up)

private slots:
    void onNew();
    void onOpen();
    void onSave();
    void onSaveAs();
    void onConnectMcp();
    void onSettings();
    void onAbout();
    void onSelectionChanged(fuhglgraphics::scene::Node* node);
    void onNodePicked(fuhglgraphics::scene::Node* node);
    void onFrameRendered(double fps, double frameMs);
    void onGlReady(bool ready);
    void refreshTitle();

private:
    void pushCommand(std::unique_ptr<QUndoCommand> cmd);

    AssetDocument* doc_ = nullptr;
    ViewportWidget* viewport_ = nullptr;
    ViewportController* vpController_ = nullptr;
    SceneTreePanel* treePanel_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    AssetBrowserPanel* browser_ = nullptr;
    ChatPanel* chatPanel_ = nullptr;
    McpClient* mcpClient_ = nullptr;
    ChatController* chatController_ = nullptr;
    fuhglgraphics::scene::Node* currentNode_ = nullptr;
    QString pendingProject_;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
};

} // namespace assetcreator
