#pragma once

/// @file asset_cache.hpp
/// @brief G11 engine infrastructure (part 1): on-disk asset cache keyed by content hash.
/// Imported/copied assets are stored as <cacheDir>/<fnv1a64-hex><ext> so a re-import of
/// identical content hits the cache instead of re-reading/re-decoding the source.
/// Pure std::filesystem + std::fstream — Qt-free, unit-testable.
///
/// Conventions: bool + error string, never exceptions; namespace fuhglgraphics::io.

#include <cstddef>
#include <cstdint>
#include <string>

namespace fuhglgraphics::io {

/// @brief On-disk asset cache keyed by the FNV-1a 64-bit content hash.
class AssetCache {
public:
    AssetCache() = default;

    /// @brief Opens (and creates when missing) the cache directory. Returns false + err.
    bool open(const std::string& cacheDir, std::string* err = nullptr);
    bool isOpen() const { return !dir_.empty(); }
    const std::string& dir() const { return dir_; }

    /// @brief FNV-1a 64-bit hash of `n` bytes, as 16-char lowercase hex.
    static std::string hashBytes(const unsigned char* data, std::size_t n);
    /// @brief Content hash of a file (hex). Returns "" + err on I/O failure.
    std::string hashOfFile(const std::string& path, std::string* err = nullptr) const;

    /// @brief Cache path for `hash` + original extension (e.g. "ab12...34.png").
    std::string pathFor(const std::string& hash, const std::string& extension) const;

    /// @brief Copies `sourcePath` into the cache when not already present (content-hash
    /// keyed). Fills `cachedPathOut` with the cache path either way. Returns false + err
    /// on I/O failure.
    bool put(const std::string& sourcePath, std::string& cachedPathOut, std::string* err = nullptr);

    /// @brief True when a cache entry for `hash` + `extension` exists; `outPath` gets its path.
    bool get(const std::string& hash, const std::string& extension, std::string& outPath,
             std::string* err = nullptr) const;

private:
    std::string dir_;
};

} // namespace fuhglgraphics::io
