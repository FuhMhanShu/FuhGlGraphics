# qmake alternative for the G12 asset creator tool (CMake is the primary build):
#   qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" ../../FuhGlGraphics.pro   # library first
#   qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" "FUHGLGRAPHICS_DIR=../.." asset_creator.pro && make
TEMPLATE = app
CONFIG += c++17
TARGET = fuhglgraphics_asset_creator
QT += widgets openglwidgets

isEmpty(FUHGLGRAPHICS_DIR): FUHGLGRAPHICS_DIR = $$PWD/../..
INCLUDEPATH += $$FUHGLGRAPHICS_DIR/include
LIBS += -L$$OUT_PWD/../.. -lfuhglgraphics

isEmpty(FUHGLCORE_DIR) {
    FUHGLCORE_DIR = $$(FUHGLCORE_DIR)
}
isEmpty(FUHGLCORE_DIR) {
    error("asset_creator.pro: set FUHGLCORE_DIR to a FuhGLCore checkout (for its include paths)")
}
INCLUDEPATH += $$FUHGLCORE_DIR/include

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    asset_document.cpp \
    viewport_widget.cpp \
    viewport_controller.cpp \
    commands.cpp \
    gizmo.cpp \
    thumbnailer.cpp \
    scene_tree_model.cpp \
    scene_tree_panel.cpp \
    inspector_panel.cpp \
    asset_browser_panel.cpp \
    chat_panel.cpp \
    chat_controller.cpp \
    mcp/mcp_client.cpp

HEADERS += \
    mainwindow.hpp \
    asset_document.hpp \
    viewport_widget.hpp \
    viewport_controller.hpp \
    commands.hpp \
    gizmo.hpp \
    thumbnailer.hpp \
    scene_tree_model.hpp \
    scene_tree_panel.hpp \
    inspector_panel.hpp \
    asset_browser_panel.hpp \
    chat_panel.hpp \
    chat_controller.hpp \
    mcp/mcp_client.hpp

TRANSLATIONS += i18n/asset_creator_de.ts
