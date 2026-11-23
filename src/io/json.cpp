#include "fuhglgraphics/io/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fuhglgraphics::io {

const Json& Json::at(const std::string& key) const {
    static const Json kNull{};
    const Json* v = find(key);
    return v ? *v : kNull;
}

const Json& Json::operator[](std::size_t i) const {
    static const Json kNull{};
    return (type == Type::Array && i < arr.size()) ? arr[i] : kNull;
}

namespace {

struct Parser {
    const char* p = nullptr;
    const char* end = nullptr;
    std::string* err = nullptr;

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool fail(const std::string& msg) {
        if (err) *err = msg;
        return false;
    }
    bool parseValue(Json& out) {
        skipWs();
        if (p >= end) return fail("unexpected end of input");
        const char c = *p;
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            out.type = Json::Type::String;
            return parseString(out.str);
        }
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') return parseNull(out);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail(std::string("unexpected character '") + c + "'");
    }
    bool parseObject(Json& out) {
        out.type = Json::Type::Object;
        ++p; // '{'
        skipWs();
        if (p < end && *p == '}') {
            ++p;
            return true;
        }
        while (true) {
            skipWs();
            if (p >= end || *p != '"') return fail("expected object key string");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (p >= end || *p != ':') return fail("expected ':'");
            ++p;
            if (!parseValue(out.obj[key])) return false;
            skipWs();
            if (p >= end) return fail("unexpected end of input in object");
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == '}') {
                ++p;
                return true;
            }
            return fail("expected ',' or '}' in object");
        }
    }
    bool parseArray(Json& out) {
        out.type = Json::Type::Array;
        ++p; // '['
        skipWs();
        if (p < end && *p == ']') {
            ++p;
            return true;
        }
        while (true) {
            Json v;
            if (!parseValue(v)) return false;
            out.arr.push_back(std::move(v));
            skipWs();
            if (p >= end) return fail("unexpected end of input in array");
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == ']') {
                ++p;
                return true;
            }
            return fail("expected ',' or ']' in array");
        }
    }
    bool parseString(std::string& out) {
        ++p; // '"'
        out.clear();
        while (p < end) {
            const char c = *p++;
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= end) return fail("unexpected end in string escape");
                const char e = *p++;
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (end - p < 4) return fail("short \\u escape");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = *p++;
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else return fail("invalid hex digit in \\u escape");
                        }
                        // encode code point as UTF-8 (surrogate pairs not combined — fine for glTF)
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: return fail("invalid string escape");
                }
            } else {
                out += c;
            }
        }
        return fail("unterminated string");
    }
    bool parseNumber(Json& out) {
        const char* start = p;
        if (p < end && *p == '-') ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
            ++p;
        const std::string s(start, p);
        char* endp = nullptr;
        const double v = std::strtod(s.c_str(), &endp);
        if (endp != s.c_str() + s.size()) return fail("invalid number '" + s + "'");
        out.type = Json::Type::Number;
        out.number = v;
        return true;
    }
    bool parseBool(Json& out) {
        if (end - p >= 4 && std::string(p, p + 4) == "true") {
            p += 4;
            out.type = Json::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (end - p >= 5 && std::string(p, p + 5) == "false") {
            p += 5;
            out.type = Json::Type::Bool;
            out.boolean = false;
            return true;
        }
        return fail("invalid literal");
    }
    bool parseNull(Json& out) {
        if (end - p >= 4 && std::string(p, p + 4) == "null") {
            p += 4;
            out.type = Json::Type::Null;
            return true;
        }
        return fail("invalid literal");
    }
};

} // namespace

bool parseJson(const std::string& text, Json& out, std::string* err) {
    Parser parser;
    parser.p = text.data();
    parser.end = text.data() + text.size();
    parser.err = err;
    if (!parser.parseValue(out)) return false;
    parser.skipWs();
    if (parser.p != parser.end) {
        if (err) *err = "trailing characters after JSON value";
        return false;
    }
    return true;
}

bool base64Decode(const std::string& in, std::vector<unsigned char>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    out.reserve(in.size() / 4 * 3);
    int acc = 0, bits = 0;
    std::size_t pad = 0;
    for (char c : in) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '=') {
            ++pad;
            continue;
        }
        if (pad > 0) return false; // data after '='
        const int v = val(c);
        if (v < 0) return false;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((acc >> bits) & 0xFF));
        }
    }
    if (pad > 2) return false;
    return true;
}

namespace {

void writeEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void writeNumber(std::string& out, double v) {
    if (std::isnan(v) || std::isinf(v)) {
        out += "null"; // JSON has no NaN/Inf
        return;
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    // trim to the shortest representation that round-trips
    double back = std::strtod(buf, nullptr);
    if (back == v) {
        out += buf;
        return;
    }
    for (int prec = 1; prec < 17; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    out += buf;
}

void writeValue(std::string& out, const Json& v, int indent, int level) {
    const std::string pad(static_cast<std::size_t>(indent > 0 ? indent * (level + 1) : 0), ' ');
    const std::string padEnd(static_cast<std::size_t>(indent > 0 ? indent * level : 0), ' ');
    const char* nl = indent > 0 ? "\n" : "";
    switch (v.type) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.boolean ? "true" : "false"; break;
        case Json::Type::Number: writeNumber(out, v.number); break;
        case Json::Type::String: writeEscaped(out, v.str); break;
        case Json::Type::Array: {
            out.push_back('[');
            if (!v.arr.empty()) {
                for (std::size_t i = 0; i < v.arr.size(); ++i) {
                    if (i) out.push_back(',');
                    if (indent > 0) { out += nl; out += pad; }
                    writeValue(out, v.arr[i], indent, level + 1);
                }
                if (indent > 0) { out += nl; out += padEnd; }
            }
            out.push_back(']');
            break;
        }
        case Json::Type::Object: {
            out.push_back('{');
            if (!v.obj.empty()) {
                std::size_t i = 0;
                for (const auto& kv : v.obj) {
                    if (i++) out.push_back(',');
                    if (indent > 0) { out += nl; out += pad; }
                    writeEscaped(out, kv.first);
                    out.push_back(':');
                    if (indent > 0) out.push_back(' ');
                    writeValue(out, kv.second, indent, level + 1);
                }
                if (indent > 0) { out += nl; out += padEnd; }
            }
            out.push_back('}');
            break;
        }
    }
}

} // namespace

std::string writeJson(const Json& v, int indent) {
    std::string out;
    writeValue(out, v, indent, 0);
    return out;
}

bool writeJsonFile(const std::string& path, const Json& v, std::string* err) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err) *err = "cannot open " + path + " for writing";
        return false;
    }
    const std::string s = writeJson(v, 2) + "\n";
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    if (!f) {
        if (err) *err = "write failed: " + path;
        return false;
    }
    return true;
}

} // namespace fuhglgraphics::io
