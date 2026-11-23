#pragma once

/// @file gpu_skinner.hpp
/// @brief G14 — GPU skinning (GL >= 4.3): a GpuSkinner owns one skinned mesh's GPU side —
/// a VAO with the bind-pose vertex data (standard 11-floats layout, attribs 0..3) plus the
/// JOINTS_0/WEIGHTS_0 attributes (locations 5/6, uploaded as float4) and an SSBO holding the
/// joint matrix palette (binding 0). The G3 G-buffer pass draws the skinner with a
/// skinning vertex shader (GLSL 430, matrix-palette SSBO) when render::Effect::GpuSkinning
/// is enabled; on GL < 4.3 or with the effect off the renderer falls back to the base mesh
/// (CPU skinning via skinMeshCpu, G6).
///
/// Conventions like the rest of the repo: PIMPL for the GL resources, no exceptions
/// (bool + log), namespace fuhglgraphics::anim.

#include "animation.hpp"

#include <fuhgl/gl/buffer.hpp>
#include <fuhgl/gl/vao.hpp>
#include <fuhgl/render/mesh.hpp>

#include <memory>
#include <vector>

namespace fuhglgraphics::anim {

/// @brief GPU skinning binding of one skinned mesh (GL >= 4.3, PIMPL).
class GpuSkinner {
public:
    GpuSkinner();
    ~GpuSkinner();

    GpuSkinner(const GpuSkinner&) = delete;
    GpuSkinner& operator=(const GpuSkinner&) = delete;
    GpuSkinner(GpuSkinner&&) noexcept;
    GpuSkinner& operator=(GpuSkinner&&) noexcept;

    /// @brief True when the current GL context supports SSBOs (GL >= 4.3). Pure query.
    static bool supported();

    /// @brief Creates the VAO + vertex/index/joint/weight buffers from the bind-pose
    /// `base` mesh and its skin weights. Needs a current GL >= 4.3 context.
    /// `skin` must have 4 joint bytes + 4 weights per vertex (same vertex count as base).
    bool create(const fuhgl::render::MeshData& base, const SkinWeights& skin);
    bool isReady() const { return ready_; }

    /// @brief Uploads the joint palette (one mat4 per joint, e.g. from Skeleton::palette)
    /// into the SSBO. Call per frame after Skeleton::updatePalette().
    void uploadPalette(const std::vector<fuhgl::math::Mat4>& palette);
    /// @brief Binds the palette SSBO to binding point 0 (the skinning VS reads it there).
    void bindPalette() const;

    /// @brief Binds the VAO and issues the draw call (indexed when the base mesh has indices).
    void draw() const;

    fuhgl::gl::GLsizei vertexCount() const { return vertexCount_; }
    fuhgl::gl::GLsizei indexCount() const { return indexCount_; }
    int jointCount() const { return jointCount_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false;
    fuhgl::gl::GLsizei vertexCount_ = 0, indexCount_ = 0;
    int jointCount_ = 0;
    fuhgl::Buffer vbo_{fuhgl::Buffer::Target::Array};
    fuhgl::Buffer ibo_{fuhgl::Buffer::Target::ElementArray};
    fuhgl::Buffer joints_{fuhgl::Buffer::Target::Array};
    fuhgl::Buffer weights_{fuhgl::Buffer::Target::Array};
    fuhgl::Buffer palette_{fuhgl::Buffer::Target::ShaderStorage};
    fuhgl::VertexArray vao_;
};

} // namespace fuhglgraphics::anim
