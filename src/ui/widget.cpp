// G41 2D UI layer: Widget/Container/layout implementations (pure CPU).

#include <fuhglgraphics/ui/widget.hpp>

#include <algorithm>

namespace fuhglgraphics::ui {

Widget* Widget::hitTest(float x, float y) {
    return containsAbs(x, y) ? this : nullptr;
}

Container::Container() : layout(std::make_unique<FreeLayout>()) {}

Widget* Container::hitTest(float x, float y) {
    if (!containsAbs(x, y)) return nullptr;
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        if ((*it)->visible)
            if (Widget* h = (*it)->hitTest(x, y)) return h;
    return this;
}

void Container::arrangeChildren(const Rect& absRect) {
    if (layout) layout->arrange(*this, contentArea(absRect));
}

void Container::render(Batch2D& b, const Theme& th, float ox, float oy) const {
    const float ax = ox + rect.x, ay = oy + rect.y;
    if (background.w > 0.f)
        b.fillRect({ax, ay, rect.w, rect.h}, background, nullptr, {}, th.cornerRadius);
    if (clipContent) b.pushScissor({ax, ay, rect.w, rect.h});
    for (const auto& c : children)
        if (c->visible) c->render(b, th, ax, ay);
    if (clipContent) b.popScissor();
}

void HBoxLayout::arrange(Container& c, Rect area) {
    const float x0 = area.x + padding;
    const float y0 = area.y + padding;
    const float h = area.h - 2.f * padding;
    const float avail = area.w - 2.f * padding - spacing * static_cast<float>(c.children.size() > 0 ? c.children.size() - 1 : 0);
    float fixed = 0.f, stretchSum = 0.f;
    for (const auto& ch : c.children) {
        const auto d = ch->desiredSize();
        if (ch->sizeHint.stretch > 0.f) stretchSum += ch->sizeHint.stretch;
        else fixed += std::max(d.x, ch->sizeHint.minW);
    }
    const float rest = std::max(0.f, avail - fixed);
    float x = x0;
    for (const auto& ch : c.children) {
        const auto d = ch->desiredSize();
        float w = ch->sizeHint.stretch > 0.f ? rest * ch->sizeHint.stretch / (stretchSum > 0.f ? stretchSum : 1.f)
                                             : std::max(d.x, ch->sizeHint.minW);
        w = std::max(w, ch->sizeHint.minW);
        ch->rect = {x - area.x, y0 - area.y, w, h};
        x += w + spacing;
    }
}

void VBoxLayout::arrange(Container& c, Rect area) {
    const float x0 = area.x + padding;
    const float y0 = area.y + padding;
    const float w = area.w - 2.f * padding;
    const float avail = area.h - 2.f * padding - spacing * static_cast<float>(c.children.size() > 0 ? c.children.size() - 1 : 0);
    float fixed = 0.f, stretchSum = 0.f;
    for (const auto& ch : c.children) {
        const auto d = ch->desiredSize();
        if (ch->sizeHint.stretch > 0.f) stretchSum += ch->sizeHint.stretch;
        else fixed += std::max(d.y, ch->sizeHint.minH);
    }
    const float rest = std::max(0.f, avail - fixed);
    float y = y0;
    for (const auto& ch : c.children) {
        const auto d = ch->desiredSize();
        float h = ch->sizeHint.stretch > 0.f ? rest * ch->sizeHint.stretch / (stretchSum > 0.f ? stretchSum : 1.f)
                                             : std::max(d.y, ch->sizeHint.minH);
        h = std::max(h, ch->sizeHint.minH);
        ch->rect = {x0 - area.x, y - area.y, w, h};
        y += h + spacing;
    }
}

void GridLayout::arrange(Container& c, Rect area) {
    const int n = static_cast<int>(c.children.size());
    if (n == 0 || columns <= 0) return;
    const int cols = std::min(columns, n);
    const int rows = (n + cols - 1) / cols;
    const float cw = (area.w - 2.f * padding - spacing * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float ch = (area.h - 2.f * padding - spacing * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    for (int i = 0; i < n; ++i) {
        const int col = i % cols, row = i / cols;
        const float x = area.x + padding + col * (cw + spacing);
        const float y = area.y + padding + row * (ch + spacing);
        c.children[static_cast<std::size_t>(i)]->rect = {x - area.x, y - area.y, cw, ch};
    }
}

} // namespace fuhglgraphics::ui
