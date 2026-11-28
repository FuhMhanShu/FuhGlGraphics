#pragma once

/// @file ui_root.hpp
/// @brief G41 2D UI layer: UiRoot — owns the widget tree + the Batch2D, dispatches input
/// events (hover/press/capture/focus), runs the layout pass and renders the tree.
/// Usage: create() once (GL context), build widgets under root, setSize(w, h), then per
/// frame dispatch(...) the app's events, update(dt) and render() — or render(w, h) into
/// a post pass / WorldSurface (see overlay.hpp).

#include "batch2d.hpp"
#include "theme.hpp"
#include "widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace fuhglgraphics::ui {

class LineEdit;

class UiRoot {
public:
    Container root;      ///< build the widget tree here (root covers the whole target)
    Theme theme;         ///< copied by defaultTheme()
    Batch2D batch;       ///< the renderer (created by create())

    UiRoot();

    /// @brief Creates the Batch2D (needs a current GL context).
    bool create(int maxQuads = 8192);
    bool isCreated() const { return batch.isCreated(); }

    void setSize(int w, int h);
    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief Routes one input event: hover, mouse capture, focus (click + Tab), wheel,
    /// keys and text to the focused widget.
    void dispatch(const Event& e);
    /// @brief Advances the UI clock (caret blink); call once per frame.
    void update(float dt);
    /// @brief Layout + render into the currently bound framebuffer at the set size.
    void render();
    /// @brief Layout + render at an explicit size (post pass / WorldSurface target).
    void render(int w, int h);

    Widget* focused() const { return focused_; }
    Widget* hovered() const { return hovered_; }
    /// @brief Sets the keyboard focus (nullptr clears; wires the clipboard into a LineEdit).
    void focus(Widget* w);

    /// @brief Clipboard callbacks (the app provides the actual clipboard), used by LineEdit.
    std::function<std::string()> clipboardGet;
    std::function<void(const std::string&)> clipboardSet;

private:
    int width_ = 0, height_ = 0;
    float time_ = 0.f;
    Widget* focused_ = nullptr;
    Widget* hovered_ = nullptr;
    Widget* captured_ = nullptr; ///< widget that got the mouse-down (receives move/up)

    void layoutRec(Widget& w, float ox, float oy);
    void setHover(Widget* w);
    void clearHover(Widget& w);
    void setTime(Widget& w);
    void collectFocusable(Widget& w, std::vector<Widget*>& out);
    void focusNext();
};

} // namespace fuhglgraphics::ui
