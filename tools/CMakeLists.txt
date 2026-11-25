# tools/ — G12 executables (see docs/ASSET_CREATOR.md):
# - fuhglgraphics_mcp_server: Qt-free stdio MCP server exposing the library tool catalog
#   (procedural G24-G31, deferred-render settings, G13 water, scene ops) — built whenever
#   FUHGLGRAPHICS_BUILD_TOOLS=ON, no Qt needed.
# - fuhglgraphics_asset_creator: the Qt6 asset creator tool — only with FUHGLGRAPHICS_WITH_QT=ON.

add_subdirectory(mcp_server)

if(FUHGLGRAPHICS_WITH_QT)
    add_subdirectory(asset_creator)
endif()
