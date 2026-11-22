#pragma once

/// @file debug_hud.hpp
/// @brief G10 mini debug HUD: an ImGui-like panel drawn with FuhGLCore's TextRenderer
/// (embedded 8x8 font, no external files) showing fps, frame time and the on/off state
/// of every render::Effect. Toggled in the G3 renderer via render::Effect::DebugHud;
/// the panel is drawn as the last post-graph pass into the renderer's output.
///
/// Conventions like FuhGLCore: PIMPL (wraps GL resources), no exceptions,
/// namespace fuhglgraphics::render.

#include <fuhgl/render/text.hpp>

#include <memory>

namespace fuhglgraphics::render {

class DeferredRenderer;

/// @brief Debug HUD panel (fps + frame time + effect toggles). Usage: create() once with a
/// current GL context, call frame(nowSeconds) once per rendered frame, and draw() into the
/// currently bound framebuffer (the renderer does this in its "hud" post pass).
class DebugHud {
public:
    DebugHud();
    ~DebugHud();

    DebugHud(const DebugHud&) = delete;
    DebugHud& operator=(const DebugHud&) = delete;
    DebugHud(DebugHud&&) noexcept;
    DebugHud& operator=(DebugHud&&) noexcept;

    /// @brief Creates the underlying TextRenderer (needs a current GL context).
    bool create();
    bool isCreated() const;

    /// @brief Records one frame at `nowSeconds` (FpsCounter). Call once per rendered frame.
    void frame(double nowSeconds) { fps_.frame(nowSeconds); }
    /// @brief Smoothed fps (0 until two frames were recorded).
    float fps() const { return fps_.fps(); }
    /// @brief Total frames recorded.
    long frames() const { return fps_.frames; }

    float scale = 2.f;   ///< glyph scale (2 = 16 px line height)
    float x = 8.f;       ///< panel position, pixels from the top-left
    float y = 8.f;

    /// @brief Draws the panel into the currently bound framebuffer (w x h, y down).
    /// `frameMs` and the effect states come from the renderer.
    void draw(int width, int height, float frameMs, const DeferredRenderer& renderer);

private:
    fuhgl::render::FpsCounter fps_;
    fuhgl::render::TextRenderer text_;
};

} // namespace fuhglgraphics::render
