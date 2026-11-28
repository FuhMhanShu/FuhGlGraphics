#pragma once

/// @file overlay.hpp
/// @brief G41 2D UI layer: integration of a UiRoot into the G3 deferred renderer —
/// two modes:
///  - makeOverlayPass(): a screen-space overlay drawn as a G40 custom post pass on top
///    of the tonemapped frame (after "tonemap", LDR, unaffected by DoF/bloom/TAA).
///  - WorldSurface: renders the UI into an offscreen texture that is bound as the albedo
///    of a 3D "screen" mesh in the scene (menu on a 3D monitor model); pickUv() maps a
///    scene::pick() ray hit (G18) onto the surface UV so clicks reach the widgets.
///
/// Conventions like FuhGLCore: PIMPL, bool + log (no exceptions), namespace fuhglgraphics::ui.

#include "ui_root.hpp"

#include <fuhglgraphics/render/deferred_renderer.hpp>
#include <fuhglgraphics/scene/picking.hpp>
#include <fuhglgraphics/scene/scene.hpp>

#include <fuhgl/cam/camera.hpp>
#include <fuhgl/gl/framebuffer.hpp>
#include <fuhgl/gl/texture.hpp>

namespace fuhglgraphics::ui {

/// @brief Builds the G40 PostPass descriptor that draws `ui` as a screen-space overlay.
/// Register with DeferredRenderer::addPostPass(makeOverlayPass(ui)); the pass copies its
/// "@chain" input (the tonemapped frame) and draws the UI over it with alpha blending.
render::DeferredRenderer::PostPass makeOverlayPass(UiRoot& ui, const std::string& name = "ui");

/// @brief Offscreen render target for a world-space UI: render the UI into texture(),
/// bind that texture as a MeshEntry::albedoTexture of a quad in the 3D scene.
class WorldSurface {
public:
    WorldSurface();
    ~WorldSurface();

    WorldSurface(const WorldSurface&) = delete;
    WorldSurface& operator=(const WorldSurface&) = delete;

    /// @brief Creates the RGBA8 texture + FBO (needs a current GL context).
    bool create(int width, int height);
    void resize(int width, int height);
    bool isCreated() const { return created_; }

    /// @brief Renders `ui` into the surface (binds the FBO, sets the viewport).
    void render(UiRoot& ui);

    const fuhgl::Texture& texture() const { return tex_; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief Maps a screen-space pick onto the surface UV (0..1, matching the quad's UVs).
    /// Returns false when the pick missed or hit another node than `surfaceNode`'s subtree.
    /// `scene` is non-const because scene::pick updates the world transforms.
    static bool pickUv(scene::Scene& scene, const fuhgl::Camera& cam, int viewportW, int viewportH,
                       float px, float py, const scene::Node* surfaceNode, float& u, float& v);

private:
    fuhgl::Texture tex_{fuhgl::Texture::Target::Tex2D};
    fuhgl::Framebuffer fbo_;
    int width_ = 0, height_ = 0;
    bool created_ = false;
};

} // namespace fuhglgraphics::ui
