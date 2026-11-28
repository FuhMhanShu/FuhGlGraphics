// G41 2D UI layer: DeferredRenderer integration — overlay post pass + WorldSurface.

#include <fuhglgraphics/ui/overlay.hpp>

#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/node.hpp>

#include <fuhgl/gl/state.hpp>
#include <fuhgl/gl/vao.hpp>
#include <fuhgl/render/graph.hpp>

#include <cstdio>

namespace fuhglgraphics::ui {

namespace {

// Fullscreen-triangle blit of the texture bound at unit 0 (the pass's "@chain" input).
const char* kBlitVert = R"glsl(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";
const char* kBlitFrag = R"glsl(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 frag;
void main() { frag = texture(uTex, vUv); }
)glsl";

struct Blitter {
    fuhgl::Program prog;
    fuhgl::VertexArray vao; // empty VAO: the vertex shader only uses gl_VertexID
    bool created = false;

    bool ensure() {
        if (created) return true;
        fuhgl::Shader vs(fuhgl::Shader::Type::Vertex);
        fuhgl::Shader fs(fuhgl::Shader::Type::Fragment);
        if (!vs.compile(kBlitVert) || !fs.compile(kBlitFrag)) {
            std::fprintf(stderr, "ui: blit shader compile failed: %s | %s\n", vs.infoLog().c_str(), fs.infoLog().c_str());
            return false;
        }
        if (!prog.create()) return false;
        prog.attach(vs);
        prog.attach(fs);
        if (!prog.link()) {
            std::fprintf(stderr, "ui: blit link failed: %s\n", prog.infoLog().c_str());
            return false;
        }
        created = vao.create();
        return created;
    }
    void draw() const {
        prog.use();
        prog.setUniform("uTex", 0);
        vao.bind();
        fuhgl::gl::drawArrays(fuhgl::gl::Primitive::Triangles, 0, 3);
    }
};

} // namespace

render::DeferredRenderer::PostPass makeOverlayPass(UiRoot& ui, const std::string& name) {
    static Blitter blitter; // shared; GL objects are created lazily inside the pass (GL context current)
    render::DeferredRenderer::PostPass pass;
    pass.name = name;
    pass.inputs = {"@chain"};
    pass.output = name + "Out";
    pass.atOutputSize = true;
    pass.fn = [&ui](fuhgl::render::RenderGraph::PassContext& ctx) {
        // copy the previous chain output (bound at unit 0) into this pass's FBO ...
        const bool blend = fuhgl::gl::isEnabled(fuhgl::gl::Cap::Blend);
        fuhgl::gl::disable(fuhgl::gl::Cap::Blend);
        if (blitter.ensure()) blitter.draw();
        if (blend) fuhgl::gl::enable(fuhgl::gl::Cap::Blend);
        // ... then draw the UI over it (batch.begin re-enables blending itself)
        ui.render(ctx.width, ctx.height);
    };
    return pass;
}

WorldSurface::WorldSurface() = default;
WorldSurface::~WorldSurface() = default;

bool WorldSurface::create(int width, int height) {
    if (!tex_.create(fuhgl::Texture::Target::Tex2D)) return false;
    tex_.bind();
    tex_.setImage2D(0, fuhgl::gl::GL_RGBA8, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, nullptr);
    tex_.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    tex_.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    tex_.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    tex_.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    if (!fbo_.create()) return false;
    fbo_.bind();
    fbo_.attachTexture2D(fuhgl::Framebuffer::Attachment::Color0, tex_, 0);
    if (!fbo_.isComplete()) {
        std::fprintf(stderr, "ui::WorldSurface: FBO incomplete\n");
        return false;
    }
    fuhgl::Framebuffer::unbind();
    width_ = width;
    height_ = height;
    created_ = true;
    return true;
}

void WorldSurface::resize(int width, int height) {
    if (!created_) return;
    tex_.bind();
    tex_.setImage2D(0, fuhgl::gl::GL_RGBA8, width, height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, nullptr);
    width_ = width;
    height_ = height;
}

void WorldSurface::render(UiRoot& ui) {
    if (!created_) return;
    fbo_.bind();
    fuhgl::gl::viewport(0, 0, width_, height_);
    fuhgl::gl::clearColor(0.f, 0.f, 0.f, 0.f);
    fuhgl::gl::clear(fuhgl::gl::ClearBit::Color);
    ui.render(width_, height_);
    fuhgl::Framebuffer::unbind();
}

bool WorldSurface::pickUv(scene::Scene& scene, const fuhgl::Camera& cam, int viewportW, int viewportH,
                          float px, float py, const scene::Node* surfaceNode, float& u, float& v) {
    if (!surfaceNode) return false;
    scene::PickHit hit;
    if (!scene::pick(scene, &cam, viewportW, viewportH, px, py, hit) || !hit.node || hit.meshEntry < 0) return false;
    // only hits on the surface node's subtree map onto the UI
    bool inSubtree = false;
    for (const scene::Node* n = hit.node; n; n = n->parent())
        if (n == surfaceNode) inSubtree = true;
    if (!inSubtree) return false;

    const auto* mc = hit.node->find<scene::MeshComponent>();
    if (!mc || static_cast<std::size_t>(hit.meshEntry) >= mc->meshes.size()) return false;
    const scene::MeshEntry& entry = mc->meshes[static_cast<std::size_t>(hit.meshEntry)];
    const fuhgl::render::MeshData* md = entry.meshData;
    if (!md || md->vertices.empty()) return false;

    const std::size_t triCount = !md->indices.empty() ? md->indices.size() / 3 : md->vertexCount() / 3;
    if (hit.triangle >= triCount) return false;
    std::size_t i0, i1, i2;
    if (!md->indices.empty()) {
        i0 = md->indices[hit.triangle * 3];
        i1 = md->indices[hit.triangle * 3 + 1];
        i2 = md->indices[hit.triangle * 3 + 2];
    } else {
        i0 = hit.triangle * 3;
        i1 = i0 + 1;
        i2 = i0 + 2;
    }
    // re-run the triangle test in world space to recover the barycentric (u, v)
    const fuhgl::math::Mat4 m = hit.node->worldMatrix() * entry.localOffset;
    auto vertPos = [&](std::size_t vi) {
        const std::size_t o = vi * fuhgl::render::MeshData::kFloatsPerVertex;
        const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{md->vertices[o], md->vertices[o + 1], md->vertices[o + 2], 1.f};
        return fuhgl::math::Vec3{p.x, p.y, p.z};
    };
    const float aspect = viewportH > 0 ? static_cast<float>(viewportW) / static_cast<float>(viewportH) : 1.f;
    const scene::Ray ray = scene::rayFromScreen(cam, aspect, viewportW, viewportH, px, py);
    float t = 0.f, bu = 0.f, bv = 0.f;
    if (!scene::rayIntersectTriangle(ray, vertPos(i0), vertPos(i1), vertPos(i2), t, bu, bv)) return false;
    // uv lives at float offset 9 of the 11-float vertex (pos3 normal3 color3 uv2)
    constexpr int kUvOffset = 9;
    const auto uvAt = [&](std::size_t vi) {
        return fuhgl::math::Vec2{md->vertices[vi * fuhgl::render::MeshData::kFloatsPerVertex + kUvOffset],
                                 md->vertices[vi * fuhgl::render::MeshData::kFloatsPerVertex + kUvOffset + 1]};
    };
    const fuhgl::math::Vec2 uv0 = uvAt(i0), uv1 = uvAt(i1), uv2 = uvAt(i2);
    const float w0 = 1.f - bu - bv;
    u = w0 * uv0.x + bu * uv1.x + bv * uv2.x;
    v = w0 * uv0.y + bu * uv1.y + bv * uv2.y;
    return true;
}

} // namespace fuhglgraphics::ui
