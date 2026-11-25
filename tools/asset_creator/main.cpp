// fuhglgraphics_asset_creator — G12 asset creator Qt tool entry point.
//
// Usage: fuhglgraphics_asset_creator [--project scene.fasset] [--lang en|de] [--version]
//
// Layout (docs/ASSET_CREATOR.md): left dock = scene tree + inspector + asset browser,
// center = G3 DeferredRenderer viewport with editing tools, right = MCP chat.

#include "mainwindow.hpp"

#include <fuhglgraphics/version.hpp>

#include <QApplication>
#include <QTranslator>
#include <QSettings>
#include <QLocale>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("fuhglgraphics_asset_creator");
    QApplication::setOrganizationName("FuhGlGraphics");

    QString projectPath, lang;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::fprintf(stderr, "fuhglgraphics_asset_creator %s\n", FUHGLGRAPHICS_VERSION_STRING);
            return 0;
        } else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            projectPath = QString::fromLocal8Bit(argv[++i]);
        } else if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            lang = QString::fromLocal8Bit(argv[++i]);
        }
    }

    // UI language: English default, German first translation (QTranslator + .ts/.qm).
    if (lang.isEmpty()) lang = QSettings().value("ui/language", QLocale::system().name().left(2)).toString();
    if (lang == "de") {
        QTranslator* tr = new QTranslator(&app);
        const QStringList candidates = {
            QApplication::applicationDirPath() + "/i18n/asset_creator_de.qm",
            QApplication::applicationDirPath() + "/asset_creator_de.qm",
            "asset_creator_de.qm",
        };
        for (const QString& c : candidates) {
            if (tr->load(c)) { QApplication::installTranslator(tr); break; }
        }
    }

    assetcreator::MainWindow window;
    if (!projectPath.isEmpty()) window.openProject(projectPath);
    window.show();
    return app.exec();
}
