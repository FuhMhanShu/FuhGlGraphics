// G41 2D UI layer: widget implementations (pure CPU logic; render() queues into Batch2D).

#include <fuhglgraphics/ui/widgets.hpp>

#include <algorithm>
#include <cmath>

namespace fuhglgraphics::ui {

namespace {
float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
} // namespace

void Label::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    Font f{th.fontScale};
    const float ax = ox + rect.x, ay = oy + rect.y;
    const fuhgl::math::Vec2 ts = f.measure(text.get());
    float tx = ax;
    if (align == Align::Center) tx = ax + (rect.w - ts.x) * 0.5f;
    else if (align == Align::Right) tx = ax + rect.w - ts.x;
    const fuhgl::math::Vec4 c = color.w > 0.f ? color : (enabled ? th.text : th.textDisabled);
    b.text(f, text.get(), tx, ay + (rect.h - ts.y) * 0.5f, c);
}

Button::Button() {
    focusable = true;
    sizeHint.prefW = 90.f;
    sizeHint.prefH = 24.f;
}

bool Button::onEvent(const Event& e) {
    if (!enabled) return false;
    if (e.type != EventType::MouseButton || e.button != MouseButton::Left) return false;
    if (e.down && containsAbs(e.x, e.y)) {
        pressed = true;
        return true;
    }
    if (!e.down && pressed) {
        pressed = false;
        if (containsAbs(e.x, e.y)) clicked.emit();
        return true;
    }
    return false;
}

void Button::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    b.fillRect({ax, ay, rect.w, rect.h}, controlColor(th, *this), nullptr, {}, th.cornerRadius);
    b.outlineRect({ax, ay, rect.w, rect.h}, focused ? th.accent : th.border, th.borderWidth);
    Font f{th.fontScale};
    const fuhgl::math::Vec2 ts = f.measure(text.get());
    b.text(f, text.get(), ax + (rect.w - ts.x) * 0.5f, ay + (rect.h - ts.y) * 0.5f,
           enabled ? th.text : th.textDisabled);
}

Checkbox::Checkbox() {
    focusable = true;
    sizeHint.prefH = 20.f;
    checked.onChanged().connect([this] { toggled.emit(); });
}

bool Checkbox::onEvent(const Event& e) {
    if (!enabled) return false;
    if (e.type != EventType::MouseButton || e.button != MouseButton::Left) return false;
    if (e.down && containsAbs(e.x, e.y)) {
        pressed = true;
        return true;
    }
    if (!e.down && pressed) {
        pressed = false;
        if (containsAbs(e.x, e.y)) checked.set(!checked.get());
        return true;
    }
    return false;
}

bool RadioButton::onEvent(const Event& e) {
    if (!enabled) return false;
    if (e.type != EventType::MouseButton || e.button != MouseButton::Left) return false;
    if (e.down && containsAbs(e.x, e.y)) {
        pressed = true;
        return true;
    }
    if (!e.down && pressed) {
        pressed = false;
        if (containsAbs(e.x, e.y)) checked.set(true);
        return true;
    }
    return false;
}

fuhgl::math::Vec2 Checkbox::desiredSize() const {
    Font f{fontScale};
    return {f.cell() + 6.f + f.measure(label).x, std::max(f.lineHeight(), 14.f)};
}

void Checkbox::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    Font f{th.fontScale};
    const float box = std::min(14.f, rect.h - 4.f);
    const Rect boxR{ax, ay + (rect.h - box) * 0.5f, box, box};
    b.fillRect(boxR, controlColor(th, *this), nullptr, {}, 3.f);
    b.outlineRect(boxR, focused ? th.accent : th.border, th.borderWidth);
    if (checked.get()) {
        const float m = 3.f;
        b.line(boxR.x + m, boxR.y + box * 0.5f, boxR.x + box * 0.45f, boxR.bottom() - m, th.checkMark, 2.f);
        b.line(boxR.x + box * 0.45f, boxR.bottom() - m, boxR.right() - m, boxR.y + m, th.checkMark, 2.f);
    }
    b.text(f, label, ax + box + 6.f, ay + (rect.h - f.lineHeight()) * 0.5f, enabled ? th.text : th.textDisabled);
}

Slider::Slider() {
    focusable = true;
    sizeHint.prefW = 120.f;
    sizeHint.prefH = 20.f;
}

void Slider::setFromX(float x) {
    const float n = clamp01(abs.w > 0.f ? (x - abs.x) / abs.w : 0.f);
    value.set(min + n * (max - min));
}

bool Slider::onEvent(const Event& e) {
    if (!enabled) return false;
    if (e.type == EventType::MouseButton && e.button == MouseButton::Left) {
        if (e.down && containsAbs(e.x, e.y)) {
            pressed = true;
            setFromX(e.x);
            return true;
        }
        if (!e.down && pressed) {
            pressed = false;
            return true;
        }
    }
    if (e.type == EventType::MouseMove && pressed) {
        setFromX(e.x);
        return true;
    }
    if (e.type == EventType::Key && e.keyDown) {
        const float step = (max - min) * 0.05f;
        if (e.key == KeyLeft) {
            value.set(std::max(min, value.get() - step));
            return true;
        }
        if (e.key == KeyRight) {
            value.set(std::min(max, value.get() + step));
            return true;
        }
    }
    return false;
}

void Slider::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    const float n = clamp01(normalized());
    const float trackH = 6.f;
    const Rect track{ax, ay + (rect.h - trackH) * 0.5f, rect.w, trackH};
    b.fillRect(track, controlColor(th, *this), nullptr, {}, 3.f);
    b.fillRect({track.x, track.y, track.w * n, trackH}, enabled ? th.accent : th.controlDisabled, nullptr, {}, 3.f);
    const float hx = track.x + track.w * n - 5.f;
    b.fillRect({hx, ay + 2.f, 10.f, rect.h - 4.f}, enabled ? th.text : th.textDisabled, nullptr, {}, 2.f);
    if (focused) b.outlineRect({ax, ay, rect.w, rect.h}, th.accent, th.borderWidth);
}

ProgressBar::ProgressBar() {
    sizeHint.prefW = 140.f;
    sizeHint.prefH = 12.f;
}

void ProgressBar::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    b.fillRect({ax, ay, rect.w, rect.h}, th.controlBg, nullptr, {}, th.cornerRadius);
    b.fillRect({ax, ay, rect.w * clamp01(value.get()), rect.h}, enabled ? th.accent : th.controlDisabled, nullptr, {}, th.cornerRadius);
    b.outlineRect({ax, ay, rect.w, rect.h}, th.border, th.borderWidth);
}

LineEdit::LineEdit() {
    focusable = true;
    sizeHint.prefW = 160.f;
    sizeHint.prefH = 22.f;
}

void LineEdit::deleteSelection() {
    if (selStart() == selEnd()) return;
    std::string s = text.get();
    s.erase(selStart(), selEnd() - selStart());
    text.set(s);
    caret = anchor = selStart();
}

void LineEdit::insert(const std::string& t) {
    deleteSelection();
    std::string s = text.get();
    s.insert(caret, t);
    if (validator && !validator(s)) { // reject invalid edits, flag the error state
        valid = false;
        return;
    }
    text.set(s);
    caret += t.size();
    anchor = caret;
    valid = true;
}

bool LineEdit::onEvent(const Event& e) {
    if (!enabled || !focused) return false;
    if (e.type == EventType::Char && !e.text.empty()) {
        insert(e.text);
        return true;
    }
    if (e.type != EventType::Key || !e.keyDown) return false;
    std::string s = text.get();
    switch (e.key) {
    case KeyBackspace:
        if (selStart() != selEnd()) deleteSelection();
        else if (caret > 0) {
            s.erase(caret - 1, 1);
            text.set(s);
            --caret;
            anchor = caret;
        }
        valid = !validator || validator(text.get());
        return true;
    case KeyDelete:
        if (selStart() != selEnd()) deleteSelection();
        else if (caret < s.size()) {
            s.erase(caret, 1);
            text.set(s);
        }
        valid = !validator || validator(text.get());
        return true;
    case KeyLeft:
        if (caret > 0) --caret;
        anchor = caret;
        return true;
    case KeyRight:
        if (caret < s.size()) ++caret;
        anchor = caret;
        return true;
    case KeyHome:
        caret = anchor = 0;
        return true;
    case KeyEnd:
        caret = anchor = s.size();
        return true;
    case KeyEnter:
        committed.emit();
        return true;
    default:
        break;
    }
    if (e.modifiers & ModCtrl) {
        if (e.key == 'a' || e.key == 'A') {
            caret = 0;
            anchor = s.size();
            return true;
        }
        if ((e.key == 'c' || e.key == 'C' || e.key == 'x' || e.key == 'X') && clipboardSet && selStart() != selEnd()) {
            clipboardSet(s.substr(selStart(), selEnd() - selStart()));
            if (e.key == 'x' || e.key == 'X') deleteSelection();
            return true;
        }
        if (e.key == 'v' || e.key == 'V') {
            if (clipboardGet) insert(clipboardGet());
            return true;
        }
    }
    return false;
}

fuhgl::math::Vec2 LineEdit::desiredSize() const {
    Font f{fontScale};
    return {sizeHint.prefW, f.lineHeight() + 8.f};
}

void LineEdit::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    Font f{th.fontScale};
    const float cw = f.cell(), lh = f.lineHeight();
    b.fillRect({ax, ay, rect.w, rect.h}, enabled ? (hovered ? th.controlHover : th.controlBg) : th.controlDisabled,
               nullptr, {}, th.cornerRadius);
    const fuhgl::math::Vec4 bc = !valid ? th.error : (focused ? th.accent : th.border);
    b.outlineRect({ax, ay, rect.w, rect.h}, bc, th.borderWidth);
    const Rect inner = Rect{ax, ay, rect.w, rect.h}.inset(4.f);
    b.pushScissor(inner);
    const float ty = inner.y + (inner.h - lh) * 0.5f;
    if (selStart() != selEnd()) {
        const float x0 = inner.x + static_cast<float>(selStart()) * cw;
        const float x1 = inner.x + static_cast<float>(selEnd()) * cw;
        b.fillRect({x0, inner.y, x1 - x0, inner.h}, th.selection);
    }
    b.text(f, text.get(), inner.x, ty, enabled ? th.text : th.textDisabled);
    if (focused && std::fmod(time, 1.f) < 0.55f) {
        const float cx = inner.x + static_cast<float>(caret) * cw;
        b.line(cx, inner.y + 2.f, cx, inner.bottom() - 2.f, th.text, 1.f);
    }
    b.popScissor();
}

ListBox::ListBox() {
    focusable = true;
    sizeHint.prefW = 160.f;
    sizeHint.prefH = 100.f;
}

bool ListBox::onEvent(const Event& e) {
    if (!enabled) return false;
    if (e.type == EventType::Wheel && containsAbs(e.x, e.y)) {
        scroll = std::max(0.f, std::min(maxScroll(), scroll - e.wheelDelta * itemHeight));
        return true;
    }
    if (e.type == EventType::MouseButton && e.button == MouseButton::Left && e.down && containsAbs(e.x, e.y)) {
        const int idx = static_cast<int>((e.y - abs.y + scroll) / itemHeight);
        if (idx >= 0 && idx < static_cast<int>(items.size())) selected.set(idx);
        return true;
    }
    return false;
}

void ListBox::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    b.fillRect({ax, ay, rect.w, rect.h}, controlColor(th, *this), nullptr, {}, 2.f);
    b.outlineRect({ax, ay, rect.w, rect.h}, focused ? th.accent : th.border, th.borderWidth);
    b.pushScissor(Rect{ax, ay, rect.w, rect.h}.inset(1.f));
    Font f{th.fontScale};
    const fuhgl::math::Vec4 tc = enabled ? th.text : th.textDisabled;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const float iy = ay + static_cast<float>(i) * itemHeight - scroll;
        if (iy + itemHeight < ay || iy > ay + rect.h) continue;
        if (static_cast<int>(i) == selected.get())
            b.fillRect({ax + 1.f, iy, rect.w - 2.f, itemHeight}, th.selection);
        b.text(f, items[i], ax + 4.f, iy + (itemHeight - f.lineHeight()) * 0.5f, tc);
    }
    b.popScissor();
}

Rect Panel::contentArea(const Rect& absRect) const {
    return {absRect.x + padding, absRect.y + titleBarHeight, absRect.w - 2.f * padding,
            absRect.h - titleBarHeight - padding};
}

void Panel::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    b.fillRect({ax, ay, rect.w, rect.h}, th.panelBg, nullptr, {}, th.cornerRadius);
    b.outlineRect({ax, ay, rect.w, rect.h}, th.border, th.borderWidth);
    if (!title.empty()) {
        Font f{th.fontScale};
        b.text(f, title, ax + padding, ay + (titleBarHeight - f.lineHeight()) * 0.5f, th.text);
        b.line(ax, ay + titleBarHeight, ax + rect.w, ay + titleBarHeight, th.border, 1.f);
    }
    if (clipContent) b.pushScissor({ax, ay, rect.w, rect.h});
    for (const auto& c : children)
        if (c->visible) c->render(b, th, ax, ay);
    if (clipContent) b.popScissor();
}

void Image::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    const Rect r{ax, ay, rect.w, rect.h};
    if (!tex) {
        b.fillRect(r, th.controlBg, nullptr, {}, th.cornerRadius);
        return;
    }
    if (!nineSlice) {
        b.fillRect(r, {1.f, 1.f, 1.f, 1.f}, tex);
        return;
    }
    const float u[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f};
    const float v[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f};
    const float x[4] = {r.x, r.x + r.w / 3.f, r.x + 2.f * r.w / 3.f, r.right()};
    const float y[4] = {r.y, r.y + r.h / 3.f, r.y + 2.f * r.h / 3.f, r.bottom()};
    for (int gy = 0; gy < 3; ++gy)
        for (int gx = 0; gx < 3; ++gx)
            b.fillRect({x[gx], y[gy], x[gx + 1] - x[gx], y[gy + 1] - y[gy]}, {1.f, 1.f, 1.f, 1.f}, tex,
                       {u[gx], v[gy], u[gx + 1] - u[gx], v[gy + 1] - v[gy]}, 0.f);
}

} // namespace fuhglgraphics::ui
