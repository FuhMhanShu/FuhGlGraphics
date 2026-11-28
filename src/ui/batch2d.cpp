// G41 2D UI layer: Batch2D implementation — batched quads/lines/text in one dynamic
// VBO, one GLSL 330 program, texture-segmented draw calls, scissor stack.

#include <fuhglgraphics/ui/batch2d.hpp>

#include <fuhgl/gl/state.hpp>
#include <fuhgl/render/text.hpp>

#include <cstdio>

namespace fuhglgraphics::ui {

namespace gl = fuhgl::gl;

namespace {

// Vertex layout: pos(2) uv(2) color(4) local(2) quadSize(2) radius(1) = 13 floats.
constexpr int kFloatsPerVertex = 13;
constexpr gl::GLsizei kStride = kFloatsPerVertex * 4;

const char* kVertSrc = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aLocal;
layout(location = 4) in vec2 aQuadSize;
layout(location = 5) in float aRadius;
uniform mat4 uProj;
out vec2 vUv;
out vec4 vColor;
out vec2 vLocal;
out vec2 vQuadSize;
out float vRadius;
void main() {
    vUv = aUv;
    vColor = aColor;
    vLocal = aLocal;
    vQuadSize = aQuadSize;
    vRadius = aRadius;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* kFragSrc = R"glsl(#version 330 core
in vec2 vUv;
in vec4 vColor;
in vec2 vLocal;
in vec2 vQuadSize;
in float vRadius;
uniform sampler2D uTex;
out vec4 frag;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - (b - r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    vec4 t = texture(uTex, vUv);
    float a = t.a * vColor.a;
    if (vRadius > 0.0) {
        vec2 p = (vLocal - 0.5) * vQuadSize;
        float d = sdRoundBox(p, vQuadSize * 0.5, vRadius);
        a *= clamp(-d + 0.5, 0.0, 1.0);
    }
    frag = vec4(vColor.rgb * t.rgb, a);
}
)glsl";

} // namespace

struct Batch2D::Impl {
    fuhgl::Program prog;
    fuhgl::VertexArray vao;
    fuhgl::Buffer vbo{fuhgl::Buffer::Target::Array};
    fuhgl::Texture fontAtlas{fuhgl::Texture::Target::Tex2D};
    fuhgl::Texture white{fuhgl::Texture::Target::Tex2D};

    std::vector<float> verts;              ///< 13 floats per vertex, 6 per quad
    struct Segment {
        const fuhgl::Texture* tex = nullptr; ///< nullptr = white
        int first = 0;
        int count = 0;
    };
    std::vector<Segment> segments;
    const fuhgl::Texture* curTex = nullptr;  ///< nullptr = white (segment texture)
    bool segOpen = false;                    ///< a segment is being filled

    std::vector<Rect> scissors;
    fuhgl::math::Mat4 proj{};
    int width = 0, height = 0;
    int maxQuads = 0;

    bool created = false;
    bool begun = false;
    bool savedBlend = false, savedDepth = false, savedScissor = false, savedCull = false;

    void pushVert(float px, float py, float u, float v, const fuhgl::math::Vec4& c,
                  float lx, float ly, float qw, float qh, float radius) {
        verts.insert(verts.end(), {px, py, u, v, c.x, c.y, c.z, c.w, lx, ly, qw, qh, radius});
    }

    /// Makes sure a segment for `tex` is open, flushing the queue on a texture change
    /// or when the vertex buffer is full.
    void ensureSegment(const fuhgl::Texture* tex) {
        if (segOpen && tex == curTex) return;
        flushSegs();
        curTex = tex;
        segments.push_back({tex, static_cast<int>(verts.size() / kFloatsPerVertex), 0});
        segOpen = true;
    }

    /// Queues one axis-aligned quad (6 verts).
    void pushQuad(Rect r, const fuhgl::math::Vec4& color, const fuhgl::Texture* tex, Rect uv, float radius) {
        if (r.empty()) return;
        if (verts.size() + 6 * kFloatsPerVertex > static_cast<std::size_t>(maxQuads) * 6 * kFloatsPerVertex)
            flushSegs();
        ensureSegment(tex);
        const float lx[6] = {0.f, 1.f, 1.f, 0.f, 1.f, 0.f};
        const float ly[6] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f};
        const float px[6] = {r.x, r.right(), r.right(), r.x, r.right(), r.x};
        const float py[6] = {r.y, r.y, r.bottom(), r.y, r.bottom(), r.bottom()};
        for (int i = 0; i < 6; ++i) {
            const float u = uv.x + lx[i] * uv.w;
            const float v = uv.y + ly[i] * uv.h;
            pushVert(px[i], py[i], u, v, color, lx[i], ly[i], r.w, r.h, radius);
        }
        segments.back().count += 6;
    }

    /// Uploads + draws all queued segments; clears the queue.
    void flushSegs() {
        if (verts.empty() || !begun) return;
        prog.use();
        prog.setUniform("uProj", proj);
        prog.setUniform("uTex", 0);
        vao.bind();
        vbo.bind();
        vbo.setSubData(0, static_cast<gl::GLsizeiptr>(verts.size() * 4), verts.data());
        for (const Segment& s : segments) {
            if (s.tex) s.tex->bind(gl::GL_TEXTURE0);
            else white.bind(gl::GL_TEXTURE0);
            gl::drawArrays(gl::Primitive::Triangles, s.first, s.count);
        }
        verts.clear();
        segments.clear();
        curTex = nullptr;
        segOpen = false;
    }

    void applyScissor() {
        if (scissors.empty()) {
            gl::disable(gl::Cap::ScissorTest);
            return;
        }
        const Rect& r = scissors.back();
        gl::enable(gl::Cap::ScissorTest);
        const int sx = static_cast<int>(r.x);
        const int sy = height - static_cast<int>(r.bottom());
        const int sw = static_cast<int>(r.w);
        const int sh = static_cast<int>(r.h);
        gl::scissor(sx, sy, sw > 0 ? sw : 0, sh > 0 ? sh : 0);
    }
};

Batch2D::Batch2D() : impl_(std::make_unique<Impl>()) {}
Batch2D::~Batch2D() = default;
Batch2D::Batch2D(Batch2D&&) noexcept = default;
Batch2D& Batch2D::operator=(Batch2D&&) noexcept = default;

bool Batch2D::create(int maxQuads) {
    Impl& im = *impl_;
    im.maxQuads = maxQuads;

    fuhgl::Shader vs(fuhgl::Shader::Type::Vertex);
    fuhgl::Shader fs(fuhgl::Shader::Type::Fragment);
    if (!vs.compile(kVertSrc) || !fs.compile(kFragSrc)) {
        std::fprintf(stderr, "ui::Batch2D: shader compile failed: %s | %s\n", vs.infoLog().c_str(), fs.infoLog().c_str());
        return false;
    }
    if (!im.prog.create()) return false;
    im.prog.attach(vs);
    im.prog.attach(fs);
    if (!im.prog.link()) {
        std::fprintf(stderr, "ui::Batch2D: link failed: %s\n", im.prog.infoLog().c_str());
        return false;
    }

    if (!im.vao.create() || !im.vbo.create(fuhgl::Buffer::Target::Array)) return false;
    im.vao.bind();
    im.vbo.bind();
    im.vbo.setData(static_cast<gl::GLsizeiptr>(maxQuads) * 6 * kStride, nullptr, fuhgl::Buffer::Usage::DynamicDraw);
    im.vao.setAttrib(0, 2, gl::GL_FLOAT, false, kStride, 0);
    im.vao.setAttrib(1, 2, gl::GL_FLOAT, false, kStride, 8);
    im.vao.setAttrib(2, 4, gl::GL_FLOAT, false, kStride, 16);
    im.vao.setAttrib(3, 2, gl::GL_FLOAT, false, kStride, 32);
    im.vao.setAttrib(4, 2, gl::GL_FLOAT, false, kStride, 40);
    im.vao.setAttrib(5, 1, gl::GL_FLOAT, false, kStride, 48);
    for (gl::GLuint i = 0; i < 6; ++i) im.vao.enableAttrib(i);
    fuhgl::VertexArray::unbind();

    // 1x1 white texture (solid quads).
    if (!im.white.create(fuhgl::Texture::Target::Tex2D)) return false;
    const unsigned char whitePx[4] = {255, 255, 255, 255};
    im.white.bind();
    im.white.setImage2D(0, gl::GL_RGBA8, 1, 1, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, whitePx);
    im.white.setParameteri(gl::GL_TEXTURE_MIN_FILTER, gl::GL_NEAREST);
    im.white.setParameteri(gl::GL_TEXTURE_MAG_FILTER, gl::GL_NEAREST);
    im.white.setParameteri(gl::GL_TEXTURE_WRAP_S, gl::GL_CLAMP_TO_EDGE);
    im.white.setParameteri(gl::GL_TEXTURE_WRAP_T, gl::GL_CLAMP_TO_EDGE);

    // Embedded 8x8 font atlas: 128 glyphs * 8 px = 1024 x 8, RGBA (rgb = white, a = coverage).
    constexpr int kGlyphs = fuhgl::render::kFontGlyphCount;
    constexpr int kCell = fuhgl::render::kFontGlyphSize;
    constexpr int kAtlasW = kGlyphs * kCell;
    constexpr int kAtlasH = kCell;
    std::vector<unsigned char> atlas(static_cast<std::size_t>(kAtlasW) * kAtlasH * 4, 0);
    for (int g = 0; g < kGlyphs; ++g) {
        const std::uint8_t* rows = fuhgl::render::font8x8Glyph(static_cast<unsigned char>(g));
        for (int py = 0; py < kAtlasH; ++py) {
            for (int px = 0; px < kCell; ++px) {
                const bool on = (rows[py] >> px) & 1;
                unsigned char* p = atlas.data() + (static_cast<std::size_t>(py) * kAtlasW + g * kCell + px) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = on ? 255 : 0;
            }
        }
    }
    if (!im.fontAtlas.create(fuhgl::Texture::Target::Tex2D)) return false;
    im.fontAtlas.bind();
    im.fontAtlas.setImage2D(0, gl::GL_RGBA8, kAtlasW, kAtlasH, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, atlas.data());
    im.fontAtlas.setParameteri(gl::GL_TEXTURE_MIN_FILTER, gl::GL_LINEAR);
    im.fontAtlas.setParameteri(gl::GL_TEXTURE_MAG_FILTER, gl::GL_LINEAR);
    im.fontAtlas.setParameteri(gl::GL_TEXTURE_WRAP_S, gl::GL_CLAMP_TO_EDGE);
    im.fontAtlas.setParameteri(gl::GL_TEXTURE_WRAP_T, gl::GL_CLAMP_TO_EDGE);

    im.created = true;
    return true;
}

bool Batch2D::isCreated() const { return impl_->created; }

void Batch2D::begin(int width, int height) {
    Impl& im = *impl_;
    im.width = width;
    im.height = height;
    im.proj = fuhgl::math::ortho(0.f, static_cast<float>(width), static_cast<float>(height), 0.f, -1.f, 1.f);
    im.verts.clear();
    im.segments.clear();
    im.curTex = nullptr;
    im.segOpen = false;
    im.scissors.clear();

    im.savedBlend = gl::isEnabled(gl::Cap::Blend);
    im.savedDepth = gl::isEnabled(gl::Cap::DepthTest);
    im.savedScissor = gl::isEnabled(gl::Cap::ScissorTest);
    im.savedCull = gl::isEnabled(gl::Cap::CullFace);

    gl::viewport(0, 0, width, height);
    gl::disable(gl::Cap::DepthTest);
    gl::disable(gl::Cap::CullFace);
    gl::enable(gl::Cap::Blend);
    gl::blendFunc(gl::BlendFactor::SrcAlpha, gl::BlendFactor::OneMinusSrcAlpha);
    im.begun = true;
}

void Batch2D::end() {
    Impl& im = *impl_;
    flush();
    if (im.savedBlend) gl::enable(gl::Cap::Blend);
    else gl::disable(gl::Cap::Blend);
    if (im.savedDepth) gl::enable(gl::Cap::DepthTest);
    else gl::disable(gl::Cap::DepthTest);
    if (im.savedScissor) gl::enable(gl::Cap::ScissorTest);
    else gl::disable(gl::Cap::ScissorTest);
    if (im.savedCull) gl::enable(gl::Cap::CullFace);
    else gl::disable(gl::Cap::CullFace);
    fuhgl::VertexArray::unbind();
    im.begun = false;
}

void Batch2D::fillRect(Rect r, const fuhgl::math::Vec4& color, const fuhgl::Texture* tex, Rect uv, float cornerRadius) {
    impl_->pushQuad(r, color, tex, uv, cornerRadius);
}

void Batch2D::outlineRect(Rect r, const fuhgl::math::Vec4& color, float thickness) {
    Impl& im = *impl_;
    im.pushQuad({r.x, r.y, r.w, thickness}, color, nullptr, {}, 0.f);
    im.pushQuad({r.x, r.bottom() - thickness, r.w, thickness}, color, nullptr, {}, 0.f);
    im.pushQuad({r.x, r.y + thickness, thickness, r.h - 2.f * thickness}, color, nullptr, {}, 0.f);
    im.pushQuad({r.right() - thickness, r.y + thickness, thickness, r.h - 2.f * thickness}, color, nullptr, {}, 0.f);
}

void Batch2D::line(float x1, float y1, float x2, float y2, const fuhgl::math::Vec4& color, float thickness) {
    Impl& im = *impl_;
    const float dx = x2 - x1, dy = y2 - y1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) return;
    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;
    if (im.verts.size() + 6 * kFloatsPerVertex > static_cast<std::size_t>(im.maxQuads) * 6 * kFloatsPerVertex)
        im.flushSegs();
    im.ensureSegment(nullptr);
    const float cx[6] = {x1 - nx, x2 - nx, x2 + nx, x1 - nx, x2 + nx, x1 + nx};
    const float cy[6] = {y1 - ny, y2 - ny, y2 + ny, y1 - ny, y2 + ny, y1 + ny};
    const float lx[6] = {0.f, 1.f, 1.f, 0.f, 1.f, 0.f};
    const float ly[6] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f};
    for (int i = 0; i < 6; ++i)
        im.pushVert(cx[i], cy[i], 0.f, 0.f, color, lx[i], ly[i], len, thickness, 0.f);
    im.segments.back().count += 6;
}

void Batch2D::text(const Font& font, const std::string& s, float x, float y, const fuhgl::math::Vec4& color) {
    Impl& im = *impl_;
    const float cell = font.cell();
    constexpr float kInvAtlasW = 1.f / (fuhgl::render::kFontGlyphCount * fuhgl::render::kFontGlyphSize);
    float cx = x;
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch) & 0x7F;
        if (c >= 32) {
            const Rect uv{c * fuhgl::render::kFontGlyphSize * kInvAtlasW, 0.f,
                          fuhgl::render::kFontGlyphSize * kInvAtlasW, 1.f};
            im.pushQuad({cx, y, cell, cell}, color, &im.fontAtlas, uv, 0.f);
        }
        cx += cell;
    }
}

void Batch2D::pushScissor(Rect r) {
    Impl& im = *impl_;
    const Rect cur = im.scissors.empty() ? Rect{0.f, 0.f, static_cast<float>(im.width), static_cast<float>(im.height)}
                                         : im.scissors.back();
    im.scissors.push_back(cur.intersected(r));
    im.applyScissor();
}

void Batch2D::popScissor() {
    Impl& im = *impl_;
    if (!im.scissors.empty()) im.scissors.pop_back();
    im.applyScissor();
}

void Batch2D::flush() { impl_->flushSegs(); }

const fuhgl::Texture* Batch2D::fontAtlas() const { return impl_->created ? &impl_->fontAtlas : nullptr; }

} // namespace fuhglgraphics::ui
