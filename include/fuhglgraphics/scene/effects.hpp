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
    // --- G7 light probes (Effect::LightProbes) ---
    float lightProbeStrength = 1.f;  ///< scale of the probe diffuse-indirect term
    // --- G10 DoF (Effect::Dof) ---
    float dofFocusDist = 8.f;   ///< world-space focus distance (view depth)
    float dofStrength = 12.f;   ///< CoC scale per unit of focus error
    float dofMaxCoC = 24.f;     ///< max circle of confusion in output pixels
    // --- G15 SSAO (Effect::Ssao) ---
    float ssaoRadius = 1.5f;    ///< world-space sampling radius
    float ssaoIntensity = 1.4f; ///< occlusion strength (occ fraction * intensity subtracted from 1)
    float ssaoBias = 0.03f;     ///< depth bias in world units (avoids self-occlusion acne)
    int ssaoSamples = 24;       ///< hemisphere kernel samples (max 64)
    // --- G16 SSR (Effect::Ssr) ---
    int ssrMaxSteps = 32;       ///< raymarch steps
    float ssrThickness = 0.4f;  ///< hit thickness in world units
    float ssrMaxDist = 30.f;    ///< max march distance in world units
    float ssrStrength = 0.8f;   ///< reflection intensity (wetness adds on top, G22)
    // --- G17 bloom (Effect::Bloom) ---
    float bloomThreshold = 1.0f; ///< HDR luminance above which a pixel blooms
    float bloomStrength = 0.6f;  ///< added bloom intensity
    float bloomRadius = 1.0f;    ///< blur radius in texels (scaled by the tap pattern)
    // --- G21 volumetric fog / clouds (Effect::VolumetricFog) ---
    float volumetricDensity = 0.02f;     ///< fog density at ground level (per world unit)
    float volumetricCloudiness = 0.6f;    ///< 0 = uniform fog, 1 = fully noise-driven clouds
    float volumetricHeightFalloff = 8.f;  ///< scale height of the exponential height falloff
    float volumetricNoiseScale = 6.f;     ///< world units per 3D-noise cell
    float volumetricAnisotropy = 0.6f;    ///< Henyey–Greenstein g (forward scattering)
    float volumetricMaxDist = 120.f;      ///< march end for sky pixels (world units)
    float volumetricStrength = 1.f;       ///< in-scatter intensity scale
    int volumetricSteps = 24;             ///< raymarch samples (max 64)
    // --- G22 wetness / rain (Effect::Wetness) ---
    float wetnessAmount = 0.8f;   ///< global rain amount 0..1 (0 = dry)
    float wetnessPuddle = 0.7f;   ///< puddle mask strength: 0 = flat wet film, 1 = only depressions
    float wetnessNoiseScale = 3.5f; ///< world units per puddle-noise cell
    // --- G31 building weathering (Effect::Weathering, lighting pass) ---
    float weatherStrength = 0.8f;      ///< global weathering intensity 0..1
    float weatherHumidity = 0.5f;      ///< local humidity 0..1 -> moss/lichen + rain streaks
    float weatherBaseY = 0.f;           ///< ground level of the buildings (splash-stain reference)
    float weatherRainStreaks = 0.6f;    ///< strength of the vertical dirt streaks on walls 0..1
    fuhgl::math::Vec3 weatherSoilColor{0.36f, 0.29f, 0.21f}; ///< local soil color (splash stain + plinth)
    fuhgl::math::Vec3 weatherMossColor{0.24f, 0.33f, 0.16f}; ///< moss/lichen color
    fuhgl::math::Vec3 weatherBrickColor{0.45f, 0.22f, 0.16f}; ///< masonry under chipped plaster (walls)
    // --- G38 bridge waterline (Effect::Weathering, lighting pass) ---
    float waterLevelWorld = 0.f;         ///< world y of the water plane (0 = effect off)
    float bridgeWaterlineStrength = 1.f; ///< wet algae band strength on bridge meshes (matId 10) 0..1
    // --- G23 tonemapping operator (Effect::Tonemap) ---
    int tonemapOperator = 0;      ///< render::TonemapOperator: 0 = ACES, 1 = Reinhard, 2 = Uncharted2, 3 = linear
    // --- G30 lens flare + anamorphic streaks (Effect::LensFlare / Effect::Anamorphic) ---
    float lensFlareStrength = 0.8f;   ///< glow/ghost/streak intensity around the sun
    float anamorphicStrength = 0.5f;  ///< horizontal streak intensity
    float anamorphicStretch = 10.f;   ///< horizontal blur width in texels per tap
    float anamorphicThreshold = 1.0f; ///< HDR luminance threshold of the streak source
    // --- G31 HBAO (Effect::Hbao) ---
    float hbaoRadius = 1.5f;     ///< world-space horizon-march radius
    float hbaoBias = 0.05f;      ///< height-above-plane bias (self-occlusion), world units
    int hbaoDirections = 8;      ///< tangent directions (clamped 1..16)
    int hbaoSteps = 4;           ///< march steps per direction (clamped 1..8)
    float hbaoPower = 1.6f;      ///< final exponent on the combined AO factor
    // --- G32 chromatic aberration (Effect::ChromaticAberration) ---
    float caStrength = 0.006f;   ///< R/B uv offset at the frame corners
    // --- G33 subsurface scattering (Effect::Subsurface) ---
    float sssDistortion = 0.4f;  ///< translucency normal shift
    float sssPower = 3.f;        ///< translucency pow
    float sssScale = 1.f;        ///< translucency intensity
    float sssThickness = 1.f;    ///< effective object thickness
    // --- G34 screen-space GI (Effect::Gi) ---
    float giStrength = 0.6f;     ///< indirect intensity
    int giCones = 3;             ///< cones per pixel (clamped 1..4)
    int giSteps = 12;            ///< march steps per cone (clamped 1..32)
    float giMaxDist = 8.f;       ///< cone length in world units
    float giThickness = 0.3f;    ///< hit band in world units
    // --- G40 comic look (Effect::ComicLook, final post pass after the tonemap) ---
    float comicLevels = 6.f;          ///< posterize color levels (>= 2)
    float comicEdgeStrength = 1.4f;   ///< ink edge intensity (Sobel response scale)
    float comicEdgeThreshold = 0.06f; ///< edge response above which a pixel becomes ink
    float comicSaturation = 1.35f;    ///< cel color saturation boost (1 = unchanged)
    fuhgl::math::Vec3 comicInkColor{0.04f, 0.03f, 0.05f}; ///< ink/outline color
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
