// fuhglgraphics_mcp_server — standalone stdio MCP server for the FuhGlGraphics library
// (G12 MCP layer, Qt-free). Speaks JSON-RPC 2.0 line-delimited on stdin/stdout:
//   initialize -> notifications/initialized -> tools/list / tools/call
// The served catalog (mcp/tool_catalog.hpp) covers scene editing, the deferred-renderer
// settings (render_set_effect / render_set_param / ...), the G13 water parameters and the
// procedural generators G24-G31 (procedural_noise, procedural_marching_cubes,
// procedural_lsystem, procedural_wfc, procedural_voronoi, procedural_sdf,
// procedural_creature, procedural_worldgen).
//
// Without a GL context the server runs headless: scene/render/water/procedural-data tools
// work fully; "attach" operations answer "attached": false (no GL mesh resolver bound).
//
// Usage: fuhglgraphics_mcp_server            (speak MCP on stdio)
//        fuhglgraphics_mcp_server --version

#include <fuhglgraphics/mcp/mcp_server.hpp>
#include <fuhglgraphics/version.hpp>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::fprintf(stderr, "fuhglgraphics_mcp_server %s\n", FUHGLGRAPHICS_VERSION_STRING);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::fprintf(stderr,
                         "fuhglgraphics_mcp_server — stdio MCP server for FuhGlGraphics (JSON-RPC 2.0, "
                         "line-delimited): initialize, notifications/initialized, ping, tools/list, tools/call\n");
            return 0;
        }
    }
    // Headless host: a plain CPU scene + the root node's effects as the render-settings
    // target. No GL resolvers bound -> attach operations report attached:false.
    fuhglgraphics::scene::Scene scene;
    fuhglgraphics::mcp::ToolHost host;
    host.scene = &scene;
    host.effects = &scene.root().effects;
    return fuhglgraphics::mcp::runServer(host, std::cin, std::cout) ? 0 : 1;
}
