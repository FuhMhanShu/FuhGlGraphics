#pragma once

/// @file deferred_renderer.hpp
/// @brief G3 deferred renderer — a new deferred renderer on top of FuhGLCore
/// (docs/REQUIREMENTS.md G3) with an "AAA-style" feature set on the GL 3.3 baseline:
///
/// - extended G-buffer (GBufferAttachment): albedo+normal, metallic/roughness/AO,
///   HDR emissive, motion vectors (for TAA), material ID (for G8 decals), optional MSAA
/// - PBR lighting pass reusing FuhGLCore lights (LightBlock UBO), shadows
///   (ShadowMap / CascadeShadowMap) and IBL (render::Environment)
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

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fuhglgraphics::render {

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
    void render(const fuhgl::render::SceneView& scene,
                const std::vector<std::uint32_t>* materialIds = nullptr,
                const std::vector<bool>* castShadow = nullptr);

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
    fuhgl::math::Vec3 skyBottom{0.85f, 0.88f, 0.95f};

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

    // --- shadows (reusing FuhGLCore ShadowMap / CascadeShadowMap) --------------------
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

    /// @brief Frames rendered with TAA so far (0 after reset) — introspection/tests.
    unsigned frameIndex() const { return frameIndex_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    bool createInternal(); ///< (re)builds all GL resources at the current sizes
    void renderGBuffer(const fuhgl::render::SceneView& scene,
                       const std::vector<std::uint32_t>* materialIds);
    void drawFs(fuhgl::render::Material& mat, const std::function<void(fuhgl::Program&)>& setUniforms);
    void drawLighting(fuhgl::render::RenderGraph::PassContext& ctx);
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
    bool shadows_ = false;
    bool csmEnabled_ = false;
    int shadowSize_ = 0;   ///< map size of the single ShadowMap (recreated on resize/scale change)
    int csmSize_ = 0;      ///< map size of the CascadeShadowMap (recreated on resize/scale change)
    int csmCascades_ = 0;
    float shadowZNear_ = 0.5f, shadowZFar_ = 200.f;
    fuhgl::math::Vec3 shadowCenter_{0.f, 0.f, 0.f};
    float shadowRadius_ = 10.f;
    float shadowStrength_ = 1.f;

    int glMajor_ = 0, glMinor_ = 0;
    unsigned warnedMask_ = 0; ///< effects already warned about (bit = Effect index)
};

} // namespace fuhglgraphics::render
