#include "mainwindow.hpp"

#include "asset_document.hpp"
#include "viewport_widget.hpp"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>

MainWindow::~MainWindow() = default;

MainWindow::MainWindow(const QString& projectPath, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Asset Creator"));
    resize(1600, 1000);

    doc_ = std::make_unique<AssetDocument>();
    doc_->newDocument();

    viewport_ = new ViewportWidget(this);
    viewport_->setDocument(doc_.get());
    setCentralWidget(viewport_);

    // left dock: data structures (scene tree + inspector + asset browser land in P2/P3)
    dataDock_ = new QDockWidget(QStringLiteral("Data structures"), this);
    dataDock_->setObjectName(QStringLiteral("dataDock"));
    dataDock_->setWidget(new QLabel(QStringLiteral("Scene tree / inspector / asset browser — P2/P3"), dataDock_));
    addDockWidget(Qt::LeftDockWidgetArea, dataDock_);

    // right dock: MCP chat (P4)
    chatDock_ = new QDockWidget(QStringLiteral("Chat (MCP)"), this);
    chatDock_->setObjectName(QStringLiteral("chatDock"));
    chatDock_->setWidget(new QLabel(QStringLiteral("LLM chat over MCP — P4"), chatDock_));
    addDockWidget(Qt::RightDockWidgetArea, chatDock_);

    // File menu
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* newAction = fileMenu->addAction(QStringLiteral("&New"));
    QAction* openAction = fileMenu->addAction(QStringLiteral("&Open..."));
    saveAction_ = fileMenu->addAction(QStringLiteral("&Save"));
    QAction* saveAsAction = fileMenu->addAction(QStringLiteral("Save &As..."));
    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction_->setShortcut(QKeySequence::Save);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNew);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::onSave);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    if (!projectPath.isEmpty())
        openProject(projectPath);

    refreshTitle();
}

void MainWindow::refreshTitle() {
    QString title = QStringLiteral("Asset Creator — %1").arg(doc_ ? doc_->displayName() : QStringLiteral("untitled"));
    if (doc_ && doc_->dirty())
        title += QStringLiteral(" *");
    setWindowTitle(title);
}

void MainWindow::onNew() {
    doc_ = std::make_unique<AssetDocument>();
    doc_->newDocument();
    viewport_->setDocument(doc_.get());
    refreshTitle();
}

void MainWindow::onOpen() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open asset project"), {},
        QStringLiteral("FuhGlGraphics asset (*.fasset);;All files (*)"));
    if (!path.isEmpty())
        openProject(path);
}

bool MainWindow::openProject(const QString& path) {
    auto fresh = std::make_unique<AssetDocument>();
    std::string err;
    if (!fresh->open(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Open failed"),
                             QString::fromStdString(err));
        return false;
    }
    doc_ = std::move(fresh);
    viewport_->setDocument(doc_.get());
    refreshTitle();
    return true;
}

void MainWindow::onSave() {
    if (!doc_->hasPath()) {
        onSaveAs();
        return;
    }
    std::string err;
    if (!doc_->save(&err))
        QMessageBox::warning(this, QStringLiteral("Save failed"), QString::fromStdString(err));
    refreshTitle();
}

void MainWindow::onSaveAs() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save asset project"), doc_->displayName() + QStringLiteral(".fasset"),
        QStringLiteral("FuhGlGraphics asset (*.fasset);;All files (*)"));
    if (path.isEmpty())
        return;
    std::string err;
    if (!doc_->save(path, &err))
        QMessageBox::warning(this, QStringLiteral("Save failed"), QString::fromStdString(err));
    refreshTitle();
}
