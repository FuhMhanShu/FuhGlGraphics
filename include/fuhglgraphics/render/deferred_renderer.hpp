#pragma once

/// @file deferred_renderer.hpp
/// @brief G3 deferred renderer — a new deferred renderer on top of FuhGLCore
/// (docs/REQUIREMENTS.md G3) with an "AAA-style" feature set on the GL 3.3 baseline:
///
/// - extended G-buffer (GBufferAttachment): albedo+normal, metallic/roughness/AO,
///   HDR emissive, motion vectors (for TAA), material ID (for G8 decals), optional MSAA
/// - PBR lighting pass reusing FuhGLCore lights (LightBlock UBO), shadows and IBL
///   (render::Environment); shadows run on the FuhGLCore F45 shadow kit
///   (ShadowTarget/ShadowMap/CascadeShadowMap/CubeShadowMap, texel snapping, poisson/PCSS/VSM
///   filters, cascade cross-fade, point-light cube shadows, contact shadows  —  GLSL snippets
///   from fuhgl/render/shadow_glsl.hpp)
/// - own post chain on fuhgl::render::RenderGraph: TAA -> FSR1-style upscaling
///   (EASU+RCAS) -> ACES tonemap, each with a per-effect toggle
/// - F39/F40-style capability gating: unsupported effects are refused with a warning
///
/// Conventions like FuhGLCore: PIMPL, no exceptions for domain errors (bool + log),
/// namespace fuhglgraphics::render.

#include "effects.hpp"
#include "gbuffer.hpp"

#include <fuhgl/math.hpp>
#include <fuhgl/gl/framebuffer.hpp>
#include <fuhgl/gl/texture.hpp>
#include <fuhgl/render/graph.hpp>
#include <fuhgl/render/material.hpp>
#include <fuhgl/render/scene.hpp>
#include <fuhgl/render/shadow.hpp>
#include <fuhgl/render/shadow_kit.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fuhglgraphics::particles {
class ParticleSystem;
}

namespace fuhglgraphics::probes {
class LightProbeGrid;
}

namespace fuhglgraphics::water {
class WaterSurface;
}

namespace fuhglgraphics::render {

/// @brief Texture unit of the optional sky cubemap in the lighting pass. 16 because the
/// F46 shadow kit occupies units 11..15 (moments 2D/array, point cube depth/moments,
/// scene depth); FuhGLCore's built-in pipelines use units 3..10.
inline constexpr int kTextureUnitSkyCube = 16;

/// @brief G3 deferred renderer. Usage: create(w, h) once with a current GL context,
/// then per frame fill a fuhgl::render::SceneView and call render(scene).
/// The frame lands LDR-tonemapped in output(); the extended G-buffer and the HDR
/// image are exposed for inspection and for later features (G8 decals, ...).
class DeferredRenderer {
public:
    DeferredRenderer();
    ~DeferredRenderer();

    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;
    DeferredRenderer(DeferredRenderer&&) noexcept;
    DeferredRenderer& operator=(DeferredRenderer&&) noexcept;

    /// @brief Creates the G-buffer + post chain at `width` x `height` output size.
    /// `msaaSamples` > 0 enables a multisampled G-buffer with resolve (Effect::Msaa).
    /// Needs a current GL context with GL >= 3.3.
    bool create(int width, int height, int msaaSamples = 0);
    /// @brief Recreates all targets at a new output size (MSAA setting is kept).
    void resize(int width, int height);

    /// @brief Renders one full frame into output(): shadow depth pass (if enabled)
    /// -> G-buffer -> MSAA resolve -> post graph (lighting -> TAA -> upscale -> tonemap).
    /// @param materialIds optional, parallel to scene.items: per-item material ID
    /// written into the G-buffer MaterialId attachment (default 0) — used by G8 decals.
    /// @param castShadow optional, parallel to scene.items: per-item flag whether the
    /// item is drawn into the shadow depth pass (G9 scene graph sets this per node;
    /// missing entries default to true).
    /// @param particleSystems optional G5 particle systems (e.g. FrameData::particleSystems
    /// from ParticleEmitterComponents) — drawn in the "particles" post pass when
    /// Effect::Particles is enabled. Call ParticleSystem::update(dt) yourself per frame.
    /// @param lightProbes optional G7 SH light-probe grid (e.g. FrameData::lightProbes from
    /// a LightProbeComponent) — sampled in the lighting pass when Effect::LightProbes is
    /// enabled; the grid's 3D textures are uploaded lazily on the first render with it.
    /// @param water optional G13 water surface (e.g. FrameData::water from a
    /// WaterComponent): its mesh must be one of scene.items. When set and Effect::Water is
    /// enabled, the renderer first renders a planar reflection of the scene (without the
    /// water) into reflectionTexture(), keeps the water out of the G-buffer and shades it
    /// in the "water" post pass (waves, reflection, refraction, foam). When the effect is
    /// off, the water mesh falls back to plain G-buffer PBR rendering.
    void render(const fuhgl::render::SceneView& scene,
                const std::vector<std::uint32_t>* materialIds = nullptr,
                const std::vector<bool>* castShadow = nullptr,
                const std::vector<const particles::ParticleSystem*>* particleSystems = nullptr,
                const probes::LightProbeGrid* lightProbes = nullptr,
                const water::WaterSurface* water = nullptr);

    /// @brief LDR output target (RGBA8, tonemapped when Effect::Tonemap is on).
    fuhgl::Framebuffer& output();
    int width() const { return width_; }
    int height() const { return height_; }
    int msaaSamples() const { return msaa_; }
    /// @brief Internal (pre-upscale) resolution = output size * renderScale.
    int internalWidth() const { return internalWidth_; }
    int internalHeight() const { return internalHeight_; }

    // --- extended G-buffer accessors (resolved textures when MSAA is on) ------------
    const fuhgl::Texture& texAlbedo() const;     ///< GBufferAttachment::Albedo
    const fuhgl::Texture& texNormal() const;     ///< GBufferAttachment::Normal
    const fuhgl::Texture& texPbr() const;        ///< GBufferAttachment::Pbr (r=metallic, g=roughness, b=ao)
    const fuhgl::Texture& texEmissive() const;   ///< GBufferAttachment::Emissive (HDR)
    const fuhgl::Texture& texMotion() const;     ///< GBufferAttachment::Motion (prevUV - curUV)
    const fuhgl::Texture& texMaterialId() const; ///< GBufferAttachment::MaterialId
    const fuhgl::Texture& texDepth() const;      ///< depth attachment as texture
    /// @brief Lit HDR scene before the post chain (graph resource "hdr").
    const fuhgl::Texture& hdrTexture() const;
    /// @brief G13: planar-reflection target (RGBA16F, internal size * waterReflectionScale),
    /// rendered from the mirrored camera before the G-buffer when a water surface is active.
    const fuhgl::Texture& reflectionTexture() const;
    /// @brief The post-processing render graph (passes "lighting", "taa", "upscale", "tonemap").
    fuhgl::render::RenderGraph& postGraph();

    // --- settings --------------------------------------------------------------------
    /// @brief Render scale in (0, 1]: G-buffer/lighting/TAA run at output*scale and are
    /// upscaled to the output size (Effect::Upscale, FSR1-style EASU+RCAS). 1 = off.
    void setRenderScale(float scale);
    float renderScale() const { return renderScale_; }
    float exposure = 1.f;             ///< HDR exposure before tonemapping
    float taaBlend = 0.1f;            ///< weight of the current frame in the TAA blend (1 on the first frame)
    float upscaleSharpness = 0.6f;    ///< RCAS sharpening amount of the FSR1-style upscale (0..1)
    fuhgl::math::Vec3 skyTop{0.25f, 0.45f, 0.9f};    ///< sky gradient, used for background pixels
    fuhgl::math::Vec3 skyBottom{0.85f, 0.88f, 0.95f}; ///< (replaced by the sky cubemap when set)

    /// @brief Optional sky cubemap: when set, background pixels in the lighting pass sample
    /// it by view-ray direction instead of the skyTop/skyBottom gradient (unit kTextureUnitSkyCube).
    void setSkyCubemap(const fuhgl::Texture* cube) { skyCube_ = cube; }
    const fuhgl::Texture* skyCubemap() const { return skyCube_; }

    /// @brief God rays (crepuscular rays) toward the sun, added in the tonemap pass: a radial
    /// blur from each pixel toward the sun's screen position on the HDR image (cheap
    /// approximation without occlusion). The sun disk itself is drawn by the lighting pass
    /// (SceneView::sunColor, derived from the first directional light). Off by default.
    struct GodRays {
        bool enabled = false;
        float strength = 0.8f; ///< scale of the added ray light
        float decay = 0.95f;   ///< per-sample decay along the ray
        int samples = 32;      ///< radial blur samples (max 64)
    };
    GodRays godRays;

    // --- effects: uniform toggle API + capability gating (F39/F40-style) -------------
    /// @brief Enables/disables one effect. Unsupported effects are refused with a
    /// warning and stay off (graceful degradation). Effect::Msaa is a create()
    /// parameter and cannot be toggled at runtime.
    void setEffectEnabled(Effect e, bool on);
    /// @brief True when the effect is both requested and supported by the current GPU.
    bool effectEnabled(Effect e) const;
    /// @brief Enables/disables all effects at once (still capability-gated).
    void setAllEffects(bool on);
    /// @brief True when the current GPU can run this effect at all (pure mapping, F39).
    bool effectSupported(Effect e) const { return render::effectSupported(e, glMajor_, glMinor_); }

    // --- shadows on the FuhGLCore F45 shadow kit (ShadowMap/CascadeShadowMap are thin
    // wrappers over ShadowTarget there; CubeShadowMap covers point lights) -------------
    void enableShadows(int mapSize = 2048);
    void disableShadows() { shadows_ = false; }
    bool shadowsEnabled() const { return shadows_; }
    /// @brief CSM instead of the single ShadowMap: one depth-array layer per cascade.
    void enableCsmShadows(int mapSize = 2048, int cascades = 4);
    void disableCsmShadows() { csmEnabled_ = false; }
    bool csmShadowsEnabled() const { return csmEnabled_; }
    void setShadowSceneBounds(const fuhgl::math::Vec3& center, float radius) {
        shadowCenter_ = center;
        shadowRadius_ = radius;
    }
    void setShadowStrength(float s) { shadowStrength_ = s; }
    /// @brief Camera depth range the cascades cover; default 0.5 .. 200.
    void setShadowZRange(float zn, float zf) { shadowZNear_ = zn; shadowZFar_ = zf; }

    /// @brief Shadow sampling filter of the shadow light (F46 ShadowFilter): legacy 3x3
    /// PCF (default, bit-identical to before), poisson-disk PCF, PCSS (blocker search +
    /// penumbra-sized kernel, needs shadowLightSize) or VSM (renders RGBA32F moments into
    /// an extra attachment  —  recreated automatically when the filter changes).
    fuhgl::render::ShadowFilter shadowFilter = fuhgl::render::ShadowFilter::Pcf3x3;
    void setShadowFilter(fuhgl::render::ShadowFilter f);
    /// @brief Snap each cascade light VP to the shadow-map texel grid (F45
    /// snapLightVPToTexels inside CascadeShadowMap)  —  kills sub-texel shadow swimming.
    bool shadowSnapToTexels = true;
    /// @brief CSM cross-fade fraction of a cascade span near the split (0 = hard switch).
    float shadowCascadeBlend = 0.f;
    float shadowLightSize = 1.f;     ///< PCSS: world-space light size (penumbra width)
    float shadowMinVariance = 5e-4f;  ///< VSM: min-variance clamp against light bleeding
    float shadowNormalOffset = 0.f;  ///< normal-offset bias, in shadow-map texels
    float shadowPcfRadius = 2.f;      ///< poisson-PCF kernel radius in texels

    /// @brief Cube shadow map for one point light (F46 CubeShadowMap, Effect::PointShadows).
    /// `lightIndex` = index into SceneView::lights, -1 = the first point light of the frame.
    void enablePointShadow(int lightIndex = -1, int mapSize = 1024);
    void disablePointShadow() { pointShadows_ = false; }
    bool pointShadowEnabled() const { return pointShadows_; }
    fuhgl::render::CubeShadowMap* pointShadowMap() { return pointShadows_ ? &pointShadowMap_ : nullptr; }
    /// @brief Depth range of the point-light cube map.
    void setPointShadowRange(float zn, float zf) { pointShadowNear_ = zn; pointShadowFar_ = zf; }
    void setPointShadowStrength(float s) { pointShadowStrength_ = s; }

    /// @brief Screen-space contact shadows on the shadow light (F46, Effect::ContactShadows):
    /// raymarched in the lighting pass against the G-buffer depth. Off by default.
    bool contactShadowsEnabled = false;
    float contactShadowStrength = 1.f;
    float contactShadowMaxDist = 3.f;    ///< raymarch length in world units
    float contactShadowThickness = 0.2f; ///< depth thickness treated as occluder
    int contactShadowSteps = 16;         ///< raymarch steps

    /// @brief G7: strength of the light-probe diffuse indirect term in the lighting pass
    /// (Effect::LightProbes). Set per frame via scene::EffectParams::lightProbeStrength.
    float lightProbeStrength = 1.f;

    /// @brief G13: resolution scale of the planar-reflection target relative to the
    /// internal render size (0, 1]; applied on the next create()/resize()/setWaterReflectionScale().
    float waterReflectionScale = 0.5f;
    /// @brief Recreates the reflection target at the new scale.
    void setWaterReflectionScale(float scale);

    /// @brief Frames rendered with TAA so far (0 after reset) — introspection/tests.
    unsigned frameIndex() const { return frameIndex_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    bool createInternal(); ///< (re)builds all GL resources at the current sizes
    void renderGBuffer(const fuhgl::render::SceneView& scene,
                       const std::vector<std::uint32_t>* materialIds);
    /// @brief G13: planar reflection of the scene (without the water mesh) from the
    /// mirrored camera into the reflection FBO, clipped at the water plane.
    void renderReflection(const fuhgl::render::SceneView& scene, const fuhgl::math::Mat4& reflViewProj,
                          const fuhgl::math::Vec4& clipPlane, const fuhgl::math::Vec3& reflEye);
    void drawFs(fuhgl::render::Material& mat, const std::function<void(fuhgl::Program&)>& setUniforms);
    void drawLighting(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawWater(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawParticles(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawTaa(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawUpscale(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawTonemap(fuhgl::render::RenderGraph::PassContext& ctx);
    void applyCapabilities();
    void gate(Effect e, bool& flag);

    int width_ = 0, height_ = 0, msaa_ = 0;
    int internalWidth_ = 0, internalHeight_ = 0;
    float renderScale_ = 1.f;
    unsigned frameIndex_ = 0;
    fuhgl::math::Mat4 prevViewProj_{};
    const fuhgl::render::SceneView* curScene_ = nullptr;

    bool taaEnabled_ = true;
    bool upscaleEnabled_ = true;
    bool tonemapEnabled_ = true;
    bool iblEnabled_ = true;
    bool particlesEnabled_ = true; ///< Effect::Particles
    const std::vector<const particles::ParticleSystem*>* curParticles_ = nullptr;
    bool lightProbesEnabled_ = true; ///< Effect::LightProbes
    bool waterEnabled_ = true;       ///< Effect::Water
    const probes::LightProbeGrid* curProbes_ = nullptr;
    const water::WaterSurface* curWater_ = nullptr; ///< G13: active water surface this frame
    fuhgl::math::Mat4 waterModel_{};                 ///< its draw item's model matrix
    fuhgl::math::Vec4 waterClipPlane_{0.f, 1.f, 0.f, 0.f}; ///< world plane (n, d) for clipping
    fuhgl::math::Vec3 waterPlaneN_{0.f, 1.f, 0.f};
    bool shadows_ = false;
    bool csmEnabled_ = false;
    int shadowSize_ = 0;   ///< map size of the single ShadowMap (recreated on resize/scale change)
    int csmSize_ = 0;      ///< map size of the CascadeShadowMap (recreated on resize/scale change)
    int csmCascades_ = 0;
    float shadowZNear_ = 0.5f, shadowZFar_ = 200.f;
    fuhgl::math::Vec3 shadowCenter_{0.f, 0.f, 0.f};
    float shadowRadius_ = 10.f;
    float shadowStrength_ = 1.f;
    bool pointShadows_ = false;  ///< F46: point-light cube shadow requested
    int pointShadowLightIndex_ = -1;
    int activePointShadowLightIndex_ = -1; ///< resolved during render() (-1 = none this frame)
    int pointShadowSize_ = 1024;
    float pointShadowNear_ = 0.5f, pointShadowFar_ = 100.f;
    float pointShadowStrength_ = 1.f;
    fuhgl::render::CubeShadowMap pointShadowMap_; ///< F46: allocated when pointShadows_

    const fuhgl::Texture* skyCube_ = nullptr;
    int glMajor_ = 0, glMinor_ = 0;
    unsigned warnedMask_ = 0; ///< effects already warned about (bit = Effect index)
};

} // namespace fuhglgraphics::render
