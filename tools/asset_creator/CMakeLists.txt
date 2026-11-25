# fuhglgraphics_asset_creator — the G12 asset creator Qt tool (docs/ASSET_CREATOR.md):
# data-structures dock (scene tree + inspector + asset browser), G3-renderer viewport with
# editing tools, and an LLM chat over MCP (external stdio server command or the built-in
# fuhglgraphics catalog: procedural G24-G31 + deferred-render settings + G13 water).
set(CMAKE_AUTOMOC ON)

set(ASSET_CREATOR_SOURCES
    main.cpp
    mainwindow.cpp
    asset_document.cpp
    viewport_widget.cpp
    viewport_controller.cpp
    commands.cpp
    scene_tree_model.cpp
    scene_tree_panel.cpp
    inspector_panel.cpp
    asset_browser_panel.cpp
    chat_panel.cpp
    chat_controller.cpp
    mcp/mcp_client.cpp
)
set(ASSET_CREATOR_HEADERS
    mainwindow.hpp
    asset_document.hpp
    viewport_widget.hpp
    viewport_controller.hpp
    commands.hpp
    scene_tree_model.hpp
    scene_tree_panel.hpp
    inspector_panel.hpp
    asset_browser_panel.hpp
    chat_panel.hpp
    chat_controller.hpp
    mcp/mcp_client.hpp
)

add_executable(fuhglgraphics_asset_creator ${ASSET_CREATOR_SOURCES} ${ASSET_CREATOR_HEADERS})
target_link_libraries(fuhglgraphics_asset_creator PRIVATE fuhglgraphics Qt6::Widgets Qt6::OpenGLWidgets)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fuhglgraphics_asset_creator PRIVATE -Wall -Wextra -Wpedantic)
endif()

# UI translations (English default source strings; German first translation). Compiled to
# .qm next to the executable when Qt6 LinguistTools is available; the app also loads them
# from ./i18n at runtime. Missing .qm simply falls back to English.
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    qt_add_translations(fuhglgraphics_asset_creator
        TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/asset_creator_de.ts
        QM_FILES_OUTPUT_VARIABLE ASSET_CREATOR_QM_FILES)
endif()
