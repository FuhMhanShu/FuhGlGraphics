#pragma once

/// @file mainwindow.hpp
/// @brief G12 asset creator — main window (view, MVC): central ViewportWidget plus the
/// left "Data structures" dock (scene tree + inspector + asset browser — P2/P3) and the
/// right "Chat (MCP)" dock (P4), both placeholders in P1. File menu: new/open/save .fasset.

#include <QMainWindow>

#include <memory>

class AssetDocument;
class ViewportWidget;
class QAction;
class QDockWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& projectPath = {}, QWidget* parent = nullptr);

private slots:
    void onNew();
    void onOpen();
    void onSave();
    void onSaveAs();

private:
    bool openProject(const QString& path);
    void refreshTitle();

    std::unique_ptr<AssetDocument> doc_;
    ViewportWidget* viewport_ = nullptr;
    QDockWidget* dataDock_ = nullptr; ///< left:  scene tree + inspector + asset browser (P2/P3)
    QDockWidget* chatDock_ = nullptr; ///< right: MCP chat (P4)
    QAction* saveAction_ = nullptr;
};
