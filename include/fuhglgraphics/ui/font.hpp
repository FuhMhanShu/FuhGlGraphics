#pragma once

/// @file font.hpp
/// @brief G41 2D UI layer: Font metrics + word wrap for the embedded 8x8 bitmap font
/// (fuhgl::render::font8x8Glyph — the atlas texture itself is built by Batch2D).
/// Pure CPU — unit-testable. One monospace cell per character, cell = 8 px * scale.

#include <fuhgl/math.hpp>
#include <fuhgl/render/text.hpp>

#include <string>
#include <vector>

namespace fuhglgraphics::ui {

class Font {
public:
    float scale = 1.f; ///< glyph scale (1 = 8 px cell height)

    /// @brief Edge length of one glyph cell in pixels at the current scale.
    float cell() const { return static_cast<float>(fuhgl::render::kFontGlyphSize) * scale; }
    /// @brief Line height in pixels (one cell per line).
    float lineHeight() const { return cell(); }
    /// @brief Size of `text` in pixels (monospace: one cell per character).
    fuhgl::math::Vec2 measure(const std::string& text) const {
        return {static_cast<float>(text.size()) * cell(), lineHeight()};
    }
    /// @brief Word-wraps `text` to `maxWidth` pixels (breaks at spaces; long words are split).
    std::vector<std::string> wrap(const std::string& text, float maxWidth) const {
        std::vector<std::string> lines;
        std::string line;
        const float cw = cell();
        auto flush = [&] {
            if (!line.empty()) lines.push_back(line);
            line.clear();
        };
        std::string word;
        auto pushWord = [&] {
            while (!word.empty() && (line.size() + 1 + word.size()) * cw > maxWidth && !line.empty()) {
                flush();
            }
            // hard-split overlong words
            while (word.size() * cw > maxWidth) {
                const std::size_t fit = static_cast<std::size_t>(maxWidth / cw);
                if (!line.empty()) flush();
                lines.push_back(word.substr(0, fit));
                word.erase(0, fit);
            }
            if (!line.empty() && (line.size() + 1 + word.size()) * cw > maxWidth) flush();
            if (!line.empty()) line += ' ';
            line += word;
            word.clear();
        };
        for (const char c : text) {
            if (c == ' ' || c == '\n') {
                pushWord();
                if (c == '\n') flush();
            } else {
                word += c;
            }
        }
        pushWord();
        flush();
        if (lines.empty()) lines.push_back("");
        return lines;
    }
};

} // namespace fuhglgraphics::ui
