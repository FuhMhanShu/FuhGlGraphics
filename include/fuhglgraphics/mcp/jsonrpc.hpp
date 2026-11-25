#pragma once

/// @file jsonrpc.hpp
/// @brief G12 MCP layer (Qt-free): JSON-RPC 2.0 message build/parse helpers on top of
/// io::Json (docs/ASSET_CREATOR.md §4/§7). Used by the asset creator's `McpClient`
/// (stdio, line-delimited) and by the in-repo MCP server (mcp/mcp_server.hpp) that
/// exposes the library's tool catalog (mcp/tool_catalog.hpp) — procedural functions
/// (G24–G31) and the deferred renderer settings (render::Effect / scene::EffectParams,
/// incl. the G13 water parameters).
///
/// Conventions: bool + error string, never exceptions; namespace fuhglgraphics::mcp.

#include <fuhglgraphics/io/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace fuhglgraphics::mcp {

// --- JSON-RPC 2.0 standard error codes ----------------------------------------------
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;

/// @brief One parsed JSON-RPC 2.0 message.
struct Message {
    enum class Kind { Invalid, Request, Notification, Response };
    Kind kind = Kind::Invalid;
    io::Json id;              ///< request/response id (Null for notifications)
    std::string method;       ///< request/notification method
    io::Json params;          ///< request/notification params (Null when absent)
    io::Json result;          ///< response result (Null when absent/error)
    int errorCode = 0;        ///< response error code (0 = success)
    std::string errorMessage; ///< response error message
};

// --- small Json builders (the io::Json value type has no constructors) --------------
io::Json jsonNumber(double v);
io::Json jsonString(const std::string& s);
io::Json jsonBool(bool b);
io::Json jsonArray(std::vector<io::Json> items);
io::Json jsonObject(std::map<std::string, io::Json> members);
/// @brief Null value.
io::Json jsonNull();

// --- message builders ----------------------------------------------------------------
/// @brief JSON-RPC request object {"jsonrpc":"2.0","id":id,"method":method,"params":params}.
io::Json makeRequest(const io::Json& id, const std::string& method, const io::Json& params);
/// @brief JSON-RPC notification (no id).
io::Json makeNotification(const std::string& method, const io::Json& params);
/// @brief Successful response {"jsonrpc":"2.0","id":id,"result":result}.
io::Json makeResultResponse(const io::Json& id, const io::Json& result);
/// @brief Error response {"jsonrpc":"2.0","id":id,"error":{"code":code,"message":msg}}.
io::Json makeErrorResponse(const io::Json& id, int code, const std::string& message);

/// @brief Serializes a message to one compact line (stdio framing: one JSON value per line).
std::string serializeMessage(const io::Json& msg);

/// @brief Parses one line into `out`. Returns false + err on malformed JSON or a message
/// that is not a valid JSON-RPC 2.0 request/notification/response.
bool parseMessage(const std::string& line, Message& out, std::string* err = nullptr);

/// @brief Pops the first complete '\n'-terminated line from `buffer` into `line`.
/// Returns false when no full line is available yet (stream framing helper).
bool extractLine(std::string& buffer, std::string& line);

/// @brief Response-id matching: two ids are equal when both Null, or same type + value.
bool idEquals(const io::Json& a, const io::Json& b);

} // namespace fuhglgraphics::mcp
