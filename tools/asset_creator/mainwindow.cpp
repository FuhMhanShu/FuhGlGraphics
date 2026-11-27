#include "mainwindow.hpp"

#include "thumbnailer.hpp"

#include <fuhglgraphics/version.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QTextEdit>
#include <QWindow>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>

namespace assetcreator {

using namespace fuhglgraphics;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("FuhGlGraphics Asset Creator"));
    resize(1600, 900);

    doc_ = new AssetDocument(this);
    viewport_ = new ViewportWidget(doc_, this);
    vpController_ = new ViewportController(viewport_, doc_, this);
    vpController_->install();

    treePanel_ = new SceneTreePanel(doc_, this);
    inspector_ = new InspectorPanel(doc_, this);
    browser_ = new AssetBrowserPanel(doc_, this);
    chatPanel_ = new ChatPanel(this);
    mcpClient_ = new McpClient(this);
    chatController_ = new ChatController(doc_, viewport_, chatPanel_, mcpClient_, this);

    // --- left dock: data structures (tabs: tree / inspector / assets) ---
    auto* leftDock = new QDockWidget(tr("Data structures"), this);
    auto* tabs = new QTabWidget(leftDock);
    tabs->addTab(treePanel_, tr("Scene tree"));
    tabs->addTab(inspector_, tr("Inspector"));
    tabs->addTab(browser_, tr("Assets"));
    leftDock->setWidget(tabs);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    // --- center: viewport ---
    setCentralWidget(viewport_);

    // --- right dock: MCP chat ---
    auto* chatDock = new QDockWidget(tr("MCP chat"), this);
    chatDock->setWidget(chatPanel_);
    addDockWidget(Qt::RightDockWidgetArea, chatDock);

    // --- wiring: commands from every panel/controller go through the undo stack ---
    auto push = [this](std::unique_ptr<QUndoCommand> cmd) { pushCommand(std::move(cmd)); };
    treePanel_->pushCommand = push;
    inspector_->pushCommand = push;
    browser_->pushCommand = push;
    browser_->selectedNodeFn = [this]() { return currentNode_; };
    chatController_->pushCommand = push;
    vpController_->pushCommand = push; // P5 gizmo drags

    connect(treePanel_, &SceneTreePanel::nodeSelected, this, &MainWindow::onSelectionChanged);
    connect(vpController_, &ViewportController::nodePicked, this, &MainWindow::onNodePicked);
    connect(vpController_, &ViewportController::statusMessage, this,
            [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    connect(viewport_, &ViewportWidget::frameRendered, this, &MainWindow::onFrameRendered);
    connect(viewport_, &ViewportWidget::glReadyChanged, this, &MainWindow::onGlReady);
    connect(chatPanel_, &ChatPanel::connectRequested, this, &MainWindow::onConnectMcp);
    connect(doc_, &AssetDocument::changed, this, &MainWindow::refreshTitle);
    connect(doc_, &AssetDocument::documentReset, this, [this]() {
        currentNode_ = nullptr;
        inspector_->setNode(nullptr);
        viewport_->setGizmoTarget(nullptr); // P5
        browser_->setProjectDir(doc_->projectDir());
        refreshThumbnails(); // P5: render missing glTF thumbnails (GL context up by now)
        refreshTitle();
    });

    // --- menus ---
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* aNew = fileMenu->addAction(tr("&New"));
    auto* aOpen = fileMenu->addAction(tr("&Open..."));
    auto* aSave = fileMenu->addAction(tr("&Save"));
    auto* aSaveAs = fileMenu->addAction(tr("Save &As..."));
    fileMenu->addSeparator();
    auto* aExit = fileMenu->addAction(tr("E&xit"));
    connect(aNew, &QAction::triggered, this, &MainWindow::onNew);
    connect(aOpen, &QAction::triggered, this, &MainWindow::onOpen);
    connect(aSave, &QAction::triggered, this, &MainWindow::onSave);
    connect(aSaveAs, &QAction::triggered, this, &MainWindow::onSaveAs);
    connect(aExit, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = doc_->undoStack().createUndoAction(this, tr("&Undo"));
    redoAction_ = doc_->undoStack().createRedoAction(this, tr("&Redo"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);
    connect(&doc_->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::refreshTitle);

    auto* mcpMenu = menuBar()->addMenu(tr("&MCP"));
    auto* aConnect = mcpMenu->addAction(tr("&Connect / Disconnect"));
    auto* aSettings = mcpMenu->addAction(tr("&Settings..."));
    connect(aConnect, &QAction::triggered, this, &MainWindow::onConnectMcp);
    connect(aSettings, &QAction::triggered, this, &MainWindow::onSettings);

    // --- view menu: P5 gizmo mode + animation playback ---
    // shortcuts must not fire while typing into the chat input
    auto textInputFocused = []() {
        QWidget* w = QApplication::focusWidget();
        return qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w);
    };
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* gizmoGroup = new QActionGroup(this);
    gizmoGroup->setExclusive(true);
    auto addGizmoMode = [this, viewMenu, gizmoGroup, textInputFocused](const QString& text, int key,
                                                                      GizmoOverlay::Mode mode) {
        auto* a = viewMenu->addAction(text);
        a->setShortcut(QKeySequence(key));
        a->setCheckable(true);
        a->setChecked(mode == GizmoOverlay::Mode::Translate);
        gizmoGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, a, mode, textInputFocused](bool on) {
            if (textInputFocused()) { // keep the button in sync, ignore the keypress
                a->setChecked(viewport_->gizmo().mode() == mode);
                return;
            }
            if (!on) return;
            viewport_->gizmo().setMode(mode);
            viewport_->requestRender();
        });
    };
    addGizmoMode(tr("Gizmo: &Translate"), Qt::Key_T, GizmoOverlay::Mode::Translate);
    addGizmoMode(tr("Gizmo: &Rotate"), Qt::Key_R, GizmoOverlay::Mode::Rotate);
    addGizmoMode(tr("Gizmo: &Scale"), Qt::Key_S, GizmoOverlay::Mode::Scale);
    viewMenu->addSeparator();
    playAction_ = viewMenu->addAction(tr("&Play animation"));
    playAction_->setShortcut(QKeySequence(Qt::Key_Space));
    playAction_->setCheckable(true);
    connect(playAction_, &QAction::triggered, this, [this, textInputFocused](bool on) {
        if (textInputFocused()) {
            playAction_->setChecked(viewport_->animationPlaying());
            return;
        }
        if (on && viewport_->animationClipCount() == 0) {
            playAction_->setChecked(false);
            statusBar()->showMessage(tr("no animation clips in the scene"), 4000);
            return;
        }
        viewport_->setAnimationPlaying(on);
        statusBar()->showMessage(
            on ? tr("animation playing (%1 clips)").arg(viewport_->animationClipCount())
               : tr("animation stopped"),
            4000);
    });

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aAbout = helpMenu->addAction(tr("&About"));
    connect(aAbout, &QAction::triggered, this, &MainWindow::onAbout);

    // --- settings (persisted; API keys never stored — they come from the env) ---
    QSettings s;
    mcpClient_->setServerCommand(s.value("mcp/serverCommand", "builtin").toString());
    chatController_->setChatToolName(s.value("mcp/chatTool", "chat").toString());

    doc_->newDocument();
    statusBar()->showMessage(tr("ready"));
}

void MainWindow::pushCommand(std::unique_ptr<QUndoCommand> cmd) {
    doc_->undoStack().push(cmd.release()); // push() runs redo()
}

void MainWindow::openProject(const QString& path) {
    if (!viewport_->glReady()) {
        pendingProject_ = path; // loaded once the GL context is up (onGlReady)
        return;
    }
    auto* glw = viewport_->glWidget();
    if (glw->context() && glw->windowHandle()) glw->context()->makeCurrent(glw->windowHandle());
    QString err;
    if (!doc_->open(path, &err)) QMessageBox::warning(this, tr("Open failed"), err);
    refreshTitle();
}

void MainWindow::onGlReady(bool ready) {
    if (!ready) return;
    if (!pendingProject_.isEmpty()) {
        const QString path = pendingProject_;
        pendingProject_.clear();
        openProject(path);
    }
    refreshThumbnails(); // P5
}

void MainWindow::refreshThumbnails() {
    if (!viewport_->glReady()) return;
    auto* glw = viewport_->glWidget();
    if (!glw->context() || !glw->windowHandle()) return;
    glw->context()->makeCurrent(glw->windowHandle()); // thumbnailer needs a current GL context
    const int made = browser_->generateThumbnails();
    browser_->refreshIcons();
    if (made > 0) statusBar()->showMessage(tr("generated %1 thumbnails").arg(made), 5000);
}

void MainWindow::onNew() {
    if (doc_->isDirty()) {
        auto r = QMessageBox::question(this, tr("New document"),
                                       tr("Discard unsaved changes?"));
        if (r != QMessageBox::Yes) return;
    }
    doc_->newDocument();
}

void MainWindow::onOpen() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), doc_->projectDir(), tr("FuhGlGraphics assets (*.fasset)"));
    if (path.isEmpty()) return;
    openProject(path);
}

void MainWindow::onSave() {
    QString err;
    if (doc_->projectPath().isEmpty()) { onSaveAs(); return; }
    if (!doc_->save(&err)) QMessageBox::warning(this, tr("Save failed"), err);
    refreshTitle();
}

void MainWindow::onSaveAs() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save project as"), doc_->projectDir() + "/" + doc_->projectName() + ".fasset",
        tr("FuhGlGraphics assets (*.fasset)"));
    if (path.isEmpty()) return;
    QString err;
    if (!doc_->save(path, &err)) QMessageBox::warning(this, tr("Save failed"), err);
    refreshTitle();
}

void MainWindow::onConnectMcp() {
    if (mcpClient_->isConnected())
        mcpClient_->disconnectFromServer();
    else
        mcpClient_->connectToServer();
}

void MainWindow::onSettings() {
    QSettings s;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("MCP / viewport settings"));
    auto* form = new QFormLayout(&dlg);
    auto* serverEdit = new QLineEdit(s.value("mcp/serverCommand", "builtin").toString(), &dlg);
    serverEdit->setToolTip(tr("external MCP server command, or \"builtin\" for the in-repo "
                              "catalog (procedural + renderer + water tools); API keys come from "
                              "the environment, never from the project file"));
    form->addRow(tr("MCP server command"), serverEdit);
    auto* chatToolEdit = new QLineEdit(s.value("mcp/chatTool", "chat").toString(), &dlg);
    form->addRow(tr("Chat tool name"), chatToolEdit);
    auto* langCombo = new QComboBox(&dlg);
    langCombo->addItems({"en", "de"});
    langCombo->setCurrentText(s.value("ui/language", "en").toString());
    form->addRow(tr("UI language"), langCombo);
    auto* msaaSpin = new QSpinBox(&dlg);
    msaaSpin->setRange(0, 8);
    msaaSpin->setValue(s.value("viewport/msaa", 0).toInt());
    form->addRow(tr("MSAA samples (before first show)"), msaaSpin);
    auto* scaleSpin = new QDoubleSpinBox(&dlg);
    scaleSpin->setRange(0.25, 1.0);
    scaleSpin->setSingleStep(0.05);
    scaleSpin->setValue(s.value("viewport/renderScale", 1.0).toDouble());
    form->addRow(tr("Render scale"), scaleSpin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    s.setValue("mcp/serverCommand", serverEdit->text().trimmed());
    s.setValue("mcp/chatTool", chatToolEdit->text().trimmed());
    s.setValue("ui/language", langCombo->currentText());
    s.setValue("viewport/msaa", msaaSpin->value());
    s.setValue("viewport/renderScale", scaleSpin->value());
    mcpClient_->setServerCommand(serverEdit->text().trimmed());
    chatController_->setChatToolName(chatToolEdit->text().trimmed());
    viewport_->setRenderScale(static_cast<float>(scaleSpin->value()));
    statusBar()->showMessage(tr("settings saved (language applies on restart)"), 4000);
}

void MainWindow::onAbout() {
    QMessageBox::about(this, tr("About"),
                       tr("FuhGlGraphics Asset Creator %1\n\nG12 tool: scene tree + inspector + "
                          "asset browser, G3 deferred-renderer viewport, MCP chat (stdio JSON-RPC "
                          "2.0; builtin catalog exposes the procedural G24-G31 functions, the "
                          "deferred-render settings and the G13 water parameters).")
                           .arg(FUHGLGRAPHICS_VERSION_STRING));
}

void MainWindow::onSelectionChanged(scene::Node* node) {
    currentNode_ = node;
    inspector_->setNode(node);
    viewport_->setGizmoTarget(node); // P5
}

void MainWindow::onNodePicked(scene::Node* node) {
    currentNode_ = node;
    inspector_->setNode(node);
    viewport_->setGizmoTarget(node); // P5
    if (node) {
        const QModelIndex idx = treePanel_->model()->indexOf(node);
        if (idx.isValid()) treePanel_->setCurrentIndex(idx);
    }
}

void MainWindow::onFrameRendered(double fps, double frameMs) {
    statusBar()->showMessage(
        tr("%1 fps, %2 ms — %3").arg(fps, 0, 'f', 1).arg(frameMs, 0, 'f', 2).arg(doc_->projectName()));
}

void MainWindow::refreshTitle() {
    setWindowTitle(tr("FuhGlGraphics Asset Creator — %1%2")
                       .arg(doc_->projectName())
                       .arg(doc_->isDirty() ? "*" : ""));
}

} // namespace assetcreator
