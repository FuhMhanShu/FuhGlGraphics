#include "fuhglgraphics/mcp/mcp_server.hpp"

#include "fuhglgraphics/mcp/jsonrpc.hpp"
#include "fuhglgraphics/version.hpp"

#include <istream>
#include <ostream>

namespace fuhglgraphics::mcp {

std::string handleLine(const std::string& line, ToolHost& host) {
    Message msg;
    std::string err;
    if (!parseMessage(line, msg, &err)) {
        return serializeMessage(makeErrorResponse(jsonNull(), kParseError, err));
    }
    switch (msg.kind) {
        case Message::Kind::Notification:
            return {}; // notifications/initialized and friends: no response
        case Message::Kind::Response:
            return {}; // this server sends no server->client requests (yet)
        case Message::Kind::Invalid:
            return serializeMessage(makeErrorResponse(jsonNull(), kInvalidRequest, "invalid message"));
        case Message::Kind::Request:
            break;
    }
    const std::string& method = msg.method;
    if (method == "initialize") {
        io::Json result = jsonObject({
            {"protocolVersion", jsonString(kProtocolVersion)},
            {"capabilities", jsonObject({{"tools", jsonObject({{"listChanged", jsonBool(false)}})}})},
            {"serverInfo", jsonObject({{"name", jsonString("fuhglgraphics")},
                                       {"version", jsonString(FUHGLGRAPHICS_VERSION_STRING)}})}});
        return serializeMessage(makeResultResponse(msg.id, result));
    }
    if (method == "ping") {
        return serializeMessage(makeResultResponse(msg.id, jsonObject({})));
    }
    if (method == "tools/list") {
        return serializeMessage(makeResultResponse(msg.id, toolsListJson()));
    }
    if (method == "tools/call") {
        const io::Json& params = msg.params;
        const std::string name = params.at("name").asString();
        const io::Json& arguments = params.at("arguments");
        std::string callErr;
        io::Json payloadJson = callTool(name, arguments, host, &callErr);
        if (payloadJson.isNull() && !callErr.empty()) {
            return serializeMessage(makeResultResponse(
                msg.id, makeToolResult(jsonObject({{"error", jsonString(callErr)}}), true)));
        }
        return serializeMessage(makeResultResponse(msg.id, makeToolResult(payloadJson)));
    }
    return serializeMessage(makeErrorResponse(msg.id, kMethodNotFound, "method not found: " + method));
}

bool runServer(ToolHost& host, std::istream& in, std::ostream& out) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::string response = handleLine(line, host);
        if (!response.empty()) {
            out << response << '\n';
            out.flush();
            if (!out) return false;
        }
    }
    return true;
}

} // namespace fuhglgraphics::mcp
