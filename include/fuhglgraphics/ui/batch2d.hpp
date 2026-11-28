#pragma once

/// @file batch2d.hpp
/// @brief G41 2D UI layer: Batch2D — one dynamic VBO, one shader, everything (solid
/// rects, textured quads, rounded rects, lines, bitmap text) queued per frame and drawn
/// in as few draw calls as texture switches allow. Renders into the currently bound
/// framebuffer — usable as a screen-space overlay post pass AND into an offscreen
/// texture (WorldSurface) for a menu on a 3D screen model.
///
/// Conventions like FuhGLCore: PIMPL, no exceptions for domain errors (bool + log),
/// GL 3.3 baseline, namespace fuhglgraphics::ui.

#include "font.hpp"
#include "geometry.hpp"

#include <fuhgl/gl/buffer.hpp>
#include <fuhgl/gl/program.hpp>
#include <fuhgl/gl/shader.hpp>
#include <fuhgl/gl/texture.hpp>
#include <fuhgl/gl/vao.hpp>
#include <fuhgl/math.hpp>

#include <memory>
#include <vector>

namespace fuhglgraphics::ui {

/// @brief Batched 2D renderer. Usage: create() once with a current GL context; per
/// frame: begin(w, h), queue primitives, end(). Coordinates are pixels, (0,0) =
/// top-left, y down. Alpha blending on, depth test off.
class Batch2D {
public:
    Batch2D();
    ~Batch2D();

    Batch2D(const Batch2D&) = delete;
    Batch2D& operator=(const Batch2D&) = delete;
    Batch2D(Batch2D&&) noexcept;
    Batch2D& operator=(Batch2D&&) noexcept;

    /// @brief Creates the shader, the dynamic VBO (maxQuads * 6 vertices), the embedded
    /// 8x8 font atlas texture and a 1x1 white texture. Needs a current GL context.
    bool create(int maxQuads = 8192);
    bool isCreated() const;

    /// @brief Sets viewport + ortho projection (y down), saves GL state, enables blending.
    void begin(int width, int height);
    /// @brief Flushes the queue (draw calls) and restores the saved GL state.
    void end();

    /// @brief Queues a filled quad. `tex` = nullptr -> solid color (white texture);
    /// `uv` = sub-rect of the texture in UV space (for atlases/9-slice). `cornerRadius`
    /// > 0 rounds the corners (SDF in the fragment shader, ignored for text quads).
    void fillRect(Rect r, const fuhgl::math::Vec4& color, const fuhgl::Texture* tex = nullptr,
                  Rect uv = {0.f, 0.f, 1.f, 1.f}, float cornerRadius = 0.f);
    /// @brief Queues a rectangular outline (4 thin quads, no rounded corners).
    void outlineRect(Rect r, const fuhgl::math::Vec4& color, float thickness = 1.f);
    /// @brief Queues a line as a thickness-wide quad.
    void line(float x1, float y1, float x2, float y2, const fuhgl::math::Vec4& color, float thickness = 1.f);
    /// @brief Queues one line of text with the embedded 8x8 font (font atlas quad per glyph).
    void text(const Font& font, const std::string& s, float x, float y, const fuhgl::math::Vec4& color);

    /// @brief Intersects the current scissor rect with `r` and enables scissor test.
    void pushScissor(Rect r);
    /// @brief Pops the scissor stack (disables the test when empty).
    void popScissor();

    /// @brief Draws everything queued so far (called by end(); public for explicit flushes).
    void flush();

    /// @brief The embedded font atlas texture (used by text()).
    const fuhgl::Texture* fontAtlas() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fuhglgraphics::ui
