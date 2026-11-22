#include "fuhglgraphics/scene/effects.hpp"

#include "fuhglgraphics/render/deferred_renderer.hpp"

namespace fuhglgraphics::scene {

void Effects::setAll(bool on) {
    list.clear();
    if (on) {
        for (int i = 0; i < render::kEffectCount; ++i)
            list.push_back(static_cast<render::Effect>(i));
    }
}

void Effects::applyTo(render::DeferredRenderer& renderer) const {
    renderer.setAllEffects(false);
    for (render::Effect e : list) renderer.setEffectEnabled(e, true);
    renderer.exposure = params.exposure;
    renderer.taaBlend = params.taaBlend;
    renderer.upscaleSharpness = params.upscaleSharpness;
    renderer.setShadowStrength(params.shadowStrength);
    renderer.setShadowSceneBounds(params.shadowCenter, params.shadowRadius);
    renderer.setShadowZRange(params.shadowZNear, params.shadowZFar);
}

} // namespace fuhglgraphics::scene
