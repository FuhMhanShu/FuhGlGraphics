#pragma once

// Minimal dependency-free PNG writer (8-bit RGBA, uncompressed/"stored" DEFLATE
// blocks). Enough for doc/CI screenshots without libpng/zlib.

#include <cstdint>
#include <cstdio>
#include <vector>

namespace fuhglgraphics::screenshot {

namespace detail {

inline uint32_t crc32(const unsigned char* data, size_t len) {
    static uint32_t table[256];
    static bool filled = false;
    if (!filled) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[n] = c;
        }
        filled = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline uint32_t adler32(const unsigned char* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

inline void putU32(std::vector<unsigned char>& out, uint32_t v) {
    out.push_back(static_cast<unsigned char>(v >> 24));
    out.push_back(static_cast<unsigned char>(v >> 16));
    out.push_back(static_cast<unsigned char>(v >> 8));
    out.push_back(static_cast<unsigned char>(v));
}

inline void putChunk(std::vector<unsigned char>& out, const char type[4], const std::vector<unsigned char>& data) {
    putU32(out, static_cast<uint32_t>(data.size()));
    const size_t typePos = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    putU32(out, crc32(out.data() + typePos, 4 + data.size()));
}

} // namespace detail

/// @brief Writes an 8-bit RGBA image as PNG (bottom-up `rgba` buffer, row 0 = bottom).
/// Returns false on I/O error.
inline bool writePng(const char* path, const unsigned char* rgba, int width, int height) {
    if (!path || !rgba || width <= 0 || height <= 0) return false;

    // Raw scanlines (top row first), filter byte 0 + RGBA per pixel.
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + 4 * width));
    for (int y = height - 1; y >= 0; --y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + static_cast<size_t>(y) * 4 * width,
                   rgba + static_cast<size_t>(y + 1) * 4 * width);
    }

    // zlib stream with stored (uncompressed) deflate blocks.
    std::vector<unsigned char> z;
    z.push_back(0x78); // zlib header: deflate, 32K window
    z.push_back(0x01); // no compression, check bits
    size_t pos = 0;
    do {
        const size_t block = raw.size() - pos > 65535 ? 65535 : raw.size() - pos;
        const bool final = (pos + block == raw.size());
        z.push_back(final ? 0x01 : 0x00);
        const uint16_t len = static_cast<uint16_t>(block);
        z.push_back(static_cast<unsigned char>(len & 0xFF));
        z.push_back(static_cast<unsigned char>(len >> 8));
        z.push_back(static_cast<unsigned char>(~len & 0xFF));
        z.push_back(static_cast<unsigned char>((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + block);
        pos += block;
    } while (pos < raw.size());
    detail::putU32(z, detail::adler32(raw.data(), raw.size()));

    std::vector<unsigned char> png;
    const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    png.insert(png.end(), sig, sig + 8);

    std::vector<unsigned char> ihdr;
    detail::putU32(ihdr, static_cast<uint32_t>(width));
    detail::putU32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // color type RGBA
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    detail::putChunk(png, "IHDR", ihdr);
    detail::putChunk(png, "IDAT", z);
    detail::putChunk(png, "IEND", {});

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

} // namespace fuhglgraphics::screenshot
