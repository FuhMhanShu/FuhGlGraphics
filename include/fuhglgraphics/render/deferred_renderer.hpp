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

#include "debug_hud.hpp"
#include "decals.hpp"
#include "effects.hpp"
#include "gbuffer.hpp"
#include "tonemap.hpp"

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

namespace fuhglgraphics::anim {
class GpuSkinner;
}

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
    /// @param decals optional G8 decal list (e.g. FrameData::decals from DecalComponents) —
    /// projected into the G-buffer (albedo + normal) after the geometry pass when
    /// Effect::Decals is enabled; limited to render::kMaxDecals, sorted back-to-front.
    /// @param skins optional G14 GPU-skinning bindings (e.g. FrameData::skins), parallel to
    /// scene.items: when set for an item and Effect::GpuSkinning is enabled (GL >= 4.3), the
    /// item is drawn with the skinning vertex shader (matrix-palette SSBO) instead of the
    /// plain mesh — otherwise the plain mesh is drawn (CPU-skinning fallback, G6).
    void render(const fuhgl::render::SceneView& scene,
                const std::vector<std::uint32_t>* materialIds = nullptr,
                const std::vector<bool>* castShadow = nullptr,
                const std::vector<const particles::ParticleSystem*>* particleSystems = nullptr,
                const probes::LightProbeGrid* lightProbes = nullptr,
                const std::vector<Decal>* decals = nullptr,
                const water::WaterSurface* water = nullptr, // G13 water surface (FrameData::water)
                const std::vector<const anim::GpuSkinner*>* skins = nullptr); // G14 (FrameData::skins)

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
    /// @brief G15: screen-space AO factor (graph resource "ssao", R16F, 1 = unoccluded),
    /// computed from the G-buffer and sampled by the lighting pass on unit 6.
    const fuhgl::Texture& texSsao() const;
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
    /// @brief G23: tonemapping operator of the final pass (`uOperator` in kTonemapFrag);
    /// pure CPU twins in render/tonemap.hpp. Default ACES (previous behavior).
    TonemapOperator tonemapOperator = TonemapOperator::Aces;
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

    // --- G10 DoF (Effect::Dof): bokeh depth of field, post pass after the upscale --------
    float dofFocusDist = 8.f;   ///< world-space focus distance (view depth)
    float dofStrength = 12.f;   ///< CoC scale: blur radius per unit of focus error
    float dofMaxCoC = 24.f;     ///< max circle of confusion in output pixels

    /// @brief G15 SSAO (Effect::Ssao): the "ssao" post pass runs before lighting and writes
    /// the AO factor into the "ssao" resource (RG16F, .r); the lighting pass multiplies the material AO
    /// by it. Off by default (costs a fullscreen pass + kernel loop).
    float ssaoRadius = 1.5f;    ///< world-space sampling radius
    float ssaoIntensity = 1.4f; ///< occlusion strength
    float ssaoBias = 0.03f;     ///< depth bias in world units
    int ssaoSamples = 24;       ///< hemisphere kernel samples (max 64)

    /// @brief G16 SSR (Effect::Ssr): the "ssr" post pass runs after lighting and raymarches
    /// the reflection vector against the G-buffer depth (inputs hdr/gNormal/gDepth/gPbr ->
    /// hdrS), adding the reflected HDR color with an edge/roughness/fresnel mask. Off by
    /// default (costs a raymarch pass). G22 wetness raises the effective strength.
    int ssrMaxSteps = 32;       ///< raymarch steps
    float ssrThickness = 0.4f;  ///< hit thickness in world units
    float ssrMaxDist = 30.f;    ///< max march distance in world units
    float ssrStrength = 0.8f;   ///< reflection intensity
    float ssrWetnessBoost = 0.f; ///< G22: extra strength on wet surfaces (0..1 added to strength)

    /// @brief G17 bloom (Effect::Bloom): the "bloom" post pass runs after particles and
    /// before TAA — bright-pass above `bloomThreshold`, 9-tap blur, added with `bloomStrength`
    /// (output resource "hdrB", consumed by the TAA pass). Off by default.
    float bloomThreshold = 1.0f;
    float bloomStrength = 0.6f;
    float bloomRadius = 1.0f;

    /// @brief G21 volumetric fog / clouds (Effect::VolumetricFog): the "volumetric" post pass
    /// runs after particles and before bloom — it raymarches each pixel's view ray through a
    /// 3D-noise volume (height falloff + fBm clouds, Henyey–Greenstein phase for the sun),
    /// ending at the G-buffer depth for geometry or `volumetricMaxDist` for sky (inputs
    /// hdrP/gDepth -> hdrV, consumed by bloom). Off by default. Pure twins of the formulas:
    /// render/volumetric.hpp.
    float volumetricDensity = 0.02f;      ///< fog density at ground level (per world unit)
    float volumetricCloudiness = 0.6f;     ///< 0 = uniform fog, 1 = fully noise-driven clouds
    float volumetricHeightFalloff = 8.f;   ///< scale height of the exponential falloff
    float volumetricNoiseScale = 6.f;      ///< world units per 3D-noise cell
    float volumetricAnisotropy = 0.6f;     ///< Henyey–Greenstein g
    float volumetricMaxDist = 120.f;       ///< march end for sky pixels
    float volumetricStrength = 1.f;        ///< in-scatter scale
    int volumetricSteps = 24;              ///< march samples (max 64)
    float volumetricTime = 0.f;            ///< noise field drift (world units/frame step)

    /// @brief G22 wetness / rain (Effect::Wetness): modulates the PBR inputs in the lighting
    /// pass — up-facing surfaces get a glossy film (roughness toward 0.06), the albedo
    /// darkens ~35 % at full wetness, and a noise puddle mask deepens depressions. Also adds
    /// `wetnessAmount` to the SSR strength (wet surfaces reflect more). Off by default.
    float wetnessAmount = 0.8f;    ///< global rain amount 0..1
    float wetnessPuddle = 0.7f;    ///< puddle mask strength (0 = flat film, 1 = depressions only)
    float wetnessNoiseScale = 3.5f; ///< world units per puddle-noise cell

    /// @brief G25 HBAO (Effect::Hbao): the "hbao" post pass runs before lighting (next to G15
    /// SSAO) and writes the horizon-based AO factor into the "hbao" resource (RG16F, .r),
    /// sampled by the lighting pass on unit 24 when enabled (preferred over SSAO). Off by
    /// default. Pure CPU twin of the march: render/hbao.hpp.
    float hbaoRadius = 1.5f;     ///< world-space march radius
    float hbaoBias = 0.05f;      ///< height-above-plane bias (self-occlusion), world units
    int hbaoDirections = 8;      ///< tangent directions (clamped 1..16)
    int hbaoSteps = 4;           ///< march steps per direction (clamped 1..8)
    float hbaoPower = 1.6f;      ///< final exponent on the combined factor

    /// @brief G24 lens flare (Effect::LensFlare) + anamorphic streaks (Effect::Anamorphic):
    /// the "flare" post pass runs after bloom and before TAA (hdrB -> hdrF). The sun's screen
    /// position is derived from SceneView::sunDirection. Off by default.
    float lensFlareStrength = 0.8f;   ///< glow/ghost/streak intensity around the sun
    fuhgl::math::Vec3 lensFlareTint{1.f, 0.9f, 0.7f}; ///< warm flare tint
    float anamorphicStrength = 0.5f;  ///< streak intensity
    float anamorphicStretch = 10.f;   ///< horizontal blur width in texels per tap
    float anamorphicThreshold = 1.0f; ///< HDR luminance threshold of the streak source
    fuhgl::math::Vec3 anamorphicTint{0.55f, 0.7f, 1.f}; ///< cool blue streak tint

    /// @brief G10 debug HUD (Effect::DebugHud): fps / frame time / effect toggles panel.
    DebugHud& hud() { return hud_; }
    const DebugHud& hud() const { return hud_; }
    /// @brief Measured duration of the last render() call in milliseconds (for the HUD).
    float lastFrameMs() const { return lastFrameMs_; }

    int glMajor() const { return glMajor_; }
    int glMinor() const { return glMinor_; }

    /// @brief Frames rendered with TAA so far (0 after reset) — introspection/tests.
    unsigned frameIndex() const { return frameIndex_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    bool createInternal(); ///< (re)builds all GL resources at the current sizes
    void renderGBuffer(const fuhgl::render::SceneView& scene,
                       const std::vector<std::uint32_t>* materialIds,
                       const std::vector<const anim::GpuSkinner*>* skins);
    void drawDecals(const fuhgl::render::SceneView& scene);
    void drawFs(fuhgl::render::Material& mat, const std::function<void(fuhgl::Program&)>& setUniforms);
    /// @brief G13: planar reflection of the scene (without the water mesh) from the
    /// mirrored camera into the reflection FBO, clipped at the water plane.
    void renderReflection(const fuhgl::render::SceneView& scene, const fuhgl::math::Mat4& reflViewProj,
                          const fuhgl::math::Vec4& clipPlane, const fuhgl::math::Vec3& reflEye);
    void drawSsao(fuhgl::render::RenderGraph::PassContext& ctx); ///< G15, runs before lighting
    void drawHbao(fuhgl::render::RenderGraph::PassContext& ctx); ///< G25, runs before lighting (next to SSAO)
    void drawSsr(fuhgl::render::RenderGraph::PassContext& ctx);   ///< G16, runs after lighting
    void drawBloom(fuhgl::render::RenderGraph::PassContext& ctx); ///< G17, after particles, before TAA
    void drawFlare(fuhgl::render::RenderGraph::PassContext& ctx); ///< G24, after bloom, before TAA
    void drawVolumetric(fuhgl::render::RenderGraph::PassContext& ctx); ///< G21, after particles, before bloom
    void drawLighting(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawWater(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawParticles(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawTaa(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawUpscale(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawDof(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawTonemap(fuhgl::render::RenderGraph::PassContext& ctx);
    void drawHud(); ///< G10 HUD, called from render() after the post graph
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
    const probes::LightProbeGrid* curProbes_ = nullptr;
    bool waterEnabled_ = true;       ///< Effect::Water
    bool gpuSkinningEnabled_ = true; ///< Effect::GpuSkinning (GL >= 4.3, CPU fallback below)
    const std::vector<const anim::GpuSkinner*>* curSkins_ = nullptr; ///< G14 skins of this frame
    bool ssaoEnabled_ = false;   ///< Effect::Ssao (off by default)
    bool ssrEnabled_ = false;    ///< Effect::Ssr (off by default)
    bool bloomEnabled_ = false;  ///< Effect::Bloom (off by default)
    bool volumetricEnabled_ = false; ///< Effect::VolumetricFog (off by default)
    bool wetnessEnabled_ = false;    ///< Effect::Wetness (off by default)
    bool lensFlareEnabled_ = false;  ///< Effect::LensFlare (off by default)
    bool anamorphicEnabled_ = false; ///< Effect::Anamorphic (off by default)
    bool hbaoEnabled_ = false;       ///< Effect::Hbao (off by default)
    const water::WaterSurface* curWater_ = nullptr; ///< G13: active water surface this frame
    fuhgl::math::Mat4 waterModel_{};                 ///< its draw item's model matrix
    fuhgl::math::Vec4 waterClipPlane_{0.f, 1.f, 0.f, 0.f}; ///< world plane (n, d) for clipping
    fuhgl::math::Vec3 waterPlaneN_{0.f, 1.f, 0.f};
    bool decalsEnabled_ = true; ///< Effect::Decals
    const std::vector<Decal>* curDecals_ = nullptr;
    bool dofEnabled_ = false;    ///< Effect::Dof (off by default: costs a fullscreen pass)
    bool hudEnabled_ = false;    ///< Effect::DebugHud (off by default)
    DebugHud hud_;               ///< G10 HUD (created in createInternal)
    float lastFrameMs_ = 0.f;    ///< measured render() duration (HUD)
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
