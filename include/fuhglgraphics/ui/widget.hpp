#pragma once

/// @file widget.hpp
/// @brief G41 2D UI layer: Widget base class + Container + layouts (Free/HBox/VBox/Grid).
/// A widget tree is laid out top-down every frame by UiRoot (rect is relative to the
/// parent's content area, abs is the cached absolute rect used for hit-testing/render).
/// Pure CPU — no GL (render() only queues into a Batch2D).

#include "batch2d.hpp"
#include "geometry.hpp"
#include "input.hpp"
#include "theme.hpp"

#include <memory>
#include <string>
#include <vector>

namespace fuhglgraphics::ui {

class Container;

/// @brief One layout pass over a container's children (area = absolute content rect).
class Layout {
public:
    virtual ~Layout() = default;
    virtual void arrange(Container& c, Rect area) = 0;
};

/// @brief Base class of all widgets.
class Widget {
public:
    virtual ~Widget() = default;

    std::string name;
    Rect rect;   ///< relative to the parent's content origin (set by the parent's layout)
    Rect abs;    ///< absolute (cached by UiRoot's layout pass; used for hit-test/render)
    bool visible = true;
    bool enabled = true;
    bool focusable = false;
    bool hovered = false;  ///< cursor is over this widget
    bool pressed = false;  ///< mouse button went down on this widget
    bool focused = false;  ///< keyboard focus
    float time = 0.f;      ///< UiRoot clock (caret blink etc.)
    Widget* parent = nullptr;

    /// @brief Size hints for box/grid layouts.
    struct SizeHint {
        float minW = 0.f, minH = 0.f;
        float prefW = 0.f, prefH = 0.f;
        float stretch = 0.f; ///< > 0: share of the remaining main-axis space, proportional to stretch
    } sizeHint;

    /// @brief Preferred size (box/grid layouts use it; free layout ignores it).
    virtual fuhgl::math::Vec2 desiredSize() const { return {sizeHint.prefW, sizeHint.prefH}; }
    /// @brief Handles one input event (absolute cursor coords in e.x/e.y). true = handled.
    virtual bool onEvent(const Event&) { return false; }
    /// @brief Queues this widget's primitives. (ox, oy) = absolute origin of the parent.
    virtual void render(Batch2D&, const Theme&, float ox, float oy) const { (void)ox; (void)oy; }
    /// @brief Deepest visible+enabled widget under the absolute point (this when none).
    virtual Widget* hitTest(float x, float y);
    /// @brief Content area for child layout (absolute), default = own abs rect.
    virtual Rect contentArea(const Rect& absRect) const { return absRect; }
    /// @brief Runs the layout over the children (Container only).
    virtual void arrangeChildren(const Rect&) {}

    bool containsAbs(float x, float y) const { return visible && enabled && abs.contains(x, y); }
};

/// @brief Widget with children + a layout.
class Container : public Widget {
public:
    std::vector<std::unique_ptr<Widget>> children;
    std::unique_ptr<Layout> layout;
    float padding = 8.f;              ///< content padding (contentArea)
    fuhgl::math::Vec4 background{0.f, 0.f, 0.f, 0.f}; ///< w == 0 -> no background
    bool clipContent = false;         ///< scissor children to the content rect

    Container();

    /// @brief Creates a child of type T, appends it and returns a reference.
    template <typename T, typename... Args>
    T& add(Args&&... args) {
        auto w = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *w;
        ref.parent = this;
        children.push_back(std::move(w));
        return ref;
    }
    void setLayout(std::unique_ptr<Layout> l) { layout = std::move(l); }

    Widget* hitTest(float x, float y) override;
    Rect contentArea(const Rect& absRect) const override { return absRect.inset(padding); }
    void arrangeChildren(const Rect& absRect) override;
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief No layout: children keep their rect (absolute positioning in parent coords).
class FreeLayout : public Layout {
public:
    void arrange(Container&, Rect) override {}
};

/// @brief Horizontal row: children get their preferred width (stretch shares the rest),
/// full cross-axis height, separated by `spacing`, inset by `padding`.
class HBoxLayout : public Layout {
public:
    float spacing = 6.f;
    float padding = 8.f;
    void arrange(Container& c, Rect area) override;
};

/// @brief Vertical column (see HBoxLayout).
class VBoxLayout : public Layout {
public:
    float spacing = 6.f;
    float padding = 8.f;
    void arrange(Container& c, Rect area) override;
};

/// @brief Row-major grid with `columns` columns, square-ish cells from the area.
class GridLayout : public Layout {
public:
    int columns = 1;
    float spacing = 6.f;
    float padding = 8.f;
    void arrange(Container& c, Rect area) override;
};

} // namespace fuhglgraphics::ui
