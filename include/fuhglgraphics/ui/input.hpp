#pragma once

/// @file input.hpp
/// @brief G41 2D UI layer: platform-neutral input events + key codes. The application
/// (Qt widget, game loop, MCP-driven test, ...) translates its native events into
/// these and feeds them to UiRoot::dispatch(). Pure data — no GL, no Qt.

#include <string>

namespace fuhglgraphics::ui {

/// @brief Kind of an input event.
enum class EventType {
    MouseMove,    ///< x/y = cursor position (pixels, y down)
    MouseButton,  ///< x/y = cursor position, button + down (press/release)
    Wheel,        ///< x/y = cursor position, wheelDelta = notches (positive = up)
    Key,          ///< key code (see Key), keyDown, modifiers
    Char,         ///< text = UTF-8 text input (for LineEdit)
};

/// @brief Mouse buttons.
enum class MouseButton { Left = 0, Middle = 1, Right = 2 };

/// @brief Platform-independent key codes. Printable keys use their ASCII code;
/// the special keys live at >= 256 to avoid collisions.
enum Key {
    KeyBackspace = 256,
    KeyTab = 257,
    KeyEnter = 258,
    KeyEscape = 259,
    KeyDelete = 260,
    KeyLeft = 261,
    KeyRight = 262,
    KeyUp = 263,
    KeyDown = 264,
    KeyHome = 265,
    KeyEnd = 266,
};

/// @brief Modifier bitmask of Event::modifiers.
enum Modifiers { ModNone = 0, ModCtrl = 1, ModShift = 2, ModAlt = 4 };

/// @brief One input event. Only the fields of `type` are meaningful.
struct Event {
    EventType type = EventType::MouseMove;
    float x = 0.f, y = 0.f;                    ///< cursor position (Mouse* / Wheel)
    MouseButton button = MouseButton::Left;    ///< MouseButton
    bool down = false;                         ///< MouseButton: true = pressed, false = released
    float wheelDelta = 0.f;                    ///< Wheel
    int key = 0;                               ///< Key: ASCII or Key_* (Key events)
    bool keyDown = false;                      ///< Key: true = pressed, false = released
    int modifiers = ModNone;                   ///< Key: bitmask of Modifiers
    std::string text;                          ///< Char: UTF-8 text
};

} // namespace fuhglgraphics::ui
