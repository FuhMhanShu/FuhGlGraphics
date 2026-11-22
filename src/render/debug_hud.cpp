#include "fuhglgraphics/render/debug_hud.hpp"

#include "fuhglgraphics/render/deferred_renderer.hpp"
#include "fuhglgraphics/render/effects.hpp"

#include <cstdio>
#include <string>

namespace fuhglgraphics::render {

DebugHud::DebugHud() = default;
DebugHud::~DebugHud() = default;
DebugHud::DebugHud(DebugHud&&) noexcept = default;
DebugHud& DebugHud::operator=(DebugHud&&) noexcept = default;

bool DebugHud::create() { return text_.create(); }
bool DebugHud::isCreated() const { return text_.isCreated(); }

void DebugHud::draw(int width, int height, float frameMs, const DeferredRenderer& renderer) {
    if (!text_.isCreated()) return;
    char line[128];
    text_.begin(width, height);
    float py = y;
    const float lh = fuhgl::render::TextRenderer::lineHeight(scale);
    const fuhgl::math::Vec4 white{1.f, 1.f, 1.f, 1.f};
    const fuhgl::math::Vec4 gray{0.75f, 0.75f, 0.75f, 1.f};
    const fuhgl::math::Vec4 green{0.35f, 1.f, 0.45f, 1.f};
    const fuhgl::math::Vec4 red{1.f, 0.4f, 0.35f, 1.f};
    const fuhgl::math::Vec4 shadow{0.f, 0.f, 0.f, 1.f};
    auto text = [&](const std::string& s, const fuhgl::math::Vec4& c) {
        text_.draw(s, x + scale, py + scale, scale, shadow); // cheap drop shadow
        text_.draw(s, x, py, scale, c);
        py += lh;
    };
    std::snprintf(line, sizeof(line), "FuhGlGraphics HUD  GL %d.%d", renderer.glMajor(), renderer.glMinor());
    text(line, white);
    std::snprintf(line, sizeof(line), "fps %.1f   frame %.2f ms   %ld frames", fps(), frameMs, frames());
    text(line, gray);
    for (int i = 0; i < kEffectCount; ++i) {
        const Effect e = static_cast<Effect>(i);
        const bool on = renderer.effectEnabled(e);
        std::snprintf(line, sizeof(line), "%-14s %s", effectName(e), on ? "[on]" : "[off]");
        text(line, on ? green : red);
    }
    text_.end();
}

} // namespace fuhglgraphics::render
