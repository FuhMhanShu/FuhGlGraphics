# fuhglgraphics_mcp_server — Qt-free stdio MCP server (JSON-RPC 2.0, line-delimited) that
# serves the library's tool catalog (mcp/tool_catalog.hpp): scene ops, deferred-renderer
# settings (Effect/EffectParams), G13 water params and the procedural generators G24-G31.
# Headless: no GL context needed; attach operations report "no GL mesh resolver in host"
# unless the caller binds resolvers (the Qt asset creator does, in-process).
add_executable(fuhglgraphics_mcp_server main.cpp)
target_link_libraries(fuhglgraphics_mcp_server PRIVATE fuhglgraphics)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fuhglgraphics_mcp_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
