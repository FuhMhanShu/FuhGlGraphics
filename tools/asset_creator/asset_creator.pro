# G12 asset creator — qmake alternative for the Qt6 tool (app target).
#
# Build the library first:
#   qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" ../../FuhGlGraphics.pro && make
# then the tool (run from this directory's build dir, e.g. build/tools/asset_creator):
#   qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" asset_creator.pro && make
# CMake is the primary build (FUHGLGRAPHICS_BUILD_TOOLS=ON with FUHGLGRAPHICS_WITH_QT=ON).

QT += widgets openglwidgets
CONFIG += c++17 automoc
TEMPLATE = app
TARGET = fuhglgraphics_asset_creator

INCLUDEPATH += $$PWD/../../include

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    viewport_widget.cpp \
    asset_document.cpp

HEADERS += \
    mainwindow.hpp \
    viewport_widget.hpp \
    asset_document.hpp

# FuhGLCore headers (fuhgl/*.hpp) are needed because the public fuhglgraphics
# headers include them; the compiled FuhGLCore code is inside libfuhglgraphics.
isEmpty(FUHGLCORE_DIR) {
    FUHGLCORE_DIR = $$(FUHGLCORE_DIR)
}
isEmpty(FUHGLCORE_DIR) {
    error("asset_creator.pro: set FUHGLCORE_DIR to a FuhGLCore checkout (qmake var or environment)")
}
INCLUDEPATH += $$FUHGLCORE_DIR/include

# the static library built from ../../FuhGlGraphics.pro (adjust if your build dir differs)
LIBS += -L$$OUT_PWD/../.. -lfuhglgraphics
