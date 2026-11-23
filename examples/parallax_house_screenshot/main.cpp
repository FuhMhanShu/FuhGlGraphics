// Parallax-mapping (POM) showcase: a brick house in bright sunshine. The facade is a single
// box whose UVs are scaled so one brick-texture tile covers kTileWorld meters; the tile is
// generated procedurally on the CPU in three channels: albedo (per-brick tint + mortar),
// height map (recessed mortar grooves + beveled brick faces) and a Sobel normal map. The G3
// G-buffer pass marches the view ray through the height layers (parallax occlusion mapping,
// MeshEntry::heightMap/parallaxScale/parallaxLayers) and samples albedo+normal at the offset
// UV \u2014 the grooves visibly shift at grazing angles (see the "wall" vs "wall_nopom" shots).
// Scene: brick body + pyramid shingle roof + chimney, door, windows (glass + frames), stone
// foundation + path, lawn, bushes, a tree; blue sky cubemap with clouds + sun glow, IBL, sun
// with CSM shadows, SSAO/SSR/bloom/god rays, TAA + upscale + ACES. Headless to PNG.
//
// Usage: fuhglgraphics_parallax_house_screenshot --prefix parallax_house [--width 1280]
//          [--height 720] [--frames 16] [--scale 1.0] [--no-parallax]
//        writes <prefix>_wide.png, <prefix>_wall.png, <prefix>_wall_nopom.png
//        (--no-parallax renders all shots without the height map).
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>

#include "../screenshot/png_writer.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kTileWorld = 2.4f; // one brick texture tile covers 2.4 m on the wall
constexpr int kBrickCols = 8, kBrickRows = 8; // bricks per tile -> 0.3 m bricks
constexpr int kBrickTex = 512;

struct Args {
    std::string prefix = "parallax_house";
    int width = 1280, height = 720, frames = 16;
    float scale = 1.f;
    bool parallax = true;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) a.prefix = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) a.scale = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--no-parallax") == 0) a.parallax = false;
    }
    return a;
}

unsigned hash2(int x, int y) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Brick layout shared by albedo/height/normal: offset rows by half a brick, recessed mortar
// grooves, slightly beveled brick faces (height ramps up toward the brick center).
struct BrickInfo {
    bool mortar;
    float height; // 0..1
    fuhgl::math::Vec3 albedo;
};
BrickInfo brickAt(float u, float v, float pxNoise) {
    const float bx = u * kBrickCols, by = v * kBrickRows;
    const int row = static_cast<int>(by);
    const float fx = bx - 0.5f * (row % 2);
    const int col = static_cast<int>(std::floor(fx));
    const float lx = fx - std::floor(fx), ly = by - row;
    const float m = 0.10f; // mortar thickness in brick-local units
    BrickInfo b{};
    if (lx < m || lx > 1.f - m || ly < m || ly > 1.f - m) {
        b.mortar = true;
        b.height = 0.08f;
        b.albedo = {0.55f, 0.53f, 0.50f};
    } else {
        b.mortar = false;
        const unsigned h = hash2(col, row);
        const float tint = 0.72f + 0.28f * ((h & 0xffu) / 255.f);
        b.albedo = {0.66f * tint, 0.30f * tint, 0.21f * tint};
        b.albedo.x *= 0.92f + 0.16f * pxNoise;
        b.albedo.y *= 0.92f + 0.16f * pxNoise;
        // bevel: height ramps from 0.55 at the brick edge to ~1 at the center
        const float ex = std::min(lx - m, 1.f - m - lx) / (0.5f - m);
        const float ey = std::min(ly - m, 1.f - m - ly) / (0.5f - m);
        const float bevel = clamp01(std::min(ex, ey) * 2.2f);
        b.height = 0.55f + 0.45f * bevel * (0.85f + 0.15f * (((h >> 8) & 0xffu) / 255.f));
    }
    return b;
}

// RGBA8 texture from a per-pixel lambda, REPEAT wrap + mipmaps (POM offset UVs must wrap).
fuhgl::Texture makeTex(int size, const std::function<void(int, int, unsigned char*)>& px) {
    std::vector<unsigned char> data(static_cast<std::size_t>(size) * size * 4, 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            px(x, y, &data[(static_cast<std::size_t>(y) * size + x) * 4]);
    fuhgl::Texture t(fuhgl::Texture::Target::Tex2D);
    t.bind();
    t.setImage2D(0, fuhgl::gl::GL_RGBA8, size, size, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                 data.data());
    t.generateMipmap();
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR_MIPMAP_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_REPEAT);
    t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_REPEAT);
    return t;
}

fuhgl::Texture makeBrickAlbedo() {
    return makeTex(kBrickTex, [](int x, int y, unsigned char* o) {
        const float n = (hash2(x, y) & 0xffu) / 255.f - 0.5f;
        const BrickInfo b = brickAt(static_cast<float>(x) / kBrickTex, static_cast<float>(y) / kBrickTex,
                                    n + 0.5f);
        o[0] = static_cast<unsigned char>(clamp01(b.albedo.x + n * 0.12f) * 255.f);
        o[1] = static_cast<unsigned char>(clamp01(b.albedo.y + n * 0.12f) * 255.f);
        o[2] = static_cast<unsigned char>(clamp01(b.albedo.z + n * 0.12f) * 255.f);
        o[3] = 255;
    });
}

// Grayscale height in the red channel (the G-buffer POM block samples .r).
fuhgl::Texture makeBrickHeight() {
    return makeTex(kBrickTex, [](int x, int y, unsigned char* o) {
        const float h = brickAt(static_cast<float>(x) / kBrickTex, static_cast<float>(y) / kBrickTex, 0.f).height;
        const unsigned char hv = static_cast<unsigned char>(h * 255.f);
        o[0] = hv;
        o[1] = hv;
        o[2] = hv;
        o[3] = 255;
    });
}

// Sobel normal map from the same height field (tangent space, +Z out).
fuhgl::Texture makeBrickNormal() {
    std::vector<float> h(static_cast<std::size_t>(kBrickTex) * kBrickTex);
    for (int y = 0; y < kBrickTex; ++y)
        for (int x = 0; x < kBrickTex; ++x)
            h[static_cast<std::size_t>(y) * kBrickTex + x] =
                brickAt(static_cast<float>(x) / kBrickTex, static_cast<float>(y) / kBrickTex, 0.f).height;
    const float strength = 2.2f;
    return makeTex(kBrickTex, [&](int x, int y, unsigned char* o) {
        const auto at = [&](int xx, int yy) {
            return h[static_cast<std::size_t>((yy + kBrickTex) % kBrickTex) * kBrickTex +
                     ((xx + kBrickTex) % kBrickTex)];
        };
        const float dx = (at(x + 1, y) - at(x - 1, y)) * strength;
        const float dy = (at(x, y + 1) - at(x, y - 1)) * strength;
        const fuhgl::math::Vec3 n = fuhgl::math::normalize({-dx, -dy, 1.f});
        o[0] = static_cast<unsigned char>((n.x * 0.5f + 0.5f) * 255.f);
        o[1] = static_cast<unsigned char>((n.y * 0.5f + 0.5f) * 255.f);
        o[2] = static_cast<unsigned char>((n.z * 0.5f + 0.5f) * 255.f);
        o[3] = 255;
    });
}

// Dark red shingle rows for the pyramid roof.
fuhgl::Texture makeShingleTex() {
    constexpr int S = 256;
    return makeTex(S, [](int x, int y, unsigned char* o) {
        const float u = static_cast<float>(x) / S, v = static_cast<float>(y) / S;
        const int row = static_cast<int>(v * 10.f);
        const float sx = u * 5.f - 0.5f * (row % 2); // staggered tabs
        const int col = static_cast<int>(std::floor(sx));
        const float lx = sx - std::floor(sx), ly = v * 10.f - row;
        const float gap = (lx < 0.08f || ly > 0.86f) ? 0.45f : 1.f;
        const unsigned hsh = hash2(col, row);
        const float tint = (0.75f + 0.25f * ((hsh & 0xffu) / 255.f)) * gap;
        const float n = (hash2(x, y) & 0xffu) / 255.f * 0.08f;
        o[0] = static_cast<unsigned char>(clamp01((0.30f * tint + n)) * 255.f);
        o[1] = static_cast<unsigned char>(clamp01((0.11f * tint + n * 0.5f)) * 255.f);
        o[2] = static_cast<unsigned char>(clamp01((0.085f * tint + n * 0.4f)) * 255.f);
        o[3] = 255;
    });
}

// Gray paving stones (foundation + path): voronoi-ish cells via brick-ish grid + fbm jitter.
fuhgl::Texture makeStoneTex() {
    constexpr int S = 256;
    return makeTex(S, [](int x, int y, unsigned char* o) {
        const float u = static_cast<float>(x) / S * 6.f, v = static_cast<float>(y) / S * 6.f;
        const int cx = static_cast<int>(u), cy = static_cast<int>(v);
        const float fx = u - cx, fy = v - cy;
        const float edge = std::min(std::min(fx, 1.f - fx), std::min(fy, 1.f - fy));
        const float joint = edge < 0.06f ? 0.55f : 1.f;
        const unsigned hsh = hash2(cx, cy);
        const float tint = 0.7f + 0.3f * ((hsh & 0xffu) / 255.f);
        const float n = fuhgl::math::fbm3(u * 3.f, v * 3.f, 1.7f, 3, 9u);
        const float g = clamp01(0.62f * tint * joint + (n - 0.5f) * 0.25f);
        o[0] = static_cast<unsigned char>(g * 255.f);
        o[1] = static_cast<unsigned char>(g * 252.f);
        o[2] = static_cast<unsigned char>(g * 244.f);
        o[3] = 255;
    });
}

// Lawn: green fbm noise with a few lighter blades specks.
fuhgl::Texture makeGrassTex() {
    constexpr int S = 256;
    return makeTex(S, [](int x, int y, unsigned char* o) {
        const float u = static_cast<float>(x) / S, v = static_cast<float>(y) / S;
        const float n = fuhgl::math::fbm3(u * 22.f, v * 22.f, 4.2f, 4, 5u);
        const float blade = fuhgl::math::fbm3(u * 90.f, v * 90.f, 9.9f, 2, 6u);
        const float g = clamp01(0.16f + n * 0.22f + blade * 0.08f);
        o[0] = static_cast<unsigned char>(g * 0.42f * 255.f);
        o[1] = static_cast<unsigned char>(g * 255.f);
        o[2] = static_cast<unsigned char>(g * 0.38f * 255.f);
        o[3] = 255;
    });
}

// Wood grain for the door: vertical streaks.
fuhgl::Texture makeWoodTex() {
    constexpr int S = 256;
    return makeTex(S, [](int x, int y, unsigned char* o) {
        const float u = static_cast<float>(x) / S, v = static_cast<float>(y) / S;
        const float grain = fuhgl::math::fbm3(u * 34.f + std::sin(v * 40.f) * 0.35f, v * 6.f, 2.2f, 4, 11u);
        const float panel = (std::fmod(v * 3.f, 1.f) > 0.94f) ? 0.8f : 1.f;
        const float b = clamp01(0.30f + grain * 0.28f) * panel;
        o[0] = static_cast<unsigned char>(b * 1.35f * 255.f);
        o[1] = static_cast<unsigned char>(b * 0.85f * 255.f);
        o[2] = static_cast<unsigned char>(b * 0.55f * 255.f);
        o[3] = 255;
    });
}

// Blue summer-sky cubemap: gradient + fbm clouds + HDR sun glow at sunDir (for bloom/rays/IBL).
fuhgl::Texture makeDaySky(int size, unsigned seed, const fuhgl::math::Vec3& sunDir) {
    fuhgl::Texture cube;
    if (!cube.create(fuhgl::Texture::Target::Cube)) return cube;
    cube.bind();
    const fuhgl::Texture::CubeFace order[6] = {
        fuhgl::Texture::CubeFace::PosX, fuhgl::Texture::CubeFace::NegX, fuhgl::Texture::CubeFace::PosY,
        fuhgl::Texture::CubeFace::NegY, fuhgl::Texture::CubeFace::PosZ, fuhgl::Texture::CubeFace::NegZ,
    };
    const fuhgl::math::Vec3 zenith{0.16f, 0.40f, 0.85f};
    const fuhgl::math::Vec3 horizon{0.72f, 0.83f, 0.95f};
    const fuhgl::math::Vec3 ground{0.32f, 0.38f, 0.28f};
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < size; ++j) {
            for (int i = 0; i < size; ++i) {
                const float u = (2.f * i / (size - 1) - 1.f), v = (2.f * j / (size - 1) - 1.f);
                fuhgl::math::Vec3 d;
                switch (face) {
                    case 0: d = {1.f, -v, -u}; break;
                    case 1: d = {-1.f, -v, u}; break;
                    case 2: d = {u, 1.f, v}; break;
                    case 3: d = {u, -1.f, -v}; break;
                    case 4: d = {u, -v, 1.f}; break;
                    default: d = {-u, -v, -1.f}; break;
                }
                d = fuhgl::math::normalize(d);
                const float sunAmt = std::max(fuhgl::math::dot(d, sunDir), 0.f);
                fuhgl::math::Vec3 col =
                    fuhgl::math::lerp(horizon, zenith, std::pow(clamp01((d.y + 0.08f) / 0.9f), 0.65f));
                if (d.y < 0.f) col = fuhgl::math::lerp(col, ground, std::min(1.f, -d.y * 3.f));
                // clouds: fbm streaks, only above the horizon
                const float n = fuhgl::math::fbm3(d.x * 3.f + 1.3f, d.y * 6.f, d.z * 3.f, 4, seed);
                const float cl = clamp01((n - 0.64f) * 5.f) * clamp01((d.y + 0.02f) * 8.f);
                col = fuhgl::math::lerp(col, {0.98f, 0.99f, 1.0f}, cl * 0.75f);
                // sun glow (HDR: blooms in the post chain)
                col += fuhgl::math::Vec3{1.0f, 0.9f, 0.7f} * std::pow(sunAmt, 700.f) * 12.f;
                col += fuhgl::math::Vec3{1.0f, 0.85f, 0.6f} * std::pow(sunAmt, 28.f) * 0.6f;
                unsigned char* p = px.data() + (static_cast<std::size_t>(j) * size + i) * 4;
                p[0] = static_cast<unsigned char>(std::min(255.f, col.x * 255.f));
                p[1] = static_cast<unsigned char>(std::min(255.f, col.y * 255.f));
                p[2] = static_cast<unsigned char>(std::min(255.f, col.z * 255.f));
                p[3] = 255;
            }
        }
        cube.setImageCubeFace(order[face], 0, fuhgl::gl::GL_RGBA8, size, fuhgl::gl::GL_RGBA,
                              fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
    }
    // no mipmaps uploaded -> LINEAR min filter keeps the cube map texture complete
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
    cube.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_R, fuhgl::gl::GL_CLAMP_TO_EDGE);
    return cube;
}

// boxData with the per-face UVs scaled (tile repeats every `uvScale` across a face).
fuhgl::render::MeshData scaledBoxData(const fuhgl::math::Vec3& halfExtents, const fuhgl::math::Vec3& color,
                                      const fuhgl::math::Vec2& uvScale) {
    fuhgl::render::MeshData d = fuhgl::render::shapes::boxData(halfExtents, color);
    for (std::size_t v = 0; v < d.vertexCount(); ++v) {
        d.vertices[v * fuhgl::render::MeshData::kFloatsPerVertex + 9] *= uvScale.x;
        d.vertices[v * fuhgl::render::MeshData::kFloatsPerVertex + 10] *= uvScale.y;
    }
    return d;
}

fuhgl::render::MeshData scaledQuadData(float halfSize, const fuhgl::math::Vec3& color,
                                       const fuhgl::math::Vec2& uvScale) {
    fuhgl::render::MeshData d = fuhgl::render::shapes::quadData(halfSize, color);
    for (std::size_t v = 0; v < d.vertexCount(); ++v) {
        d.vertices[v * fuhgl::render::MeshData::kFloatsPerVertex + 9] *= uvScale.x;
        d.vertices[v * fuhgl::render::MeshData::kFloatsPerVertex + 10] *= uvScale.y;
    }
    return d;
}

// Pyramid roof (4 side triangles + bottom quad), per-face UVs scaled, outward CCW winding.
fuhgl::render::MeshData pyramidData(float halfBase, float height, const fuhgl::math::Vec3& color,
                                    float uvScale) {
    using fuhgl::math::Vec3;
    fuhgl::render::MeshData d;
    const Vec3 c[4] = {{halfBase, 0.f, halfBase},
                       {-halfBase, 0.f, halfBase},
                       {-halfBase, 0.f, -halfBase},
                       {halfBase, 0.f, -halfBase}};
    const Vec3 apex{0.f, height, 0.f};
    const auto pushVert = [&](const Vec3& p, const Vec3& n, float u, float v) {
        const float vert[fuhgl::render::MeshData::kFloatsPerVertex] = {p.x,  p.y,  p.z,  n.x, n.y,
                                                                       n.z,  color.x, color.y, color.z, u, v};
        d.vertices.insert(d.vertices.end(), vert, vert + fuhgl::render::MeshData::kFloatsPerVertex);
    };
    const auto pushTri = [&](Vec3 a, Vec3 b, Vec3 c2, float u0, float v0, float u1, float v1, float u2,
                             float v2) {
        Vec3 n = fuhgl::math::normalize(fuhgl::math::cross(b - a, c2 - a));
        const Vec3 mid = (a + b + c2) * (1.f / 3.f);
        const Vec3 out{mid.x, 0.f, mid.z};
        if (fuhgl::math::dot(n, out) < 0.f) {
            std::swap(b, c2);
            std::swap(u1, u2);
            std::swap(v1, v2);
            n = -n;
        }
        const unsigned base = static_cast<unsigned>(d.vertexCount());
        pushVert(a, n, u0, v0);
        pushVert(b, n, u1, v1);
        pushVert(c2, n, u2, v2);
        d.indices.insert(d.indices.end(), {base, base + 1, base + 2});
    };
    for (int i = 0; i < 4; ++i) // side faces: base edge u 0..uvScale at v=0, apex at mid u
        pushTri(c[i], c[(i + 1) % 4], apex, 0.f, 0.f, uvScale, 0.f, uvScale * 0.5f, uvScale);
    // bottom quad with an explicit downward normal (CCW seen from below)
    const Vec3 down{0.f, -1.f, 0.f};
    {
        const unsigned base = static_cast<unsigned>(d.vertexCount());
        pushVert(c[2], down, 0.f, 0.f);
        pushVert(c[1], down, uvScale, 0.f);
        pushVert(c[0], down, uvScale, uvScale);
        pushVert(c[3], down, 0.f, uvScale);
        d.indices.insert(d.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    return d;
}

void aimAt(fuhgl::FirstPersonCamera& cam, const fuhgl::math::Vec3& target) {
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
}

} // namespace

int main(int argc, char** argv) {
    using namespace fuhglgraphics;
    const Args args = parseArgs(argc, argv);
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_parallax_house_screenshot", 64, 64)) {
        std::fprintf(stderr, "parallax_house_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "parallax_house_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- procedural textures ---
    fuhgl::Texture brickAlbedo = makeBrickAlbedo();
    fuhgl::Texture brickHeight = makeBrickHeight();
    fuhgl::Texture brickNormal = makeBrickNormal();
    fuhgl::Texture shingleTex = makeShingleTex();
    fuhgl::Texture stoneTex = makeStoneTex();
    fuhgl::Texture grassTex = makeGrassTex();
    fuhgl::Texture woodTex = makeWoodTex();

    // --- meshes ---
    fuhgl::render::Mesh ground, foundation, walls, roof, chimney, chimneyCap, boxFrame, door, glass,
        glassSide, path, bush, trunk, foliage;
    ground.setMeshData(scaledQuadData(30.f, {1, 1, 1}, {60.f / 4.f, 60.f / 4.f}));
    foundation.setMeshData(scaledBoxData({3.15f, 0.25f, 3.15f}, {1, 1, 1}, {6.3f / 1.2f, 0.5f / 1.2f}));
    walls.setMeshData(scaledBoxData({3.0f, 1.6f, 3.0f}, {1, 1, 1}, {6.f / kTileWorld, 3.2f / kTileWorld}));
    roof.setMeshData(pyramidData(3.6f, 2.8f, {1, 1, 1}, 3.2f));
    chimney.setMeshData(scaledBoxData({0.35f, 1.1f, 0.35f}, {1, 1, 1}, {0.7f / kTileWorld, 2.2f / kTileWorld}));
    chimneyCap.setMeshData(scaledBoxData({0.46f, 0.09f, 0.46f}, {1, 1, 1}, {0.92f / 1.2f, 0.92f / 1.2f}));
    boxFrame.setMeshData(fuhgl::render::shapes::boxData({0.05f, 0.05f, 0.05f}, {1, 1, 1}));
    door.setMeshData(scaledBoxData({0.45f, 1.05f, 0.04f}, {1, 1, 1}, {0.9f / 1.2f, 2.1f / 1.2f}));
    glass.setMeshData(fuhgl::render::shapes::boxData({0.5f, 0.55f, 0.03f}, {1, 1, 1}));
    glassSide.setMeshData(fuhgl::render::shapes::boxData({0.03f, 0.55f, 0.5f}, {1, 1, 1}));
    path.setMeshData(scaledBoxData({0.7f, 0.035f, 3.2f}, {1, 1, 1}, {1.4f / 1.2f, 6.4f / 1.2f}));
    bush = fuhgl::render::makeSphere(0.75f)->buildMesh(24, {1, 1, 1}, fuhgl::render::SolidUv::Spherical);
    trunk.setMeshData(fuhgl::render::shapes::boxData({0.16f, 1.4f, 0.16f}, {1, 1, 1}));
    foliage = fuhgl::render::makeSphere(1.f)->buildMesh(24, {1, 1, 1}, fuhgl::render::SolidUv::Spherical);

    // --- scene ---
    scene::Scene scene;
    auto addEntry = [&](scene::Node& n, fuhgl::render::Mesh* mesh) -> scene::MeshEntry& {
        return n.attach<scene::MeshComponent>().addMesh(mesh);
    };

    scene::Node& groundNode = scene.root().createChild("lawn");
    scene::MeshEntry& ge = addEntry(groundNode, &ground);
    ge.albedoTexture = &grassTex;
    ge.albedo = {1.f, 1.f, 1.f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    scene::Node& foundNode = scene.root().createChild("foundation");
    foundNode.translation = {0.f, 0.25f, 0.f};
    scene::MeshEntry& fe = addEntry(foundNode, &foundation);
    fe.albedoTexture = &stoneTex;
    fe.roughness = 0.9f;
    fe.castShadow = true;
    fe.materialId = 8;

    // the brick facade: albedo + height + normal map, POM enabled via heightMap
    scene::Node& wallNode = scene.root().createChild("brickWalls");
    wallNode.translation = {0.f, 0.25f + 1.6f, 0.f};
    scene::MeshEntry& we = addEntry(wallNode, &walls);
    we.albedo = {1.f, 1.f, 1.f};
    we.albedoTexture = &brickAlbedo;
    we.normalMap = &brickNormal;
    we.heightMap = args.parallax ? &brickHeight : nullptr; // POM on/off
    we.parallaxScale = 0.07f;  // POM depth scale in uv units (grooves ~1 cm at 2.4 m/tile)
    we.parallaxLayers = 32;    // linear search steps
    we.roughness = 0.85f;
    we.castShadow = true;
    we.materialId = 2;

    scene::Node& roofNode = scene.root().createChild("roof");
    roofNode.translation = {0.f, 3.45f, 0.f};
    scene::MeshEntry& re = addEntry(roofNode, &roof);
    re.albedoTexture = &shingleTex;
    re.albedo = {1.f, 1.f, 1.f};
    re.roughness = 0.75f;
    re.castShadow = true;
    re.materialId = 4;

    scene::Node& chimNode = scene.root().createChild("chimney");
    chimNode.translation = {1.7f, 5.6f, -1.3f};
    scene::MeshEntry& ce = addEntry(chimNode, &chimney);
    ce.albedoTexture = &brickAlbedo;
    ce.normalMap = &brickNormal;
    ce.heightMap = args.parallax ? &brickHeight : nullptr;
    ce.parallaxScale = 0.07f;
    ce.parallaxLayers = 24;
    ce.roughness = 0.85f;
    ce.castShadow = true;
    ce.materialId = 64;

    scene::Node& capNode = scene.root().createChild("chimneyCap");
    capNode.translation = {1.7f, 6.72f, -1.3f};
    scene::MeshEntry& cape = addEntry(capNode, &chimneyCap);
    cape.albedoTexture = &stoneTex;
    cape.roughness = 0.9f;
    cape.castShadow = true;
    cape.materialId = 8;

    // door on the front wall (+Z) with a white frame
    scene::Node& doorFrameNode = scene.root().createChild("doorFrame");
    doorFrameNode.translation = {0.f, 1.45f, 3.03f};
    scene::MeshEntry& dfe = addEntry(doorFrameNode, &boxFrame);
    dfe.localOffset = fuhgl::math::scale({11.f, 23.f, 1.f}); // 0.05 -> 0.55 x 1.15
    dfe.albedo = {0.92f, 0.92f, 0.90f};
    dfe.roughness = 0.5f;
    dfe.castShadow = true;
    dfe.materialId = 128;
    scene::Node& doorNode = scene.root().createChild("door");
    doorNode.translation = {0.f, 1.4f, 3.06f};
    scene::MeshEntry& de = addEntry(doorNode, &door);
    de.albedoTexture = &woodTex;
    de.albedo = {1.f, 1.f, 1.f};
    de.roughness = 0.6f;
    de.castShadow = true;
    de.materialId = 256;

    // windows: glass pane + white frame (4 border bars + cross) \u2014 front wall and both side walls
    const auto addBar = [&](const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& size) {
        scene::Node& f = scene.root().createChild("windowFrame");
        f.translation = pos;
        scene::MeshEntry& e = addEntry(f, &boxFrame); // unit box half 0.05, scaled via localOffset
        e.localOffset = fuhgl::math::scale(size);
        e.albedo = {0.92f, 0.92f, 0.90f};
        e.roughness = 0.5f;
        e.castShadow = true;
        e.materialId = 128;
    };
    const auto addGlass = [&](const fuhgl::math::Vec3& center, fuhgl::render::Mesh* glassMesh) {
        scene::Node& g = scene.root().createChild("windowGlass");
        g.translation = center;
        scene::MeshEntry& gle = addEntry(g, glassMesh);
        gle.albedo = {0.65f, 0.78f, 0.9f};
        gle.metallic = 0.85f;
        gle.roughness = 0.07f; // glossy: picks up sky IBL + SSR
        gle.castShadow = false;
        gle.materialId = 512;
    };
    // front wall (z = +3): horizontal = x, outward = +z, glass thin in z
    const auto addWindowFront = [&](float x, float y) {
        addGlass({x, y, 3.02f}, &glass);
        addBar({x, y + 0.62f, 3.05f}, {12.f, 1.f, 1.f});
        addBar({x, y - 0.62f, 3.05f}, {12.f, 1.f, 1.f});
        addBar({x + 0.57f, y, 3.05f}, {1.f, 13.5f, 1.f});
        addBar({x - 0.57f, y, 3.05f}, {1.f, 13.5f, 1.f});
        addBar({x, y, 3.055f}, {11.f, 0.8f, 0.8f});
        addBar({x, y, 3.055f}, {0.8f, 11.f, 0.8f});
    };
    // side wall (x = +-3): horizontal = z, outward = +-x, glass thin in x
    const auto addWindowSide = [&](float x, float y, float z) {
        addGlass({x, y, z}, &glassSide);
        const float ox = x > 0.f ? 0.025f : -0.025f;
        addBar({x + ox, y + 0.62f, z}, {1.f, 1.f, 12.f});
        addBar({x + ox, y - 0.62f, z}, {1.f, 1.f, 12.f});
        addBar({x + ox, y, z + 0.57f}, {1.f, 13.5f, 1.f});
        addBar({x + ox, y, z - 0.57f}, {1.f, 13.5f, 1.f});
        addBar({x + ox * 1.2f, y, z}, {0.8f, 11.f, 0.8f});
        addBar({x + ox * 1.2f, y, z}, {0.8f, 0.8f, 11.f});
    };
    addWindowFront(-1.8f, 2.3f);
    addWindowFront(1.8f, 2.3f);
    addWindowSide(3.02f, 2.3f, 0.8f);
    addWindowSide(-3.02f, 2.3f, -0.8f);

    // stone path from the door toward the viewer
    scene::Node& pathNode = scene.root().createChild("path");
    pathNode.translation = {0.f, 0.035f, 6.2f};
    scene::MeshEntry& pe = addEntry(pathNode, &path);
    pe.albedoTexture = &stoneTex;
    pe.roughness = 0.9f;
    pe.castShadow = false;
    pe.materialId = 8;

    // bushes along the walls + one tree
    const fuhgl::math::Vec3 bushPos[3] = {{3.7f, 0.45f, 2.3f}, {-3.8f, 0.5f, 1.8f}, {-3.6f, 0.4f, -2.6f}};
    for (int i = 0; i < 3; ++i) {
        scene::Node& b = scene.root().createChild("bush" + std::to_string(i));
        b.translation = bushPos[i];
        b.scale = {1.2f, 0.75f, 1.2f};
        scene::MeshEntry& be = addEntry(b, &bush);
        be.albedo = {0.16f, 0.34f, 0.13f};
        be.roughness = 0.9f;
        be.castShadow = true;
        be.materialId = 16;
    }
    scene::Node& trunkNode = scene.root().createChild("treeTrunk");
    trunkNode.translation = {-5.6f, 1.4f, -4.2f};
    scene::MeshEntry& tre = addEntry(trunkNode, &trunk);
    tre.albedo = {0.32f, 0.22f, 0.13f};
    tre.roughness = 0.9f;
    tre.castShadow = true;
    tre.materialId = 32;
    const fuhgl::math::Vec3 folPos[2] = {{-5.6f, 3.4f, -4.2f}, {-5.0f, 4.4f, -3.7f}};
    const fuhgl::math::Vec3 folScale[2] = {{1.7f, 1.5f, 1.7f}, {1.15f, 1.05f, 1.15f}};
    for (int i = 0; i < 2; ++i) {
        scene::Node& f = scene.root().createChild("treeFoliage" + std::to_string(i));
        f.translation = folPos[i];
        f.scale = folScale[i];
        scene::MeshEntry& fle = addEntry(f, &foliage);
        fle.albedo = {0.18f, 0.38f, 0.14f};
        fle.roughness = 0.9f;
        fle.castShadow = true;
        fle.materialId = 16;
    }

    // --- sun + sky + IBL ---
    // front-left-top, ~37 deg up: the +Z facade stays sunlit for the wall closeup while the
    // house shadow falls toward +X/-Z, i.e. into the wide shot's view
    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize({-0.55f, 0.60f, 0.62f});
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(-sunDir, {1.0f, 0.93f, 0.80f}, 2.7f);
    scene.root().createChild("skyFill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({0.4f, -0.6f, 0.5f}, {0.35f, 0.45f, 0.65f}, 0.35f);

    fuhgl::Texture skyCube = makeDaySky(256, 23u, sunDir);
    fuhgl::render::Environment env;
    if (skyCube.id() != 0 && !env.createFromCubemap(skyCube, 16, 32, 64))
        std::fprintf(stderr, "parallax_house_screenshot: IBL bake failed, continuing without env\n");
    env.intensity = 0.55f;

    // --- effects ---
    scene::Effects& fx = scene.root().effects;
    fx.enable(render::Effect::Taa);
    fx.enable(render::Effect::Upscale);
    fx.enable(render::Effect::Tonemap);
    fx.enable(render::Effect::Ibl);
    fx.enable(render::Effect::CsmShadows);
    fx.enable(render::Effect::Ssao);
    fx.enable(render::Effect::Ssr);
    fx.enable(render::Effect::Bloom);
    scene::EffectParams& prm = fx.params;
    prm.exposure = 1.02f;
    prm.taaBlend = 0.08f;
    prm.shadowCenter = {0.f, 2.f, 0.f};
    prm.shadowRadius = 45.f;
    prm.shadowZNear = 0.5f;
    prm.shadowZFar = 160.f;
    prm.ssaoRadius = 0.8f;
    prm.ssaoIntensity = 0.9f;
    prm.ssrMaxSteps = 32;
    prm.ssrMaxDist = 30.f;
    prm.ssrStrength = 0.55f;
    prm.bloomThreshold = 1.0f;
    prm.bloomStrength = 0.45f;
    prm.bloomRadius = 1.1f;
    prm.tonemapOperator = 0; // ACES

    // --- renderer ---
    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, 0)) return 1;
    if (skyCube.id() != 0) renderer.setSkyCubemap(&skyCube);
    renderer.skyTop = {0.16f, 0.40f, 0.85f};
    renderer.skyBottom = {0.72f, 0.83f, 0.95f};
    renderer.enableCsmShadows(2048, 4);
    renderer.godRays.enabled = true;
    renderer.godRays.strength = 0.15f;
    renderer.godRays.decay = 0.95f;
    renderer.godRays.samples = 40;

    fuhgl::FirstPersonCamera cam;
    cam.fovY = fuhgl::math::radians(50.f);
    cam.nearPlane = 0.1f;
    cam.farPlane = 300.f;
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    // --- three shots: wide 3/4 view, grazing wall closeup (POM), same without POM ---
    struct Shot {
        const char* name;
        fuhgl::math::Vec3 pos, target;
        bool pom;
    };
    const Shot shots[3] = {
        {"wide", {11.5f, 5.4f, 13.5f}, {0.f, 2.4f, 0.3f}, true},
        {"wall", {5.6f, 2.1f, 4.4f}, {-2.0f, 1.8f, 3.0f}, true}, // grazing along the front wall
        {"wall_nopom", {5.6f, 2.1f, 4.4f}, {-2.0f, 1.8f, 3.0f}, false},
    };

    bool ok = true;
    for (const Shot& shot : shots) {
        we.heightMap = (args.parallax && shot.pom) ? &brickHeight : nullptr;
        ce.heightMap = (args.parallax && shot.pom) ? &brickHeight : nullptr;
        cam.position = shot.pos;
        aimAt(cam, shot.target);
        for (int i = 0; i < args.frames; ++i) {
            scene::FrameData frame;
            scene.fillFrame(frame, &cam, args.width, args.height);
            frame.view.env = &env;
            frame.view.fogColor = {0.72f, 0.83f, 0.95f};
            frame.view.fogDensity = 0.0012f;
            frame.effects.applyTo(renderer);
            renderer.setRenderScale(args.scale);
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                            frame.lightProbes, &frame.decals, frame.water, &frame.skins);
        }
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
        fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                              pixels.data());
        const std::string path = args.prefix + "_" + shot.name + ".png";
        if (!screenshot::writePng(path.c_str(), pixels.data(), args.width, args.height)) {
            std::fprintf(stderr, "parallax_house_screenshot: failed to write %s\n", path.c_str());
            ok = false;
            continue;
        }
        std::fprintf(stderr, "parallax_house_screenshot: wrote %s (%dx%d, %d frames, pom=%d, GL %d.%d %s)\n",
                     path.c_str(), args.width, args.height, args.frames, shot.pom ? 1 : 0,
                     fuhgl::gl::version().major, fuhgl::gl::version().minor,
                     fuhgl::gl::queryDeviceInfo().renderer.c_str());
    }
    return ok ? 0 : 1;
}
