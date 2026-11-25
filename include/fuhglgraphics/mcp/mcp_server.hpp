#pragma once

/// @file mcp_server.hpp
/// @brief G12 MCP server (Qt-free, stdio): serves the FuhGlGraphics tool catalog
/// (mcp/tool_catalog.hpp) over JSON-RPC 2.0, one message per line on stdin/stdout.
/// Handled methods: `initialize`, `notifications/initialized`, `ping`, `tools/list`,
/// `tools/call`. This is the in-repo MCP server — the asset creator's `McpClient` can
/// either spawn an external server command (configurable, see docs/ASSET_CREATOR.md) or
/// talk to this catalog in-process; the standalone executable `fuhglgraphics_mcp_server`
/// (tools/mcp_server) exposes the same catalog on stdio for any MCP client.
///
/// Conventions: bool + error string, never exceptions; namespace fuhglgraphics::mcp.

#include "tool_catalog.hpp"

#include <iosfwd>
#include <string>

namespace fuhglgraphics::mcp {

/// @brief MCP protocol version this server speaks.
inline constexpr const char* kProtocolVersion = "2024-11-05";

/// @brief Handles one incoming line (request or notification). Returns the response line
/// to write back, or an empty string for notifications. Malformed lines get a parse-error
/// response with a Null id.
std::string handleLine(const std::string& line, ToolHost& host);

/// @brief Serves `in` line by line, writing responses to `out`, until EOF.
/// Returns false when a response could not be serialized (I/O error).
bool runServer(ToolHost& host, std::istream& in, std::ostream& out);

} // namespace fuhglgraphics::mcp
