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
target_link_libraries(fuhglgraphics_asset_creator PRIVATE
    fuhglgraphics
    Qt6::Widgets
    Qt6::OpenGLWidgets
)
