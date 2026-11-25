#include "fuhglgraphics/io/asset_cache.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fuhglgraphics::io {

namespace fs = std::filesystem;

std::string AssetCache::hashBytes(const unsigned char* data, std::size_t n) {
    std::uint64_t h = 14695981039346656037ull; // FNV-1a 64 offset basis
    for (std::size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 1099511628211ull; // FNV-1a 64 prime
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool AssetCache::open(const std::string& cacheDir, std::string* err) {
    std::error_code ec;
    fs::create_directories(cacheDir, ec);
    if (ec) {
        if (err) *err = "asset cache: cannot create " + cacheDir + ": " + ec.message();
        return false;
    }
    dir_ = cacheDir;
    return true;
}

std::string AssetCache::hashOfFile(const std::string& path, std::string* err) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "asset cache: cannot read " + path;
        return "";
    }
    std::uint64_t h = 14695981039346656037ull;
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        const std::streamsize got = f.gcount();
        const unsigned char* p = reinterpret_cast<const unsigned char*>(buf);
        for (std::streamsize i = 0; i < got; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    }
    if (f.bad()) {
        if (err) *err = "asset cache: read error on " + path;
        return "";
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
    return out;
}

std::string AssetCache::pathFor(const std::string& hash, const std::string& extension) const {
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    return dir_ + "/" + hash + ext;
}

bool AssetCache::put(const std::string& sourcePath, std::string& cachedPathOut, std::string* err) {
    if (!isOpen()) {
        if (err) *err = "asset cache: not open";
        return false;
    }
    std::string hash = hashOfFile(sourcePath, err);
    if (hash.empty()) return false;
    std::string ext = fs::path(sourcePath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    const std::string dest = pathFor(hash, ext);
    std::error_code ec;
    if (!fs::exists(dest, ec)) {
        fs::copy_file(sourcePath, dest, fs::copy_options::none, ec);
        if (ec) {
            if (err) *err = "asset cache: cannot copy to " + dest + ": " + ec.message();
            return false;
        }
    }
    cachedPathOut = dest;
    return true;
}

bool AssetCache::get(const std::string& hash, const std::string& extension, std::string& outPath,
                     std::string* err) const {
    if (!isOpen()) {
        if (err) *err = "asset cache: not open";
        return false;
    }
    const std::string p = pathFor(hash, extension);
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    outPath = p;
    return true;
}

} // namespace fuhglgraphics::io
