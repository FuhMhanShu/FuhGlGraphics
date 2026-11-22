#pragma once

/// @file effects.hpp
/// @brief G9 scene-graph effects: every scene Node carries an Effects member — a list of
/// render::Effect values (used or not during rendering, union over all nodes per frame)
/// plus an EffectParams struct with the effect parameters. Scene::fillFrame() merges all
/// node effects (union of the lists; parameters of deeper/later nodes win) and
/// Effects::applyTo() pushes them onto the G3 DeferredRenderer.

#include "../render/effects.hpp"

#include <fuhgl/math/vec.hpp>
#include <fuhgl/render/shadow_kit.hpp>

#include <algorithm>
#include <vector>

namespace fuhglgraphics::render {
class DeferredRenderer;
}

namespace fuhglgraphics::scene {

/// @brief Parameters of the renderer effects. Runtime parameters are applied per frame
/// by Effects::applyTo(); create-time parameters (renderScale, msaaSamples, shadowMapSize,
/// csmCascades) are only used when the renderer is created.
struct EffectParams {
    float exposure = 1.f;            ///< HDR exposure before tonemapping (Effect::Tonemap)
    float taaBlend = 0.1f;           ///< weight of the current frame in the TAA blend (Effect::Taa)
    float upscaleSharpness = 0.6f;   ///< RCAS sharpening of the FSR1-style upscale (Effect::Upscale)
    float shadowStrength = 1.f;      ///< 0 = shadows off, 1 = full darkening (Effect::Shadows/CsmShadows)
    fuhgl::math::Vec3 shadowCenter{0.f, 0.f, 0.f}; ///< shadow scene bounds center
    float shadowRadius = 10.f;       ///< shadow scene bounds radius
    float shadowZNear = 0.5f;        ///< camera depth range the cascades cover
    float shadowZFar = 200.f;
    // --- F45 shadow-kit parameters (Effect::Shadows/CsmShadows/PointShadows/ContactShadows) ---
    fuhgl::render::ShadowFilter shadowFilter = fuhgl::render::ShadowFilter::Pcf3x3; ///< sampling filter
    bool shadowSnapToTexels = true;  ///< snap cascade light VPs to the texel grid (less swimming)
    float shadowCascadeBlend = 0.f;  ///< CSM cross-fade fraction of a cascade span
    float shadowLightSize = 1.f;     ///< PCSS: world-space light size (penumbra width)
    float shadowMinVariance = 5e-4f; ///< VSM: min-variance clamp against light bleeding
    float shadowNormalOffset = 0.f;  ///< normal-offset bias in shadow-map texels
    float shadowPcfRadius = 2.f;     ///< poisson-PCF kernel radius in texels
    // --- create-time parameters (used at DeferredRenderer::create / enable*CsmShadows) ---
    float renderScale = 1.f;         ///< internal resolution scale for Effect::Upscale
    int msaaSamples = 0;             ///< MSAA samples of the G-buffer (Effect::Msaa)
    int shadowMapSize = 2048;        ///< shadow map size
    int csmCascades = 4;             ///< cascade count for Effect::CsmShadows
};

/// @brief Effect list + parameters of one scene node (or of a whole frame after merging).
class Effects {
public:
    std::vector<render::Effect> list; ///< enabled effects (render::Effect directly)
    EffectParams params;              ///< effect parameters

    /// @brief Enables an effect (added to the list when absent).
    void enable(render::Effect e) {
        if (!enabled(e)) list.push_back(e);
    }
    /// @brief Disables an effect (removed from the list).
    void disable(render::Effect e) {
        list.erase(std::remove(list.begin(), list.end(), e), list.end());
    }
    /// @brief True when the effect is in the list.
    bool enabled(render::Effect e) const {
        return std::find(list.begin(), list.end(), e) != list.end();
    }
    /// @brief Enables/disables all effects at once.
    void setAll(bool on);

    /// @brief Merges another node's effects: union of the lists; `other.params` overwrites
    /// this node's params unconditionally — during Scene::fillFrame() traversal the node
    /// visited last (deepest/latest child) determines the final frame parameters.
    void merge(const Effects& other) {
        for (render::Effect e : other.list) enable(e);
        params = other.params;
    }

    /// @brief Applies the merged effects to a G3 DeferredRenderer: all effects off, then the
    /// list enabled (capability-gated by the renderer), then the runtime parameters set.
    /// Create-time params (renderScale/msaaSamples/shadowMapSize/csmCascades) are not applied
    /// here — pass them at renderer create time.
    void applyTo(render::DeferredRenderer& renderer) const;
};

} // namespace fuhglgraphics::scene
