// G12 asset creator — Qt6 tool entry point (see docs/ASSET_CREATOR.md).
//
// Usage: fuhglgraphics_asset_creator [--project my.fasset] [--version]

#include "mainwindow.hpp"

#include <fuhglgraphics/version.hpp>

#include <QApplication>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString projectPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc)
            projectPath = QString::fromLocal8Bit(argv[++i]);
        else if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("fuhglgraphics_asset_creator 0.1.0 (FuhGlGraphics %s)\n",
                        fuhglgraphics::versionString());
            return 0;
        } else {
            std::fprintf(stderr, "usage: fuhglgraphics_asset_creator [--project x.fasset] [--version]\n");
            return 2;
        }
    }

    MainWindow window(projectPath);
    window.show();
    return app.exec();
}
