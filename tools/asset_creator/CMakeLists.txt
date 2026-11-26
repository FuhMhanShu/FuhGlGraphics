# G12 asset creator — Qt6 desktop tool (design: docs/ASSET_CREATOR.md).
# The library core stays Qt-free; this target is only built with
# FUHGLGRAPHICS_WITH_QT=ON + FUHGLGRAPHICS_BUILD_TOOLS=ON.
add_executable(fuhglgraphics_asset_creator
    main.cpp
    mainwindow.hpp
    mainwindow.cpp
    viewport_widget.hpp
    viewport_widget.cpp
    asset_document.hpp
    asset_document.cpp
)
set_target_properties(fuhglgraphics_asset_creator PROPERTIES AUTOMOC ON)
# QT_NO_KEYWORDS: the library header particles/particle_system.hpp declares emit();
# the Qt keyword macro `emit` would break it when Qt widget headers are included.
target_compile_definitions(fuhglgraphics_asset_creator PRIVATE QT_NO_KEYWORDS=1)
target_link_libraries(fuhglgraphics_asset_creator PRIVATE
    fuhglgraphics
    Qt6::Widgets
    Qt6::OpenGLWidgets
)
