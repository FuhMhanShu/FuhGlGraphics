#include "fuhglgraphics/anim/gpu_skinner.hpp"

#include <fuhgl/gl/info.hpp>
#include <fuhgl/gl/program.hpp>

#include <cstdio>

namespace fuhglgraphics::anim {

struct GpuSkinner::Impl {};

GpuSkinner::GpuSkinner() : impl_(std::make_unique<Impl>()) {}
GpuSkinner::~GpuSkinner() = default;
GpuSkinner::GpuSkinner(GpuSkinner&&) noexcept = default;
GpuSkinner& GpuSkinner::operator=(GpuSkinner&&) noexcept = default;

bool GpuSkinner::supported() {
    return fuhgl::gl::version().atLeast(4, 3);
}

bool GpuSkinner::create(const fuhgl::render::MeshData& base, const SkinWeights& skin) {
    if (!supported()) {
        std::fprintf(stderr, "fuhglgraphics::anim::GpuSkinner: needs GL >= 4.3 (SSBO) — CPU fallback\n");
        return false;
    }
    const std::size_t vcount = base.vertexCount();
    if (vcount == 0 || skin.joints.size() < vcount * 4 || skin.weights.size() < vcount * 4) {
        std::fprintf(stderr, "fuhglgraphics::anim::GpuSkinner: skin weights must have 4 joints + 4 "
                             "weights per vertex (%zu vertices)\n", vcount);
        return false;
    }
    if (!vao_.create()) return false;
    if (!vbo_.create(fuhgl::Buffer::Target::Array)) return false;
    vbo_.bind();
    vbo_.setData(static_cast<fuhgl::gl::GLsizeiptr>(base.vertices.size() * sizeof(float)),
                 base.vertices.data(), fuhgl::Buffer::Usage::StaticDraw);
    // joints as float4 (the skinning VS reads a vec4; avoids integer-attribute plumbing)
    std::vector<float> jointsF(vcount * 4);
    for (std::size_t i = 0; i < vcount * 4; ++i)
        jointsF[i] = static_cast<float>(skin.joints[i]);
    if (!joints_.create(fuhgl::Buffer::Target::Array)) return false;
    joints_.bind();
    joints_.setData(static_cast<fuhgl::gl::GLsizeiptr>(jointsF.size() * sizeof(float)), jointsF.data(),
                    fuhgl::Buffer::Usage::StaticDraw);
    if (!weights_.create(fuhgl::Buffer::Target::Array)) return false;
    weights_.bind();
    weights_.setData(static_cast<fuhgl::gl::GLsizeiptr>(skin.weights.size() * sizeof(float)),
                     skin.weights.data(), fuhgl::Buffer::Usage::StaticDraw);
    if (!base.indices.empty()) {
        if (!ibo_.create(fuhgl::Buffer::Target::ElementArray)) return false;
        ibo_.bind();
        ibo_.setData(static_cast<fuhgl::gl::GLsizeiptr>(base.indices.size() * sizeof(unsigned int)),
                     base.indices.data(), fuhgl::Buffer::Usage::StaticDraw);
    }
    if (!palette_.create(fuhgl::Buffer::Target::ShaderStorage)) return false;

    // VAO layout: standard 11-floats/vertex attribs 0..3 + joints(5) + weights(6)
    vao_.bind();
    vbo_.bind();
    constexpr fuhgl::gl::GLsizei kStride = fuhgl::render::MeshData::kVertexBytes;
    vao_.setAttrib(0, 3, fuhgl::gl::GL_FLOAT, false, kStride, 0);
    vao_.enableAttrib(0);
    vao_.setAttrib(1, 3, fuhgl::gl::GL_FLOAT, false, kStride, 3 * sizeof(float));
    vao_.enableAttrib(1);
    vao_.setAttrib(2, 3, fuhgl::gl::GL_FLOAT, false, kStride, 6 * sizeof(float));
    vao_.enableAttrib(2);
    vao_.setAttrib(3, 2, fuhgl::gl::GL_FLOAT, false, kStride, 9 * sizeof(float));
    vao_.enableAttrib(3);
    joints_.bind();
    vao_.setAttrib(5, 4, fuhgl::gl::GL_FLOAT, false, 4 * sizeof(float), 0);
    vao_.enableAttrib(5);
    weights_.bind();
    vao_.setAttrib(6, 4, fuhgl::gl::GL_FLOAT, false, 4 * sizeof(float), 0);
    vao_.enableAttrib(6);
    if (!base.indices.empty()) vao_.setElementBuffer(ibo_);
    fuhgl::VertexArray::unbind();

    vertexCount_ = static_cast<fuhgl::gl::GLsizei>(vcount);
    indexCount_ = static_cast<fuhgl::gl::GLsizei>(base.indices.size());
    jointCount_ = 0;
    for (std::size_t i = 0; i < vcount * 4; ++i)
        jointCount_ = std::max(jointCount_, static_cast<int>(skin.joints[i]) + 1);
    ready_ = true;
    return true;
}

void GpuSkinner::uploadPalette(const std::vector<fuhgl::math::Mat4>& palette) {
    if (!ready_) return;
    palette_.bind();
    palette_.setData(static_cast<fuhgl::gl::GLsizeiptr>(palette.size() * sizeof(fuhgl::math::Mat4)),
                     palette.data(), fuhgl::Buffer::Usage::DynamicDraw);
    palette_.bindBase(0);
}

void GpuSkinner::bindPalette() const {
    if (ready_) palette_.bindBase(0);
}

void GpuSkinner::draw() const {
    if (!ready_) return;
    vao_.bind();
    auto& f = fuhgl::gl::functions();
    if (indexCount_ > 0)
        f.glDrawElements(fuhgl::gl::GL_TRIANGLES, indexCount_, fuhgl::gl::GL_UNSIGNED_INT, nullptr);
    else
        f.glDrawArrays(fuhgl::gl::GL_TRIANGLES, 0, vertexCount_);
}

} // namespace fuhglgraphics::anim
