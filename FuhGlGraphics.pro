# FuhGlGraphics — standalone qmake project (static library), alternative to CMake.
# Build:  qmake6 "FUHGLCORE_DIR=/path/to/FuhGLCore" FuhGlGraphics.pro && make
#         -> libfuhglgraphics.a
#
# FuhGLCore is pulled in via fuhglcore.pri (sources compiled into this target).
# Qt6 is optional: FUHGLGRAPHICS_WITH_QT=0 builds the Qt-free headless core.

TEMPLATE = lib
CONFIG += staticlib c++17
TARGET = fuhglgraphics
VERSION = 0.1.0

INCLUDEPATH += $$PWD/include

# NOTE: plain per-directory globs (no "**") — $$files(dir/**/..., true) expands
# to nothing on some Qt6 qmake versions; per-directory globs are deterministic.
HEADERS += \
    $$files($$PWD/include/fuhglgraphics/*.hpp) \
    $$files($$PWD/include/fuhglgraphics/*/*.hpp)
SOURCES += \
    $$files($$PWD/src/*.cpp) \
    $$files($$PWD/src/*/*.cpp)

DEFINES += FUHGLGRAPHICS_VERSION_STRING=\\\"$$VERSION\\\"

# --- FuhGLCore dependency (fuhglcore.pri errors if FUHGLCORE_DIR is unset) --------
isEmpty(FUHGLCORE_DIR) {
    FUHGLCORE_DIR = $$(FUHGLCORE_DIR)
}
isEmpty(FUHGLCORE_DIR) {
    error("FuhGlGraphics.pro: set FUHGLCORE_DIR to a FuhGLCore checkout (qmake var or environment), e.g. qmake6 FUHGLCORE_DIR=/path/to/FuhGLCore FuhGlGraphics.pro")
}
include($$FUHGLCORE_DIR/fuhglcore.pri)

# --- Qt6 (editors/tools only; default ON, 0 = Qt-free headless core) --------------
isEmpty(FUHGLGRAPHICS_WITH_QT): FUHGLGRAPHICS_WITH_QT = 1
equals(FUHGLGRAPHICS_WITH_QT, 1) {
    QT += widgets openglwidgets
    CONFIG += automoc
    DEFINES += FUHGLGRAPHICS_WITH_QT=1
    # Qt code lives only in editor/ (the core in src/ stays Qt-free); the header is picked
    # up by the include/fuhglgraphics/*/*.hpp glob above and moc'd via automoc.
    SOURCES += $$files($$PWD/editor/*.cpp)
}

gcc|clang {
    QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
}
