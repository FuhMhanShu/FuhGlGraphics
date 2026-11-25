#include "fuhglgraphics/mcp/jsonrpc.hpp"

#include <cstring>

namespace fuhglgraphics::mcp {

io::Json jsonNumber(double v) {
    io::Json j;
    j.type = io::Json::Type::Number;
    j.number = v;
    return j;
}

io::Json jsonString(const std::string& s) {
    io::Json j;
    j.type = io::Json::Type::String;
    j.str = s;
    return j;
}

io::Json jsonBool(bool b) {
    io::Json j;
    j.type = io::Json::Type::Bool;
    j.boolean = b;
    return j;
}

io::Json jsonArray(std::vector<io::Json> items) {
    io::Json j;
    j.type = io::Json::Type::Array;
    j.arr = std::move(items);
    return j;
}

io::Json jsonObject(std::map<std::string, io::Json> members) {
    io::Json j;
    j.type = io::Json::Type::Object;
    j.obj = std::move(members);
    return j;
}

io::Json jsonNull() { return io::Json{}; }

namespace {

io::Json withEnvelope(const io::Json& id, bool hasId) {
    io::Json j = jsonObject({{"jsonrpc", jsonString("2.0")}});
    if (hasId) j.obj["id"] = id;
    return j;
}

} // namespace

io::Json makeRequest(const io::Json& id, const std::string& method, const io::Json& params) {
    io::Json j = withEnvelope(id, true);
    j.obj["method"] = jsonString(method);
    j.obj["params"] = params;
    return j;
}

io::Json makeNotification(const std::string& method, const io::Json& params) {
    io::Json j = withEnvelope(jsonNull(), false);
    j.obj["method"] = jsonString(method);
    j.obj["params"] = params;
    return j;
}

io::Json makeResultResponse(const io::Json& id, const io::Json& result) {
    io::Json j = withEnvelope(id, true);
    j.obj["result"] = result;
    return j;
}

io::Json makeErrorResponse(const io::Json& id, int code, const std::string& message) {
    io::Json j = withEnvelope(id, true);
    j.obj["error"] = jsonObject({{"code", jsonNumber(code)}, {"message", jsonString(message)}});
    return j;
}

std::string serializeMessage(const io::Json& msg) { return io::writeJson(msg, 0); }

bool parseMessage(const std::string& line, Message& out, std::string* err) {
    out = Message{};
    io::Json j;
    std::string perr;
    if (!io::parseJson(line, j, &perr)) {
        if (err) *err = "JSON parse error: " + perr;
        return false;
    }
    if (!j.isObject() || j.at("jsonrpc").asString() != "2.0") {
        if (err) *err = "not a JSON-RPC 2.0 message (missing/invalid \"jsonrpc\")";
        return false;
    }
    // "id" present-but-null is legal for responses (parse errors answer with id: null)
    const bool hasId = j.contains("id");
    const bool hasMethod = j.contains("method") && j.at("method").isString();
    const bool hasResult = j.contains("result");
    const bool hasError = j.contains("error");
    if (hasMethod) {
        out.kind = hasId ? Message::Kind::Request : Message::Kind::Notification;
        if (hasId) out.id = j.at("id");
        out.method = j.at("method").asString();
        out.params = j.contains("params") ? j.at("params") : jsonNull();
        if (hasResult || hasError) {
            if (err) *err = "message has both method and result/error";
            return false;
        }
        return true;
    }
    if (hasResult || hasError) {
        if (!hasId) {
            if (err) *err = "response without id";
            return false;
        }
        out.kind = Message::Kind::Response;
        out.id = j.at("id");
        if (hasError) {
            const io::Json& e = j.at("error");
            if (!e.isObject()) {
                if (err) *err = "error is not an object";
                return false;
            }
            out.errorCode = e.at("code").asInt();
            out.errorMessage = e.at("message").asString();
        } else {
            out.result = j.at("result");
        }
        return true;
    }
    if (err) *err = "message has neither method nor result/error";
    return false;
}

bool extractLine(std::string& buffer, std::string& line) {
    const std::size_t pos = buffer.find('\n');
    if (pos == std::string::npos) return false;
    line.assign(buffer, 0, pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    buffer.erase(0, pos + 1);
    return true;
}

bool idEquals(const io::Json& a, const io::Json& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case io::Json::Type::Null: return true;
        case io::Json::Type::Bool: return a.boolean == b.boolean;
        case io::Json::Type::Number: return a.number == b.number;
        case io::Json::Type::String: return a.str == b.str;
        default: return false; // array/object ids are not used by this client/server
    }
}

} // namespace fuhglgraphics::mcp
