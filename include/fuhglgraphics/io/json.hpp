#pragma once

/// @file json.hpp
/// @brief Minimal JSON parser — own implementation, no external dependency
/// (REQUIREMENTS G4 / N1: no nlohmann). Supports the full JSON grammar
/// (objects, arrays, strings with escapes incl. \uXXXX, numbers, true/false/null).
/// Errors follow the repo convention: bool + error string, never exceptions.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fuhglgraphics::io {

/// @brief One JSON value (null / bool / number / string / array / object).
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;                    ///< string value (UTF-8)
    std::vector<Json> arr;              ///< array elements
    std::map<std::string, Json> obj;    ///< object members

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    bool contains(const std::string& key) const {
        return type == Type::Object && obj.find(key) != obj.end();
    }
    /// @brief Member lookup (nullptr when absent or not an object) — safe chaining.
    const Json* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
    /// @brief Member access, returns a shared Null value when absent (safe chaining).
    const Json& at(const std::string& key) const;
    /// @brief Array access, returns a shared Null value when out of range.
    const Json& operator[](std::size_t i) const;

    std::string asString(const std::string& def = {}) const {
        return type == Type::String ? str : def;
    }
    double asNumber(double def = 0.0) const { return type == Type::Number ? number : def; }
    int asInt(int def = 0) const { return type == Type::Number ? static_cast<int>(number) : def; }
    bool asBool(bool def = false) const { return type == Type::Bool ? boolean : def; }

    std::size_t size() const {
        return type == Type::Array ? arr.size() : (type == Type::Object ? obj.size() : 0);
    }
};

/// @brief Parses `text` into `out`. Returns false and fills `err` on syntax errors.
bool parseJson(const std::string& text, Json& out, std::string* err = nullptr);

/// @brief Base64 decode (glTF `data:` URIs). Whitespace is ignored; returns false on
/// invalid characters or bad padding.
bool base64Decode(const std::string& in, std::vector<unsigned char>& out);

} // namespace fuhglgraphics::io
