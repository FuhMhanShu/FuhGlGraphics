#pragma once

/// @file widgets.hpp
/// @brief G41 2D UI layer: the widget set — Label, Button, Checkbox, RadioButton, Slider,
/// ProgressBar, LineEdit, ListBox, Panel, Image. Values live in ui::Property<T> so the
/// backend can bind to them two-way (ui/binding.hpp). Pure CPU logic; render() only
/// queues primitives into a Batch2D.

#include "binding.hpp"
#include "widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace fuhglgraphics::ui {

/// @brief Control background color by state (disabled > pressed > hovered > normal).
inline fuhgl::math::Vec4 controlColor(const Theme& th, const Widget& w) {
    if (!w.enabled) return th.controlDisabled;
    if (w.pressed) return th.controlPressed;
    if (w.hovered) return th.controlHover;
    return th.controlBg;
}

/// @brief Static text.
class Label : public Widget {
public:
    enum class Align { Left, Center, Right } align = Align::Left;
    Property<std::string> text;
    fuhgl::math::Vec4 color{0.f, 0.f, 0.f, 0.f}; ///< w == 0 -> theme.text
    float fontScale = 1.5f;

    fuhgl::math::Vec2 desiredSize() const override {
        Font f{fontScale};
        return {f.measure(text.get()).x, f.lineHeight()};
    }
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Clickable button; clicked fires on mouse-release inside after a press.
class Button : public Widget {
public:
    Property<std::string> text;
    Signal clicked;
    float fontScale = 1.5f;

    Button();
    bool onEvent(const Event& e) override;
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Toggle with a check box; checked is a Property<bool> (bindable), toggled fires on change.
class Checkbox : public Widget {
public:
    Property<bool> checked{false};
    Signal toggled;
    std::string label;
    float fontScale = 1.5f;

    Checkbox();
    bool onEvent(const Event& e) override;
    fuhgl::math::Vec2 desiredSize() const override;
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Like Checkbox but a click only ever sets checked = true (group logic lives in the model).
class RadioButton : public Checkbox {
public:
    bool onEvent(const Event& e) override;
};

/// @brief Horizontal slider; value is a Property<float> in [min, max], draggable + arrow keys.
class Slider : public Widget {
public:
    float min = 0.f, max = 1.f;
    Property<float> value{0.f};

    Slider();
    float normalized() const { return (value.get() - min) / (max - min); }
    void setFromX(float x); ///< maps an absolute cursor x onto [min, max]
    bool onEvent(const Event& e) override;
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Read-only progress bar, value in [0, 1].
class ProgressBar : public Widget {
public:
    Property<float> value{0.f};

    ProgressBar();
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Single-line text input with caret, selection, clipboard (Ctrl+A/C/X/V),
/// validator (rejects invalid edits, error border) and committed (Enter).
class LineEdit : public Widget {
public:
    Property<std::string> text;
    Signal committed;
    std::function<bool(const std::string&)> validator;      ///< false = reject the edit
    bool valid = true;                                      ///< last edit passed the validator
    std::function<std::string()> clipboardGet;             ///< wired to UiRoot's clipboard by focus()
    std::function<void(const std::string&)> clipboardSet;
    std::size_t caret = 0, anchor = 0;                      ///< selection = [selStart, selEnd)
    float fontScale = 1.5f;

    LineEdit();
    std::size_t selStart() const { return caret < anchor ? caret : anchor; }
    std::size_t selEnd() const { return caret > anchor ? caret : anchor; }
    void deleteSelection();
    void insert(const std::string& t);
    bool onEvent(const Event& e) override;
    fuhgl::math::Vec2 desiredSize() const override;
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Selectable list with wheel scrolling; selected is a Property<int> (-1 = none).
class ListBox : public Widget {
public:
    std::vector<std::string> items;
    Property<int> selected{-1};
    float itemHeight = 18.f;
    float fontScale = 1.5f;
    float scroll = 0.f; ///< pixels scrolled down

    ListBox();
    float maxScroll() const { return items.empty() ? 0.f : std::max(0.f, static_cast<float>(items.size()) * itemHeight - abs.h); }
    bool onEvent(const Event& e) override;
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Titled container panel (background + border + title bar); children are laid out
/// in the content area below the title bar.
class Panel : public Container {
public:
    std::string title;
    float titleBarHeight = 18.f;
    float fontScale = 1.5f;

    Rect contentArea(const Rect& absRect) const override;
    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

/// @brief Textured image; optional 9-slice (proportional thirds) when nineSlice is set.
class Image : public Widget {
public:
    const fuhgl::Texture* tex = nullptr;
    bool nineSlice = false;

    void render(Batch2D& b, const Theme& th, float ox, float oy) const override;
};

} // namespace fuhglgraphics::ui
