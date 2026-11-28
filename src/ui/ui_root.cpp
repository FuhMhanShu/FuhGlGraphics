// G41 2D UI layer: UiRoot implementation — event dispatch, focus, layout pass, render.

#include <fuhglgraphics/ui/ui_root.hpp>
#include <fuhglgraphics/ui/widgets.hpp>

namespace fuhglgraphics::ui {

UiRoot::UiRoot() { root.name = "root"; }

bool UiRoot::create(int maxQuads) { return batch.create(maxQuads); }

void UiRoot::setSize(int w, int h) {
    width_ = w;
    height_ = h;
}

void UiRoot::layoutRec(Widget& w, float ox, float oy) {
    w.abs = {ox + w.rect.x, oy + w.rect.y, w.rect.w, w.rect.h};
    w.arrangeChildren(w.abs);
    if (auto* c = dynamic_cast<Container*>(&w))
        for (auto& ch : c->children)
            if (ch->visible) layoutRec(*ch, w.abs.x, w.abs.y);
}

void UiRoot::render() { render(width_, height_); }

void UiRoot::render(int w, int h) {
    root.rect = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h)};
    layoutRec(root, 0.f, 0.f);
    batch.begin(w, h);
    root.render(batch, theme, 0.f, 0.f);
    batch.end();
}

void UiRoot::update(float dt) {
    time_ += dt;
    setTime(root);
}

void UiRoot::setTime(Widget& w) {
    w.time = time_;
    if (auto* c = dynamic_cast<Container*>(&w))
        for (auto& ch : c->children) setTime(*ch);
}

void UiRoot::clearHover(Widget& w) {
    w.hovered = false;
    if (auto* c = dynamic_cast<Container*>(&w))
        for (auto& ch : c->children) clearHover(*ch);
}

void UiRoot::setHover(Widget* w) {
    if (w == hovered_) return;
    clearHover(root);
    hovered_ = w;
    if (hovered_) hovered_->hovered = true;
}

void UiRoot::focus(Widget* w) {
    if (focused_ == w) return;
    if (focused_) focused_->focused = false;
    focused_ = w;
    if (focused_) {
        focused_->focused = true;
        if (auto* le = dynamic_cast<LineEdit*>(focused_)) {
            le->clipboardGet = clipboardGet;
            le->clipboardSet = clipboardSet;
        }
    }
}

void UiRoot::collectFocusable(Widget& w, std::vector<Widget*>& out) {
    if (!w.visible || !w.enabled) return;
    if (w.focusable) out.push_back(&w);
    if (auto* c = dynamic_cast<Container*>(&w))
        for (auto& ch : c->children) collectFocusable(*ch, out);
}

void UiRoot::focusNext() {
    std::vector<Widget*> f;
    collectFocusable(root, f);
    if (f.empty()) return;
    std::size_t idx = f.size();
    for (std::size_t i = 0; i < f.size(); ++i)
        if (f[i] == focused_) idx = i;
    focus(f[(idx + 1) % f.size()]);
}

void UiRoot::dispatch(const Event& e) {
    switch (e.type) {
    case EventType::MouseMove:
        if (captured_) captured_->onEvent(e);
        else if (Widget* h = root.hitTest(e.x, e.y)) {
            setHover(h);
            h->onEvent(e);
        } else {
            setHover(nullptr);
        }
        break;
    case EventType::MouseButton: {
        if (e.down) {
            Widget* h = root.hitTest(e.x, e.y);
            focus(h && h->focusable ? h : nullptr);
            captured_ = h;
            if (h) {
                h->pressed = true;
                h->onEvent(e);
            }
        } else {
            if (captured_) {
                captured_->onEvent(e);
                captured_->pressed = false;
            }
            captured_ = nullptr;
        }
        break;
    }
    case EventType::Wheel: {
        Widget* t = captured_ ? captured_ : hovered_;
        if (t) t->onEvent(e);
        break;
    }
    case EventType::Key:
        if (e.keyDown && e.key == KeyTab) {
            focusNext();
            break;
        }
        if (e.keyDown && e.key == KeyEscape && focused_) {
            focus(nullptr);
            break;
        }
        if (focused_) focused_->onEvent(e);
        break;
    case EventType::Char:
        if (focused_) focused_->onEvent(e);
        break;
    }
}

} // namespace fuhglgraphics::ui
