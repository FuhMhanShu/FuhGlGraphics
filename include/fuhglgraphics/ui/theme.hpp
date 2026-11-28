#pragma once

/// @file theme.hpp
/// @brief G41 2D UI layer: Theme — the color/metric table every widget reads per state
/// (normal / hovered / pressed / disabled / focused). Pure data — no GL.

#include <fuhgl/math.hpp>

namespace fuhglgraphics::ui {

/// @brief Colors + metrics of the default dark theme; copy and adjust per app.
struct Theme {
    fuhgl::math::Vec4 windowBg{0.11f, 0.12f, 0.14f, 0.96f};
    fuhgl::math::Vec4 panelBg{0.16f, 0.17f, 0.20f, 0.98f};
    fuhgl::math::Vec4 controlBg{0.23f, 0.24f, 0.28f, 1.f};
    fuhgl::math::Vec4 controlHover{0.30f, 0.32f, 0.37f, 1.f};
    fuhgl::math::Vec4 controlPressed{0.18f, 0.20f, 0.26f, 1.f};
    fuhgl::math::Vec4 controlDisabled{0.18f, 0.18f, 0.20f, 0.6f};
    fuhgl::math::Vec4 accent{0.25f, 0.55f, 0.95f, 1.f};      ///< selection, slider fill, focus ring
    fuhgl::math::Vec4 text{0.92f, 0.93f, 0.95f, 1.f};
    fuhgl::math::Vec4 textDisabled{0.55f, 0.56f, 0.58f, 1.f};
    fuhgl::math::Vec4 border{0.05f, 0.06f, 0.08f, 1.f};
    fuhgl::math::Vec4 selection{0.25f, 0.45f, 0.80f, 0.85f}; ///< list selection / text selection
    fuhgl::math::Vec4 checkMark{1.f, 1.f, 1.f, 1.f};
    fuhgl::math::Vec4 error{0.90f, 0.30f, 0.25f, 1.f};       ///< invalid LineEdit input

    float fontScale = 1.5f;   ///< default glyph scale (8 px * 1.5 = 12 px line height)
    float padding = 8.f;      ///< container inner padding
    float spacing = 6.f;      ///< between children in box layouts
    float cornerRadius = 4.f; ///< rounded rect radius of panels/controls
    float borderWidth = 1.f;
    float titleBarHeight = 18.f; ///< Panel title bar
};

/// @brief The default dark theme.
inline Theme defaultTheme() { return Theme{}; }

} // namespace fuhglgraphics::ui
