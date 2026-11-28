#pragma once

/// @file geometry.hpp
/// @brief G41 2D UI layer: axis-aligned rect in screen pixels, y down, top-left origin.
/// Pure math (no GL) — unit-testable.

#include <fuhgl/math.hpp>

namespace fuhglgraphics::ui {

/// @brief Axis-aligned rectangle in pixels ((0,0) = top-left, y grows downward).
struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const Rect& o) const { return !(*this == o); }
    bool empty() const { return w <= 0.f || h <= 0.f; }
    bool contains(float px, float py) const { return px >= x && px < right() && py >= y && py < bottom(); }
    fuhgl::math::Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    Rect inset(float m) const { return {x + m, y + m, w - 2.f * m, h - 2.f * m}; }
    /// @brief Intersection with `o` (empty rect when disjoint).
    Rect intersected(const Rect& o) const {
        const float x0 = x > o.x ? x : o.x;
        const float y0 = y > o.y ? y : o.y;
        const float x1 = right() < o.right() ? right() : o.right();
        const float y1 = bottom() < o.bottom() ? bottom() : o.bottom();
        return {x0, y0, x1 > x0 ? x1 - x0 : 0.f, y1 > y0 ? y1 - y0 : 0.f};
    }
};

} // namespace fuhglgraphics::ui
