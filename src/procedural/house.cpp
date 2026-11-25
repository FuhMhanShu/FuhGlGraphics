// HouseGenerator implementation: winding-safe mesh helpers (shading normal derived from the
// triangle winding, CCW seen from outside -> nothing is culled), four roof solids (gable /
// lean-to / hipped / flat) and the feature set (bay, dormers, annex, porch, chimney, timber
// frame, highrise window grid + setback). Organic-architecture layer: additive building
// history (overhanging upper floor on struts, stair tower with CSG cone roof), bevelled wall
// edges, low-freq vertex warp with sagging ridge, micro-jitter (shutters/chimney), plinth +
// pilasters + lintels/reveals, human clutter props (CSG flower boxes/dish/firewood, AC,
// clothesline) and humidity-driven ivy; CSG solids via FuhGLCore render/solid.hpp.
// BuildingContext rules (climate -> form, local resources -> material, wealth/density ->
// storeys/shop/fence/patches, flood zone -> stilts) derive form+materials automatically.
// Deterministic per seed via splitmix32.

#include <fuhglgraphics/procedural/house.hpp>

#include <fuhglgraphics/procedural/noise.hpp>

#include <fuhgl/render.hpp>
#include <fuhgl/render/solid.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fuhglgraphics::procedural {

namespace {

struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() {
        state += 0x9E3779B9u;
        std::uint32_t z = state;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }
    float nextf() { return static_cast<float>(next() * (1.0 / 4294967296.0)); }
    float range(float a, float b) { return a + (b - a) * nextf(); }
};

void pushVertex(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& n,
                const fuhgl::math::Vec3& c, float u, float v) {
    md.vertices.push_back(p.x); md.vertices.push_back(p.y); md.vertices.push_back(p.z);
    md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(u); md.vertices.push_back(v);
}

/// @brief Triangle a,b,c (CCW seen from outside); the shading normal is derived from the winding.
void addTri(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
            const fuhgl::math::Vec3& c, const fuhgl::math::Vec3& col) {
    const fuhgl::math::Vec3 n = fuhgl::math::normalize(fuhgl::math::cross(b - a, c - a));
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    pushVertex(md, a, n, col, 0.f, 0.f);
    pushVertex(md, b, n, col, 1.f, 0.f);
    pushVertex(md, c, n, col, 0.5f, 1.f);
    md.indices.push_back(base); md.indices.push_back(base + 1); md.indices.push_back(base + 2);
}

/// @brief Quad a,b,c,d (CCW seen from outside); normal from the winding.
void addQuad(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
             const fuhgl::math::Vec3& c, const fuhgl::math::Vec3& d, const fuhgl::math::Vec3& col) {
    const fuhgl::math::Vec3 n = fuhgl::math::normalize(fuhgl::math::cross(b - a, d - a));
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    pushVertex(md, a, n, col, 0.f, 0.f);
    pushVertex(md, b, n, col, 1.f, 0.f);
    pushVertex(md, c, n, col, 1.f, 1.f);
    pushVertex(md, d, n, col, 0.f, 1.f);
    md.indices.push_back(base); md.indices.push_back(base + 1); md.indices.push_back(base + 2);
    md.indices.push_back(base); md.indices.push_back(base + 2); md.indices.push_back(base + 3);
}

void shiftY(fuhgl::render::MeshData& md, float dy) {
    for (std::size_t i = 0; i < md.vertexCount(); ++i) md.vertices[i * 11 + 1] += dy;
}

void appendTransformed(fuhgl::render::MeshData& dst, const fuhgl::render::MeshData& src,
                       const fuhgl::math::Mat4& m) {
    const fuhgl::math::Mat3 m3(m);
    const std::uint32_t base = static_cast<std::uint32_t>(dst.vertexCount());
    for (std::size_t i = 0; i < src.vertexCount(); ++i) {
        const float* v = &src.vertices[i * 11];
        const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{v[0], v[1], v[2], 1.f};
        const fuhgl::math::Vec3 n = fuhgl::math::normalize(m3 * fuhgl::math::Vec3{v[3], v[4], v[5]});
        dst.vertices.push_back(p.x); dst.vertices.push_back(p.y); dst.vertices.push_back(p.z);
        dst.vertices.push_back(n.x); dst.vertices.push_back(n.y); dst.vertices.push_back(n.z);
        for (int k = 6; k < 11; ++k) dst.vertices.push_back(v[k]);
    }
    for (const std::uint32_t idx : src.indices) dst.indices.push_back(base + idx);
}

/// @brief Appends a flat-colored box (half extents `half`, centered at `pos`) to `dst`.
void appendBox(fuhgl::render::MeshData& dst, const fuhgl::math::Vec3& half,
               const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& color) {
    fuhgl::render::MeshData b = fuhgl::render::shapes::boxData(half, color);
    shiftY(b, pos.y);
    appendTransformed(dst, b, fuhgl::math::translate({pos.x, 0.f, pos.z}));
}

// --- roof solids (base at y = 0, caller shifts onto the walls) ---------------------------

fuhgl::render::MeshData gableRoof(float w, float d, float h, const fuhgl::math::Vec3& col,
                                   float overhang = 0.14f) {
    const float hw = w * 0.5f + overhang, hd = d * 0.5f + overhang;
    fuhgl::render::MeshData md;
    addQuad(md, {-hw, 0.f, hd}, {hw, 0.f, hd}, {hw, h, 0.f}, {-hw, h, 0.f}, col);   // south slope
    addQuad(md, {hw, 0.f, -hd}, {-hw, 0.f, -hd}, {-hw, h, 0.f}, {hw, h, 0.f}, col);  // north slope
    addTri(md, {hw, 0.f, hd}, {hw, 0.f, -hd}, {hw, h, 0.f}, col);                    // east gable
    addTri(md, {-hw, 0.f, -hd}, {-hw, 0.f, hd}, {-hw, h, 0.f}, col);                 // west gable
    return md;
}

fuhgl::render::MeshData shedRoof(float w, float d, float h, const fuhgl::math::Vec3& col,
                                  float overhang = 0.14f) {
    const float hw = w * 0.5f + overhang, hd = d * 0.5f + overhang;
    const float yLow = h * 0.45f;
    const fuhgl::math::Vec3 A{-hw, yLow, hd}, B{hw, yLow, hd}, C{hw, h, -hd}, D{-hw, h, -hd};
    const fuhgl::math::Vec3 E{hw, yLow, -hd}, F{-hw, yLow, -hd};
    fuhgl::render::MeshData md;
    addQuad(md, A, B, C, D, col); // slope
    addQuad(md, F, D, C, E, col); // vertical back
    addTri(md, B, E, C, col);     // east end
    addTri(md, A, D, F, col);     // west end
    return md;
}

fuhgl::render::MeshData hippedRoof(float w, float d, float h, const fuhgl::math::Vec3& col,
                                    float overhang = 0.14f) {
    const float hw = w * 0.5f + overhang, hd = d * 0.5f + overhang;
    const float rh = std::max(0.f, hw - h); // ridge half-length (hip run ~ h horizontally)
    fuhgl::render::MeshData md;
    addQuad(md, {-hw, 0.f, hd}, {hw, 0.f, hd}, {rh, h, 0.f}, {-rh, h, 0.f}, col);  // south slope
    addQuad(md, {hw, 0.f, -hd}, {-hw, 0.f, -hd}, {-rh, h, 0.f}, {rh, h, 0.f}, col); // north slope
    addTri(md, {hw, 0.f, hd}, {hw, 0.f, -hd}, {rh, h, 0.f}, col);                  // east hip
    addTri(md, {-hw, 0.f, -hd}, {-hw, 0.f, hd}, {-rh, h, 0.f}, col);               // west hip
    return md;
}

fuhgl::render::MeshData flatRoof(float w, float d, float t, const fuhgl::math::Vec3& col,
                                  float overhang = 0.1f) {
    fuhgl::render::MeshData md =
        fuhgl::render::shapes::boxData({w * 0.5f + overhang, t * 0.5f, d * 0.5f + overhang}, col);
    shiftY(md, t * 0.5f);
    return md;
}

// --- BuildingContext -> derived architecture rules ----------------------------------------
// Climate->form: much rain/snow -> steep gable/hipped roof, wide eaves, gutter; arid ->
// flat roof with parapet; hot -> small windows + shutters + light high-albedo walls;
// cold -> big windows + chimney; flood zone -> stilts. Resources -> facade/roof material.
// Wealth/density -> storeys, ornaments, patches, shop front, garden fence.
struct Derived {
    FacadeMaterial facade = FacadeMaterial::Brick;
    float roofPitchMul = 1.f;
    float overhang = 0.14f;
    bool forceGutter = false;
    bool forceFlat = false;
    bool parapet = false;
    float windowScale = 1.f;
    bool forceShutters = false;
    bool forceChimney = false;
    float wallLighten = 0.f;  ///< hot climate: light facade colors reflect the sun
    float stiltLift = 0.f;    ///< flood zone: lift the whole house onto posts
    bool shopFront = false;   ///< high density: ground floor = shop with display windows
    bool fence = false;       ///< low density: detached house with garden fence
    bool patches = false;     ///< low wealth: patched walls (mixed materials)
    bool cornice = false;     ///< high wealth: ornamental cornice bands per floor
    float irregularMul = 1.f; ///< wealth scales the wall jitter (rich = clean, poor = rough)
};

Derived derive(const BuildingContext& c, unsigned seed) {
    Derived d;
    d.facade = HouseGenerator::resolveFacadeMaterial(c, seed);
    const float snowLoad = c.precipitation * (1.f - c.temperature);
    if (c.precipitation > 0.6f || snowLoad > 0.35f) {
        d.roofPitchMul = 1.35f;  // steep roof sheds rain/snow
        d.overhang = 0.30f;      // wide eaves keep the facade dry
        d.forceGutter = true;
    }
    if (c.precipitation < 0.3f && c.temperature > 0.6f) {
        d.forceFlat = true;      // arid: flat (walkable) roof with parapet
        d.parapet = true;
        d.overhang = 0.06f;
    }
    d.windowScale = std::min(1.3f, std::max(0.55f, 1.3f - c.temperature * 0.75f));
    if (c.temperature > 0.65f) {
        d.forceShutters = true;  // shutters against the sun
        d.wallLighten = 0.28f;   // high albedo reflects sunlight
    }
    if (c.temperature < 0.35f) d.forceChimney = true; // heating
    if (c.waterProximity > 0.55f) d.stiltLift = 1.0f + 0.9f * c.waterProximity;
    if (c.density > 0.7f) d.shopFront = true;
    if (c.density < 0.35f) d.fence = true;
    if (c.wealth > 0.7f) d.cornice = true;
    if (c.wealth < 0.3f) d.patches = true;
    d.irregularMul = c.wealth > 0.7f ? 0.4f : (c.wealth < 0.3f ? 1.8f : 1.f);
    return d;
}

// --- palettes -----------------------------------------------------------------------------

struct Palette {
    fuhgl::math::Vec3 wall, roof, window, door, beam;
};

Palette pickPalette(HouseStyle style, Rng& rng) {
    static const fuhgl::math::Vec3 villageWalls[3] = {{0.85f, 0.80f, 0.72f}, {0.55f, 0.42f, 0.30f}, {0.78f, 0.74f, 0.62f}};
    static const fuhgl::math::Vec3 villageRoofs[3] = {{0.45f, 0.16f, 0.12f}, {0.30f, 0.22f, 0.18f}, {0.52f, 0.30f, 0.16f}};
    static const fuhgl::math::Vec3 townWalls[3] = {{0.78f, 0.60f, 0.48f}, {0.80f, 0.76f, 0.68f}, {0.62f, 0.66f, 0.70f}};
    static const fuhgl::math::Vec3 townRoofs[3] = {{0.50f, 0.24f, 0.14f}, {0.28f, 0.28f, 0.32f}, {0.40f, 0.20f, 0.12f}};
    const int i = static_cast<int>(rng.next() % 3u);
    Palette pal;
    pal.window = {0.72f, 0.84f, 0.95f};
    pal.door = {0.30f, 0.18f, 0.10f};
    pal.beam = {0.25f, 0.16f, 0.10f};
    if (style == HouseStyle::Village) {
        pal.wall = villageWalls[i];
        pal.roof = villageRoofs[i];
    } else if (style == HouseStyle::Highrise) {
        pal.wall = {0.55f, 0.60f, 0.66f};
        pal.roof = {0.35f, 0.36f, 0.38f};
        pal.window = {0.25f, 0.38f, 0.50f};
    } else {
        pal.wall = townWalls[i];
        pal.roof = townRoofs[i];
    }
    return pal;
}

/// @brief Overrides the style palette with the local-resource material colors and applies the
/// hot-climate lightening (high albedo). Wood = warm planks + dark shingle roof; adobe = sand
/// plaster + clay; concrete = grey + dark glass; stone/brick keep their POM texture sets.
void applyMaterialPalette(Palette& pal, FacadeMaterial mat, const BuildingContext& ctx) {
    switch (mat) {
        case FacadeMaterial::Wood:
            pal.wall = {0.62f, 0.48f, 0.33f};
            pal.roof = {0.32f, 0.22f, 0.15f}; // wood shingles
            break;
        case FacadeMaterial::Adobe:
            pal.wall = {0.86f, 0.72f, 0.52f};
            pal.roof = {0.62f, 0.45f, 0.30f};
            break;
        case FacadeMaterial::Concrete:
            pal.wall = {0.74f, 0.76f, 0.79f};
            pal.roof = {0.42f, 0.43f, 0.46f};
            pal.window = {0.22f, 0.33f, 0.44f};
            break;
        case FacadeMaterial::Stone:
            pal.roof = {0.24f, 0.26f, 0.30f}; // slate
            break;
        case FacadeMaterial::Brick:
            pal.roof = {0.55f, 0.20f, 0.13f}; // red clay tiles
            break;
        default:
            break;
    }
    if (ctx.temperature > 0.65f) // hot: light walls reflect the sun (high albedo)
        pal.wall = pal.wall * (1.f - 0.28f) + fuhgl::math::Vec3{0.28f, 0.28f, 0.28f};
}

// --- controlled imperfection: outward-oriented primitives, chamfered box, vertex warp ----

/// @brief Quad a,b,c,d wound so its normal points along `outward` (flips when needed).
void addQuadOut(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
                const fuhgl::math::Vec3& c, const fuhgl::math::Vec3& d,
                const fuhgl::math::Vec3& outward, const fuhgl::math::Vec3& col) {
    if (fuhgl::math::dot(fuhgl::math::cross(b - a, d - a), outward) < 0.f)
        addQuad(md, d, c, b, a, col);
    else
        addQuad(md, a, b, c, d, col);
}

/// @brief Triangle a,b,c wound so its normal points along `outward`.
void addTriOut(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
               const fuhgl::math::Vec3& c, const fuhgl::math::Vec3& outward,
               const fuhgl::math::Vec3& col) {
    if (fuhgl::math::dot(fuhgl::math::cross(b - a, c - a), outward) < 0.f)
        addTri(md, a, c, b, col);
    else
        addTri(md, a, b, c, col);
}

/// @brief Box with all 12 edges chamfered by `b` (6 inset faces + 12 edge quads + 8 corner
/// tris) — no razor-sharp 90° edges, so specular highlights break up realistically.
fuhgl::render::MeshData bevelBox(const fuhgl::math::Vec3& half, float b,
                                 const fuhgl::math::Vec3& col) {
    const float h[3] = {half.x, half.y, half.z};
    b = std::min(b, std::min(half.x, std::min(half.y, half.z)) * 0.45f);
    fuhgl::render::MeshData md;
    const fuhgl::math::Vec3 e[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    // 6 inset faces
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int s = -1; s <= 1; s += 2) {
            const fuhgl::math::Vec3 c = e[axis] * (s * h[axis]);
            const float hu = h[u] - b, hv = h[v] - b;
            addQuadOut(md, c - e[u] * hu - e[v] * hv, c + e[u] * hu - e[v] * hv,
                       c + e[u] * hu + e[v] * hv, c - e[u] * hu + e[v] * hv,
                       e[axis] * static_cast<float>(s), col);
        }
    }
    // 12 chamfer edge quads (edge along axis a, at sign su on u / sv on v)
    for (int a = 0; a < 3; ++a) {
        const int u = (a + 1) % 3, v = (a + 2) % 3;
        const float ha = h[a] - b;
        for (int su = -1; su <= 1; su += 2)
            for (int sv = -1; sv <= 1; sv += 2) {
                const fuhgl::math::Vec3 out =
                    fuhgl::math::normalize(e[u] * (float)su + e[v] * (float)sv);
                addQuadOut(md,
                           e[a] * (-ha) + e[u] * (su * h[u]) + e[v] * (sv * (h[v] - b)),
                           e[a] * ha + e[u] * (su * h[u]) + e[v] * (sv * (h[v] - b)),
                           e[a] * ha + e[u] * (su * (h[u] - b)) + e[v] * (sv * h[v]),
                           e[a] * (-ha) + e[u] * (su * (h[u] - b)) + e[v] * (sv * h[v]),
                           out, col);
            }
    }
    // 8 corner triangles
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                const fuhgl::math::Vec3 out =
                    fuhgl::math::normalize(fuhgl::math::Vec3{(float)sx, (float)sy, (float)sz});
                addTriOut(md, {sx * (h[0] - b), sy * h[1], sz * h[2]},
                          {sx * h[0], sy * (h[1] - b), sz * h[2]},
                          {sx * h[0], sy * h[1], sz * (h[2] - b)}, out, col);
            }
    return md;
}

/// @brief Low-frequency vertex warp: displaces vertices by fbm3 noise — walls bow slightly,
/// roofs get uneven. `horizontalOnly` keeps the floor plane intact (walls only).
void warpMesh(fuhgl::render::MeshData& md, const Noise& noise, float amp, unsigned seed,
              bool horizontalOnly) {
    if (amp <= 0.f) return;
    const float f = 1.7f;
    const float ox = seed * 0.731f, oy = seed * 0.373f;
    for (std::size_t i = 0; i < md.vertexCount(); ++i) {
        float* v = &md.vertices[i * 11];
        const float nx = noise.fbm3(v[0] * f + ox, v[1] * f, v[2] * f + oy, 2, Noise::Base::Simplex) - 0.5f;
        const float ny = noise.fbm3(v[0] * f, v[1] * f + ox, v[2] * f + oy, 2, Noise::Base::Simplex) - 0.5f;
        const float nz = noise.fbm3(v[0] * f + oy, v[1] * f + ox, v[2] * f, 2, Noise::Base::Simplex) - 0.5f;
        v[0] += nx * 2.f * amp;
        v[2] += nz * 2.f * amp;
        if (!horizontalOnly) v[1] += ny * 2.f * amp * 0.6f;
    }
}

/// @brief Lets the roof ridge sag slightly in the middle (gravity) — only top verts (|z| small
/// on a gable/hipped roof) move down by a sine over the width.
void sagRoof(fuhgl::render::MeshData& md, float halfW, float sag) {
    if (sag <= 0.f) return;
    float maxY = -1e9f;
    for (std::size_t i = 0; i < md.vertexCount(); ++i)
        maxY = std::max(maxY, md.vertices[i * 11 + 1]);
    for (std::size_t i = 0; i < md.vertexCount(); ++i) {
        float* v = &md.vertices[i * 11];
        if (v[1] > maxY - 0.05f) { // ridge line
            const float t = std::min(1.f, std::max(0.f, (v[0] + halfW) / (2.f * halfW)));
            v[1] -= sag * std::sin(fuhgl::math::kPi * t);
        }
    }
}

/// @brief Appends a CSG solid (FuhGLCore render::Solid, marching-tetrahedra mesh) transformed.
void appendSolid(fuhgl::render::MeshData& dst, const fuhgl::render::SolidPtr& s, int res,
                 const fuhgl::math::Vec3& col, const fuhgl::math::Mat4& m) {
    appendTransformed(dst, s->buildMeshData(res, col), m);
}

/// @brief Ivy/vines growing up the front wall from the ground (2D random walk with upward
/// bias + branching, hugging the facade). Density follows the humidity.
void addIvy(fuhgl::render::MeshData& md, float w, float d, float wallH, Rng& rng, const Noise& noise,
            float density, unsigned seed) {
    struct Vine { float x, y; };
    std::vector<Vine> vines;
    const int n = 1 + static_cast<int>(density * 6.f);
    for (int i = 0; i < n; ++i) vines.push_back({rng.range(-w * 0.45f, w * 0.45f), 0.12f});
    const float z = d * 0.5f + 0.014f;
    int quads = 0;
    for (int step = 0; step < 30 && !vines.empty() && quads < 90; ++step) {
        for (std::size_t vi = 0; vi < vines.size();) {
            Vine& vn = vines[vi];
            if (vn.y > wallH * 0.92f) { vines.erase(vines.begin() + vi); continue; }
            const float g = 0.55f + 0.45f * noise.fbm3(vn.x * 3.f + seed * 0.11f, vn.y * 3.f, 7.7f, 2,
                                                       Noise::Base::Simplex);
            const fuhgl::math::Vec3 col{0.16f * g + 0.06f, 0.30f * g + 0.10f, 0.10f * g + 0.04f};
            const float hw = rng.range(0.025f, 0.05f), hh = rng.range(0.05f, 0.09f);
            const float zz = z + (rng.nextf() - 0.5f) * 0.02f;
            addQuadOut(md, {vn.x - hw, vn.y, zz}, {vn.x + hw, vn.y, zz},
                       {vn.x + hw * 0.8f, vn.y + hh, zz + 0.01f}, {vn.x - hw * 0.8f, vn.y + hh, zz + 0.01f},
                       {0.f, 0.f, 1.f}, col);
            vn.y += rng.range(0.10f, 0.20f);
            vn.x += (rng.nextf() - 0.5f) * 0.24f;
            ++quads;
            if (rng.nextf() < 0.2f && vn.y < wallH * 0.8f && vines.size() < 10) // branch
                vines.push_back({vn.x + (rng.nextf() - 0.5f) * 0.3f, vn.y});
            ++vi;
        }
    }
}

} // namespace

FacadeMaterial HouseGenerator::resolveFacadeMaterial(const BuildingContext& ctx, unsigned seed) {
    LocalMaterial m = ctx.localMaterial;
    if (m == LocalMaterial::Auto) {
        if (ctx.density > 0.85f && ctx.wealth > 0.6f) {
            m = LocalMaterial::Concrete; // metropole: concrete/steel/glass, not local stone
        } else {
            switch (ctx.biome) { // worldgen Biome cast
                case 2: m = LocalMaterial::Adobe; break;            // desert
                case 4: case 5: case 6: m = LocalMaterial::Wood; break; // forest/jungle/taiga
                case 7: case 8: case 9: m = LocalMaterial::Stone; break; // tundra/rock/snow
                default: m = LocalMaterial::Brick; break;            // shore/grassland: clay
            }
        }
    }
    switch (m) {
        case LocalMaterial::Wood: return FacadeMaterial::Wood;
        case LocalMaterial::Stone: return FacadeMaterial::Stone;
        case LocalMaterial::Adobe: return FacadeMaterial::Adobe;
        case LocalMaterial::Concrete: return FacadeMaterial::Concrete;
        case LocalMaterial::Brick: return FacadeMaterial::Brick;
        default: {
            Rng rng(seed);
            return static_cast<FacadeMaterial>(rng.next() % 3u); // no biome: brick/plaster/stone
        }
    }
}

HouseGenerator::Params HouseGenerator::randomParams(HouseStyle style, float cellSize, unsigned seed) {
    Rng rng(seed);
    Params p;
    p.style = style;
    p.seed = seed;
    p.facade = static_cast<FacadeMaterial>(rng.next() % 3u); // brick / plaster / stone
    const float foot = cellSize * 0.62f;
    p.width = foot * rng.range(0.75f, 1.0f);
    p.depth = foot * rng.range(0.75f, 1.0f);
    if (style == HouseStyle::Village) {
        p.floors = 1 + static_cast<int>(rng.next() % 2u); // 1-2 storeys
        p.floorHeight = rng.range(2.5f, 2.9f);
    } else if (style == HouseStyle::Highrise) {
        p.floors = 6 + static_cast<int>(rng.next() % 7u); // 6-12 storeys
        p.floorHeight = rng.range(2.6f, 3.0f);
    } else {
        p.floors = 2 + static_cast<int>(rng.next() % 2u); // 2-3 storeys
        p.floorHeight = rng.range(2.7f, 3.0f);
    }
    return p;
}

HouseGenerator::Params HouseGenerator::randomParams(HouseStyle style, float cellSize, unsigned seed,
                                                    const BuildingContext& ctx) {
    Params p = randomParams(style, cellSize, seed);
    p.context = ctx;
    p.facade = resolveFacadeMaterial(ctx, seed); // local resources -> material
    Rng rng(seed ^ 0xC7C0u);
    // density -> storeys: suburb 1-2, town 2-4, city 4-6, center 8+ (highrise style keeps its range)
    if (style != HouseStyle::Highrise) {
        if (ctx.density < 0.35f) p.floors = 1 + static_cast<int>(rng.next() % 2u);
        else if (ctx.density < 0.65f) p.floors = 2 + static_cast<int>(rng.next() % 3u);
        else if (ctx.density < 0.85f) p.floors = 4 + static_cast<int>(rng.next() % 3u);
        else p.floors = 8 + static_cast<int>(rng.next() % 5u);
    } else {
        p.floors = 6 + static_cast<int>(ctx.density * 10.f) + static_cast<int>(rng.next() % 3u);
    }
    if (ctx.density > 0.7f) {
        // block-edge construction: the house fills the lot (shared side walls)
        p.width = cellSize * rng.range(0.80f, 0.92f);
        p.depth = cellSize * rng.range(0.80f, 0.92f);
    }
    return p;
}

HouseMesh HouseGenerator::generate(const Params& p) const {
    Rng rng(p.seed);
    HouseMesh hm;
    hm.spec.width = p.width;
    hm.spec.depth = p.depth;
    hm.spec.floors = p.floors;
    hm.spec.context = p.context;
    const float w = p.width, d = p.depth;
    const float wallH = p.floors * p.floorHeight;
    hm.spec.wallHeight = wallH;
    // environment/socio-economy rules: context wins over an explicit facade when it carries
    // a resource/biome hint (localMaterial != Auto or biome >= 0)
    const Derived der = derive(p.context, p.seed);
    const FacadeMaterial facade =
        (p.context.localMaterial != LocalMaterial::Auto || p.context.biome >= 0) ? der.facade : p.facade;
    hm.spec.facade = facade;
    hm.spec.windowScale = der.windowScale;
    hm.spec.roofOverhang = der.overhang;
    const Palette pal0 = pickPalette(p.style, rng);
    Palette pal = pal0;
    applyMaterialPalette(pal, facade, p.context);
    const float irr = p.wallIrregularity * der.irregularMul;

    // --- walls: chamfered edges (bevel) + low-freq vertex warp (bowed walls) + slight
    //     per-vertex jitter = bump-like relief on the facade ---
    hm.walls = p.bevelEdges
                   ? bevelBox({w * 0.5f, wallH * 0.5f, d * 0.5f}, 0.035f, pal.wall)
                   : fuhgl::render::shapes::boxData({w * 0.5f, wallH * 0.5f, d * 0.5f}, pal.wall);
    shiftY(hm.walls, wallH * 0.5f);
    hm.spec.beveled = p.bevelEdges;
    if (p.warpAmount > 0.f) {
        Noise warpN(p.seed ^ 0xA17u);
        warpMesh(hm.walls, warpN, p.warpAmount, p.seed, true); // horizontalOnly: floor stays flat
        hm.spec.warped = true;
    }
    if (irr > 0.f) {
        hm.spec.wallIrregular = true;
        for (std::size_t i = 0; i < hm.walls.vertexCount(); ++i) {
            float* v = &hm.walls.vertices[i * 11];
            v[0] += (rng.nextf() - 0.5f) * 2.f * irr;
            v[2] += (rng.nextf() - 0.5f) * 2.f * irr;
        }
    }
    // facade material detail: stone gets relief courses (brick relies on the app-side POM set)
    if (facade == FacadeMaterial::Stone) {
        const fuhgl::math::Vec3 stoneDark = pal.wall * 0.82f;
        for (float y = 0.35f; y < wallH; y += 0.55f)
            for (int s = -1; s <= 1; s += 2) // front + back
                appendBox(hm.details, {w * 0.5f + 0.014f, 0.07f, 0.014f},
                          {0.f, y, s * (d * 0.5f + 0.014f)}, stoneDark);
    }
    // massive protruding plinth (Sockel) around the whole base in the local soil/stone color —
    // carries the walls and levels small terrain unevenness
    {
        const fuhgl::math::Vec3 plinthCol =
            p.context.soilColor * 0.8f + fuhgl::math::Vec3{0.07f, 0.065f, 0.06f};
        appendBox(hm.details, {w * 0.5f + 0.035f, 0.19f, d * 0.5f + 0.035f}, {0.f, 0.19f, 0.f},
                  plinthCol);
        hm.spec.hasPlinth = true;
        // corner pilasters (Lisenen) on richer houses / stone facades
        if (p.context.wealth > 0.4f || facade == FacadeMaterial::Stone)
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sz = -1; sz <= 1; sz += 2)
                    appendBox(hm.details, {0.06f, wallH * 0.5f, 0.06f},
                              {sx * (w * 0.5f + 0.02f), wallH * 0.5f, sz * (d * 0.5f + 0.02f)},
                              plinthCol * 1.15f + fuhgl::math::Vec3{0.04f, 0.04f, 0.04f});
    }

    // --- door (front = +z) with load-bearing lintel (Sturz) above it ---
    appendBox(hm.details, {0.45f, 0.95f, 0.04f}, {0.f, 0.95f, d * 0.5f + 0.03f}, pal.door);
    {
        const fuhgl::math::Vec3 lintelCol =
            facade == FacadeMaterial::Wood ? pal.beam : fuhgl::math::Vec3{0.62f, 0.60f, 0.57f};
        appendBox(hm.details, {0.56f, 0.09f, 0.05f}, {0.f, 1.94f, d * 0.5f + 0.035f}, lintelCol);
        hm.spec.hasLintels = true;
    }

    if (p.style == HouseStyle::Highrise) {
        // window grid on front + back, optional setback, flat roof
        const int cols = std::max(2, static_cast<int>(w / 1.1f));
        const int rows = std::max(2, p.floors);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const float x = (static_cast<float>(c) - (cols - 1) * 0.5f) * (w / cols) * 0.8f;
                const float y = 1.4f + static_cast<float>(r) * p.floorHeight * 0.72f;
                appendBox(hm.details, {0.42f, 0.62f, 0.03f}, {x, y, d * 0.5f + 0.02f}, pal.window);
                appendBox(hm.details, {0.42f, 0.62f, 0.03f}, {x, y, -d * 0.5f - 0.02f}, pal.window);
                // glass panes in their own mesh -> the app attaches a reflective material
                appendBox(hm.glass, {0.36f, 0.56f, 0.015f}, {x, y, d * 0.5f + 0.035f}, pal.window);
                appendBox(hm.glass, {0.36f, 0.56f, 0.015f}, {x, y, -d * 0.5f - 0.035f}, pal.window);
            }
        float top = wallH;
        if (rng.nextf() < 0.5f) { // setback: smaller upper box
            const float uh = wallH * rng.range(0.2f, 0.35f);
            fuhgl::render::MeshData upper =
                fuhgl::render::shapes::boxData({w * 0.3f, uh * 0.5f, d * 0.3f}, pal.wall);
            shiftY(upper, wallH + uh * 0.5f);
            appendTransformed(hm.walls, upper, fuhgl::math::translate({0.f, 0.f, 0.f}));
            top += uh;
        }
        hm.roof = flatRoof(w, d, 0.25f, pal.roof);
        shiftY(hm.roof, top);
        hm.spec.roofType = RoofType::Flat;
        if (rng.nextf() < 0.5f) { // rooftop box (elevator shaft)
            appendBox(hm.details, {0.5f, 0.7f, 0.5f}, {w * 0.25f, top + 0.7f, -d * 0.2f}, pal.wall);
        }
        return hm;
    }

    // --- windows per floor, front + back: frame in details, glass pane in hm.glass,
    //     optional shutters (Fensterläden) flanking each window. Window size follows the
    //     climate (small when hot, large when cold); the ground floor of a shop-front house
    //     gets display windows instead (added below). ---
    const bool shutters =
        p.style != HouseStyle::Highrise && (der.forceShutters || rng.nextf() < 0.5f);
    hm.spec.hasShutters = shutters;
    const float ws = der.windowScale;
    const fuhgl::math::Vec3 lintelCol =
        facade == FacadeMaterial::Wood ? pal.beam : fuhgl::math::Vec3{0.62f, 0.60f, 0.57f};
    const fuhgl::math::Vec3 revealCol = pal.wall * 0.72f;
    for (int f = 0; f < p.floors; ++f) {
        if (der.shopFront && f == 0) continue; // ground floor = shop front
        const float y = p.floorHeight * (0.45f + f * 1.0f);
        if (y > wallH - 0.7f) break;
        for (int s = -1; s <= 1; s += 2) {
            const float wx = s * w * 0.24f;
            // recessed reveal (Laibung) on the front: frame rim around the opening -> real depth
            for (int side = -1; side <= 1; side += 2) {
                appendBox(hm.details, {0.035f, 0.42f * ws + 0.07f, 0.025f},
                          {wx + side * (0.15f * ws + 0.035f), y, d * 0.5f + 0.012f}, revealCol);
                appendBox(hm.details, {0.30f * ws + 0.07f, 0.035f, 0.025f},
                          {wx, y + (0.21f * ws + 0.035f), d * 0.5f + 0.012f}, revealCol);
            }
            appendBox(hm.details, {0.30f * ws + 0.07f, 0.035f, 0.025f},
                      {wx, y - (0.21f * ws + 0.035f), d * 0.5f + 0.012f}, revealCol);
            appendBox(hm.details, {0.30f * ws, 0.42f * ws, 0.03f}, {wx, y, d * 0.5f + 0.02f}, pal.window);
            appendBox(hm.details, {0.30f * ws, 0.42f * ws, 0.03f}, {wx, y, -d * 0.5f - 0.02f}, pal.window);
            appendBox(hm.glass, {0.25f * ws, 0.36f * ws, 0.015f}, {wx, y, d * 0.5f + 0.045f}, pal.window);
            appendBox(hm.glass, {0.25f * ws, 0.36f * ws, 0.015f}, {wx, y, -d * 0.5f - 0.045f}, pal.window);
            // lintel (Sturz) above each window (front)
            appendBox(hm.details, {0.30f * ws + 0.1f, 0.08f, 0.04f},
                      {wx, y + 0.21f * ws + 0.08f, d * 0.5f + 0.03f}, lintelCol);
            hm.spec.hasLintels = true;
            if (shutters) {
                const fuhgl::math::Vec3 shutterCol = pal.beam * 1.15f + fuhgl::math::Vec3{0.1f, 0.12f, 0.1f};
                for (int side = -1; side <= 1; side += 2) {
                    // micro-jitter: each shutter hangs at a slightly different angle/offset
                    for (int fz = -1; fz <= 1; fz += 2) {
                        fuhgl::render::MeshData sh =
                            fuhgl::render::shapes::boxData({0.09f * ws, 0.21f * ws, 0.02f}, shutterCol);
                        const float jr = (rng.nextf() - 0.5f) * 0.14f;  // ~±4°
                        const float jo = (rng.nextf() - 0.5f) * 0.025f;
                        appendTransformed(hm.details, sh,
                            fuhgl::math::translate({wx + side * 0.42f * ws, y,
                                                    fz * (d * 0.5f + 0.03f) + jo}) *
                            fuhgl::math::rotateY(jr * fz));
                    }
                }
            }
        }
    }

    // --- shop front (high density): ground floor = Ladenlokal with big display windows,
    //     a sign band above the door and the door pushed to the side ---
    if (der.shopFront && p.style != HouseStyle::Highrise) {
        hm.spec.hasShopFront = true;
        const float y = p.floorHeight * 0.5f;
        for (int s = -1; s <= 1; s += 2) {
            const float wx = s * w * 0.27f;
            appendBox(hm.details, {0.55f, 0.75f, 0.03f}, {wx, y, d * 0.5f + 0.02f}, pal.window);
            appendBox(hm.glass, {0.50f, 0.70f, 0.015f}, {wx, y, d * 0.5f + 0.045f}, pal.window);
        }
        // sign band above the shop front (the center door stays between the display windows)
        appendBox(hm.details, {w * 0.5f + 0.02f, 0.14f, 0.03f},
                  {0.f, p.floorHeight + 0.12f, d * 0.5f + 0.03f}, {0.65f, 0.18f, 0.14f});
    }

    // --- roof: arid climate -> flat roof with parapet (rooftop terrace); much rain/snow ->
    //     only steep gable/hipped (sheds water); otherwise gable / lean-to / hipped ---
    const float roofRoll = rng.nextf();
    float roofH = std::max(1.1f, wallH * (p.style == HouseStyle::Village ? 0.5f : 0.38f)) *
                  der.roofPitchMul;
    const float oh = der.overhang;
    hm.spec.roofHeight = roofH;
    if (der.forceFlat) {
        hm.roof = flatRoof(w, d, 0.25f, pal.roof, oh);
        hm.spec.roofType = RoofType::Flat;
        shiftY(hm.roof, wallH);
        // parapet (Brüstung) around the flat roof edge
        hm.spec.hasParapet = true;
        const fuhgl::math::Vec3 pc = pal.wall * 0.9f;
        const float hw = w * 0.5f + oh, hd = d * 0.5f + oh, ph = 0.45f;
        appendBox(hm.roof, {hw, ph * 0.5f, 0.05f}, {0.f, wallH + ph * 0.5f, hd - 0.05f}, pc);
        appendBox(hm.roof, {hw, ph * 0.5f, 0.05f}, {0.f, wallH + ph * 0.5f, -hd + 0.05f}, pc);
        appendBox(hm.roof, {0.05f, ph * 0.5f, hd}, {hw - 0.05f, wallH + ph * 0.5f, 0.f}, pc);
        appendBox(hm.roof, {0.05f, ph * 0.5f, hd}, {-hw + 0.05f, wallH + ph * 0.5f, 0.f}, pc);
    } else if (roofRoll < 0.45f || (der.roofPitchMul > 1.2f && roofRoll < 0.75f)) {
        hm.roof = gableRoof(w, d, roofH, pal.roof, oh);
        hm.spec.roofType = RoofType::Gable;
        shiftY(hm.roof, wallH);
    } else if (roofRoll < 0.7f && der.roofPitchMul <= 1.2f) {
        hm.roof = shedRoof(w, d, roofH, pal.roof, oh);
        hm.spec.roofType = RoofType::Shed;
        shiftY(hm.roof, wallH);
    } else {
        hm.roof = hippedRoof(w, d, roofH, pal.roof, oh);
        hm.spec.roofType = RoofType::Hipped;
        shiftY(hm.roof, wallH);
    }
    // controlled imperfection on the roof: sagging ridge (gravity) + uneven slopes
    if (hm.spec.roofType != RoofType::Flat && p.warpAmount > 0.f) {
        sagRoof(hm.roof, w * 0.5f + oh, p.warpAmount * 1.8f);
        Noise warpN(p.seed ^ 0xA18u);
        warpMesh(hm.roof, warpN, p.warpAmount * 0.6f, p.seed + 7u, false);
    }
    // roof penetrations per "structural rules": solar panels + flush roof windows on the slopes
    if ((hm.spec.roofType == RoofType::Gable || hm.spec.roofType == RoofType::Hipped)) {
        const float hd = d * 0.5f + oh;
        const float slope = std::atan2(roofH, hd);
        if (rng.nextf() < 0.15f + 0.4f * p.clutter) { // solar panels (dark blue, tilted on the slope)
            hm.spec.hasSolar = true;
            for (int k = 0; k < 2; ++k) {
                const float f = 0.35f + 0.25f * k; // fraction ridge->eave
                fuhgl::render::MeshData panel =
                    fuhgl::render::shapes::boxData({0.55f, 0.02f, 0.4f}, {0.07f, 0.11f, 0.24f});
                appendTransformed(hm.roof, panel,
                    fuhgl::math::translate({(k - 0.5f) * 1.25f, wallH + roofH * f, hd * (1.f - f)}) *
                    fuhgl::math::rotateX(slope));
            }
        }
        if (rng.nextf() < 0.35f) { // flush roof window (Dachfenster) in the front slope
            hm.spec.hasRoofWindow = true;
            fuhgl::render::MeshData rw =
                fuhgl::render::shapes::boxData({0.16f, 0.015f, 0.24f}, pal.window);
            appendTransformed(hm.glass, rw,
                fuhgl::math::translate({w * 0.18f, wallH + roofH * 0.55f, hd * 0.45f}) *
                fuhgl::math::rotateX(slope));
        }
    }

    // --- Dachrinne: gutter along the eaves + one downpipe at the front-right corner
    //     (forced in wet climates) ---
    if (der.forceGutter || (!der.forceFlat && rng.nextf() < 0.85f)) {
        hm.spec.hasGutter = true;
        const fuhgl::math::Vec3 metal{0.55f, 0.57f, 0.60f};
        const float eaveZ = d * 0.5f + oh;
        if (hm.spec.roofType == RoofType::Shed) {
            appendBox(hm.details, {w * 0.5f + oh + 0.04f, 0.06f, 0.07f}, {0.f, wallH + 0.02f, eaveZ}, metal);
        } else if (hm.spec.roofType != RoofType::Flat) {
            appendBox(hm.details, {w * 0.5f + oh + 0.04f, 0.06f, 0.07f}, {0.f, wallH - 0.04f, eaveZ}, metal);
            appendBox(hm.details, {w * 0.5f + oh + 0.04f, 0.06f, 0.07f}, {0.f, wallH - 0.04f, -eaveZ}, metal);
        }
        if (hm.spec.roofType != RoofType::Flat)
            appendBox(hm.details, {0.06f, wallH * 0.5f, 0.06f}, {w * 0.5f + oh - 0.02f, wallH * 0.5f, eaveZ}, metal);
    }

    // --- Garage: side box with its own lean-to roof and a roller door ---
    if (p.style != HouseStyle::Highrise && rng.nextf() < 0.35f) {
        hm.spec.hasGarage = true;
        const float gw2 = std::min(3.4f, w * 0.6f), gd = d * 0.9f, gh = 2.5f;
        const float gx = w * 0.5f + gw2 * 0.5f - 0.05f;
        fuhgl::render::MeshData garage = fuhgl::render::shapes::boxData({gw2 * 0.5f, gh * 0.5f, gd * 0.5f}, pal.wall);
        shiftY(garage, gh * 0.5f);
        appendTransformed(hm.walls, garage, fuhgl::math::translate({gx, 0.f, 0.f}));
        fuhgl::render::MeshData gr = shedRoof(gw2, gd, 0.7f, pal.roof);
        shiftY(gr, gh);
        appendTransformed(hm.roof, gr, fuhgl::math::translate({gx, 0.f, 0.f}));
        // roller door on the garage front (facing -x toward the street side is fine visually)
        appendBox(hm.details, {0.03f, 1.05f, gw2 * 0.42f}, {gx - gw2 * 0.5f - 0.02f, 1.05f, 0.f},
                  pal.door);
        for (int l = 0; l < 5; ++l) // door slats
            appendBox(hm.details, {0.045f, 0.05f, gw2 * 0.42f},
                      {gx - gw2 * 0.5f - 0.035f, 0.35f + l * 0.4f, 0.f}, pal.beam);
    }

    // --- balcony with balcony door on an upper floor (front = +z) ---
    if (p.floors >= 2 && p.style != HouseStyle::Highrise && rng.nextf() < 0.4f) {
        hm.spec.hasBalcony = true;
        const float by = p.floorHeight * 1.05f; // first upper floor
        const float bx = rng.range(-w * 0.15f, w * 0.15f);
        const float bw2 = std::min(1.7f, w * 0.4f), bd2 = 0.85f;
        appendBox(hm.details, {bw2 * 0.5f, 0.05f, bd2 * 0.5f}, {bx, by, d * 0.5f + bd2 * 0.5f},
                  pal.wall * 0.9f); // slab
        // railing: bars + top rail on the three open sides
        const fuhgl::math::Vec3 railCol = pal.beam;
        for (int i = 0; i <= 4; ++i) {
            const float rx = bx - bw2 * 0.5f + (bw2 * i / 4.f);
            appendBox(hm.details, {0.015f, 0.45f, 0.015f}, {rx, by + 0.5f, d * 0.5f + bd2}, railCol);
        }
        appendBox(hm.details, {bw2 * 0.5f, 0.03f, 0.02f}, {bx, by + 0.95f, d * 0.5f + bd2}, railCol);
        for (int i = 0; i <= 2; ++i) {
            const float rz = d * 0.5f + bd2 * i / 2.f;
            appendBox(hm.details, {0.015f, 0.45f, 0.015f}, {bx - bw2 * 0.5f, by + 0.5f, rz}, railCol);
            appendBox(hm.details, {0.015f, 0.45f, 0.015f}, {bx + bw2 * 0.5f, by + 0.5f, rz}, railCol);
        }
        appendBox(hm.details, {0.02f, 0.03f, bd2 * 0.5f}, {bx - bw2 * 0.5f, by + 0.95f, d * 0.5f + bd2 * 0.5f}, railCol);
        appendBox(hm.details, {0.02f, 0.03f, bd2 * 0.5f}, {bx + bw2 * 0.5f, by + 0.95f, d * 0.5f + bd2 * 0.5f}, railCol);
        // balcony door (taller than a window) + its glass
        appendBox(hm.details, {0.22f, 0.52f, 0.03f}, {bx, by + 0.42f, d * 0.5f + 0.02f}, pal.door);
        appendBox(hm.glass, {0.18f, 0.46f, 0.015f}, {bx, by + 0.46f, d * 0.5f + 0.045f}, pal.window);
    }

    // --- features ---
    if (p.style == HouseStyle::Village && rng.nextf() < 0.45f) {
        // Erker (bay window) with its own little gable roof
        hm.spec.hasBay = true;
        const float bw = w * 0.36f, bd = 0.55f, bh = wallH * 0.62f;
        appendBox(hm.details, {bw * 0.5f, bh * 0.5f, bd * 0.5f}, {0.f, 0.3f + bh * 0.5f, d * 0.5f + bd * 0.5f}, pal.wall);
        appendBox(hm.details, {0.22f, 0.3f, 0.03f}, {-bw * 0.22f, 0.3f + bh * 0.55f, d * 0.5f + bd + 0.02f}, pal.window);
        appendBox(hm.details, {0.22f, 0.3f, 0.03f}, {bw * 0.22f, 0.3f + bh * 0.55f, d * 0.5f + bd + 0.02f}, pal.window);
        fuhgl::render::MeshData bayRoof = gableRoof(bw, bd, bh * 0.45f, pal.roof);
        shiftY(bayRoof, 0.3f + bh);
        appendTransformed(hm.roof, bayRoof, fuhgl::math::translate({0.f, 0.f, d * 0.5f + bd * 0.5f}));
    }
    if (rng.nextf() < 0.3f && hm.spec.roofType != RoofType::Shed) {
        // Dachgaube (dormer) on the front slope
        hm.spec.hasDormers = true;
        const float dw = 0.8f, dh = 1.0f, dd = 0.5f;
        const float dx = rng.range(-w * 0.2f, w * 0.2f);
        const float dz = d * 0.5f * 0.55f, dy = wallH + roofH * 0.45f;
        appendBox(hm.details, {dw * 0.5f, dh * 0.5f, dd * 0.5f}, {dx, dy, dz + dd * 0.5f}, pal.wall);
        appendBox(hm.details, {0.3f, 0.4f, 0.03f}, {dx, dy, dz + dd + 0.02f}, pal.window);
        fuhgl::render::MeshData dr = gableRoof(dw, dd, 0.55f, pal.roof);
        shiftY(dr, dy + dh);
        appendTransformed(hm.roof, dr, fuhgl::math::translate({dx, 0.f, dz + dd * 0.5f}));
    }
    if (p.style != HouseStyle::Highrise && rng.nextf() < 0.35f) {
        // Anbau (side annex) with a lean-to roof
        hm.spec.hasAnnex = true;
        const float aw = w * 0.45f, ad = d * 0.7f, ah = wallH * 0.65f;
        fuhgl::render::MeshData annex = fuhgl::render::shapes::boxData({aw * 0.5f, ah * 0.5f, ad * 0.5f}, pal.wall);
        shiftY(annex, ah * 0.5f);
        appendTransformed(hm.walls, annex, fuhgl::math::translate({w * 0.5f + aw * 0.5f - 0.05f, 0.f, 0.f}));
        fuhgl::render::MeshData ar = shedRoof(aw, ad, ah * 0.3f, pal.roof);
        shiftY(ar, ah);
        appendTransformed(hm.roof, ar, fuhgl::math::translate({w * 0.5f + aw * 0.5f - 0.05f, 0.f, 0.f}));
    }
    if (rng.nextf() < 0.4f) {
        // Vordach: porch canopy over the door
        hm.spec.hasPorch = true;
        appendBox(hm.roof, {0.9f, 0.05f, 0.55f}, {0.f, 2.35f, d * 0.5f + 0.55f}, pal.roof);
    }
    // --- additive building history: a later phase docks volumes onto the core ---
    if (p.floors >= 2 && p.style != HouseStyle::Highrise && rng.nextf() < 0.3f) {
        // overhanging upper floor (Fachwerk-Obergeschoss) carried by diagonal struts (Konsolen)
        hm.spec.hasOverhang = true;
        const float ow = w * 0.6f, oy0 = p.floorHeight, oh2 = wallH - oy0, pz = 0.22f;
        fuhgl::render::MeshData up =
            fuhgl::render::shapes::boxData({ow * 0.5f, oh2 * 0.5f, d * 0.5f + pz}, pal.wall * 0.96f);
        shiftY(up, oy0 + oh2 * 0.5f);
        appendTransformed(hm.walls, up, fuhgl::math::translate({0.f, 0.f, pz * 0.5f}));
        for (int k = -1; k <= 1; ++k) {
            fuhgl::render::MeshData strut =
                fuhgl::render::shapes::boxData({0.05f, 0.36f, 0.05f}, pal.beam);
            appendTransformed(hm.details, strut,
                fuhgl::math::translate({k * ow * 0.32f, oy0 + 0.18f, d * 0.5f + pz * 0.5f + 0.2f}) *
                fuhgl::math::rotateX(0.75f));
        }
        // window in the overhang (slightly different material = later construction phase)
        appendBox(hm.details, {0.30f, 0.40f, 0.03f}, {-ow * 0.18f, oy0 + oh2 * 0.5f,
                  d * 0.5f + pz + 0.02f}, pal.window);
        appendBox(hm.glass, {0.25f, 0.34f, 0.015f}, {-ow * 0.18f, oy0 + oh2 * 0.5f,
                  d * 0.5f + pz + 0.045f}, pal.window);
    }
    if (p.style != HouseStyle::Highrise && rng.nextf() < 0.18f) {
        // stair tower (Treppenturm) at the back corner with a cone roof (CSG cone)
        const float tx = -w * 0.5f - 0.25f, tz = -d * 0.5f - 0.25f, th = wallH + 0.7f;
        fuhgl::render::MeshData tower =
            fuhgl::render::shapes::boxData({0.45f, th * 0.5f, 0.45f}, pal.wall);
        shiftY(tower, th * 0.5f);
        appendTransformed(hm.walls, tower, fuhgl::math::translate({tx, 0.f, tz}));
        appendSolid(hm.roof, fuhgl::render::makeCone(0.62f, 0.9f), 10, pal.roof,
                    fuhgl::math::translate({tx, th + 0.45f, tz}));
    }
    if (der.forceChimney || rng.nextf() < 0.6f) {
        hm.spec.hasChimney = true;
        // slightly tilted chimney (micro-jitter, ~±2°)
        fuhgl::render::MeshData ch =
            fuhgl::render::shapes::boxData({0.16f, (roofH * 0.7f + 0.4f) * 0.5f, 0.16f},
                                           {0.55f, 0.53f, 0.50f});
        const float tilt = (rng.nextf() - 0.5f) * 0.07f;
        appendTransformed(hm.details, ch,
            fuhgl::math::translate({w * 0.28f, wallH + roofH * 0.5f, -d * 0.22f}) *
            fuhgl::math::rotateZ(tilt));
    }
    if ((p.style == HouseStyle::Village || facade == FacadeMaterial::Wood) &&
        rng.nextf() < (facade == FacadeMaterial::Wood ? 0.9f : 0.5f)) {
        // Fachwerk: dark timber posts + bands + diagonal braces on the front facade
        // (almost always for wood construction)
        hm.spec.timberFrame = true;
        const float zf = d * 0.5f + 0.015f;
        for (int f = 0; f <= p.floors; ++f)
            appendBox(hm.details, {w * 0.5f + 0.02f, 0.07f, 0.02f}, {0.f, f * p.floorHeight, zf}, pal.beam);
        for (int s = -1; s <= 1; s += 1)
            appendBox(hm.details, {0.07f, wallH * 0.5f, 0.02f}, {s * w * 0.5f, wallH * 0.5f, zf}, pal.beam);
        appendBox(hm.details, {0.05f, wallH * 0.35f, 0.02f}, {-w * 0.18f, wallH * 0.5f, zf}, pal.beam);
        appendBox(hm.details, {0.05f, wallH * 0.35f, 0.02f}, {w * 0.18f, wallH * 0.5f, zf}, pal.beam);
    }

    // --- garden fence (low density / detached house): posts + rails around the front yard ---
    if (der.fence) {
        hm.spec.hasFence = true;
        const fuhgl::math::Vec3 fc = pal.beam;
        const float fx = w * 0.5f + 1.3f, fz = d * 0.5f + 1.3f;
        for (int i = 0; i <= 6; ++i) {
            const float t = static_cast<float>(i) / 6.f;
            appendBox(hm.details, {0.035f, 0.4f, 0.035f}, {-fx + 2.f * fx * t, 0.4f, fz}, fc);
            appendBox(hm.details, {0.035f, 0.4f, 0.035f}, {fx, 0.4f, -fz + 2.f * fz * t}, fc);
            appendBox(hm.details, {0.035f, 0.4f, 0.035f}, {-fx, 0.4f, -fz + 2.f * fz * t}, fc);
        }
        appendBox(hm.details, {fx, 0.03f, 0.03f}, {0.f, 0.62f, fz}, fc);   // front top rail
        appendBox(hm.details, {0.03f, 0.03f, fz}, {fx, 0.62f, 0.f}, fc);   // side top rails
        appendBox(hm.details, {0.03f, 0.03f, fz}, {-fx, 0.62f, 0.f}, fc);
        appendBox(hm.details, {fx, 0.025f, 0.025f}, {0.f, 0.3f, fz}, fc); // mid rails
        appendBox(hm.details, {0.025f, 0.025f, fz}, {fx, 0.3f, 0.f}, fc);
        appendBox(hm.details, {0.025f, 0.025f, fz}, {-fx, 0.3f, 0.f}, fc);
    }

    // --- wall patches (low wealth): corrugated-sheet / mixed-material patches on the facade ---
    if (der.patches) {
        hm.spec.hasPatches = true;
        const fuhgl::math::Vec3 patchCol = (rng.nextf() < 0.5f) ? fuhgl::math::Vec3{0.52f, 0.54f, 0.57f}
                                                              : pal.beam;
        const int n = 2 + static_cast<int>(rng.next() % 3u);
        for (int i = 0; i < n; ++i) {
            const float pw = rng.range(0.5f, 1.2f), ph2 = rng.range(0.4f, 0.9f);
            const float px = rng.range(-w * 0.35f, w * 0.35f);
            const float py = rng.range(0.5f, std::max(0.6f, wallH - 0.6f));
            const int side = (rng.nextf() < 0.7f) ? 1 : -1; // mostly front
            appendBox(hm.details, {pw * 0.5f, ph2 * 0.5f, 0.02f},
                      {px, py, side * (d * 0.5f + 0.025f)}, patchCol);
        }
    }

    // --- cornice bands per floor (high wealth): ornamental Simse on front + back ---
    if (der.cornice) {
        hm.spec.hasCornice = true;
        const fuhgl::math::Vec3 cc = pal.wall * 1.18f + fuhgl::math::Vec3{0.03f, 0.03f, 0.03f};
        for (int f = 1; f <= p.floors; ++f)
            for (int s = -1; s <= 1; s += 2)
                appendBox(hm.details, {w * 0.5f + 0.035f, 0.06f, 0.035f},
                          {0.f, f * p.floorHeight + 0.08f, s * (d * 0.5f + 0.035f)}, cc);
    }

    // --- human traces / clutter (props anchored at facade points; CSG solids where organic) ---
    if (p.clutter > 0.f && p.style != HouseStyle::Highrise) {
        const float cl = p.clutter;
        // flower boxes under the ground-floor front windows: hollow CSG box + flower spheres
        if (rng.nextf() < 0.55f * cl) {
            for (int s = -1; s <= 1; s += 2) {
                const float wx = s * w * 0.24f, wy = p.floorHeight * 0.45f - 0.26f * ws;
                fuhgl::render::SolidPtr box =
                    fuhgl::render::translated(fuhgl::render::makeCube({0.17f, 0.08f, 0.055f}),
                                               {0.f, 0.08f, 0.f}) -
                    fuhgl::render::translated(fuhgl::render::makeCube({0.14f, 0.07f, 0.05f}),
                                               {0.f, 0.10f, 0.f});
                appendSolid(hm.details, box, 8, pal.beam * 1.2f,
                            fuhgl::math::translate({wx, wy, d * 0.5f + 0.075f}));
                for (int k = 0; k < 2; ++k)
                    appendSolid(hm.details, fuhgl::render::makeSphere(0.05f), 5,
                                {0.75f, 0.25f + 0.25f * rng.nextf(), 0.3f},
                                fuhgl::math::translate({wx + (rng.nextf() - 0.5f) * 0.22f, wy + 0.17f,
                                                        d * 0.5f + 0.09f}));
            }
            ++hm.spec.props;
        }
        // AC unit on a side wall
        if (rng.nextf() < 0.3f * cl) {
            const float az = rng.range(-d * 0.2f, d * 0.2f), ay = p.floorHeight * 0.8f;
            appendBox(hm.details, {0.05f, 0.22f, 0.35f}, {w * 0.5f + 0.05f, ay, az},
                      {0.80f, 0.82f, 0.85f});
            appendBox(hm.details, {0.055f, 0.15f, 0.28f}, {w * 0.5f + 0.055f, ay, az},
                      {0.25f, 0.27f, 0.30f});
            ++hm.spec.props;
        }
        // satellite dish on the front wall (CSG sphere cap), tilted toward the sky
        if (rng.nextf() < 0.35f * cl) {
            fuhgl::render::SolidPtr dish =
                fuhgl::render::makeSphere(0.17f) &
                fuhgl::render::translated(fuhgl::render::makeCube({1.f, 0.1f, 1.f}), {0.f, 0.07f, 0.f});
            const float dx = w * 0.35f, dy = wallH * 0.55f;
            appendBox(hm.details, {0.03f, 0.26f, 0.03f}, {dx, dy, d * 0.5f + 0.02f},
                      {0.60f, 0.60f, 0.62f});
            appendSolid(hm.details, dish, 8, {0.85f, 0.87f, 0.90f},
                        fuhgl::math::translate({dx, dy + 0.29f, d * 0.5f + 0.05f}) *
                        fuhgl::math::rotateX(-0.7f));
            ++hm.spec.props;
        }
        // stacked firewood under the front eave (CSG union of lying cylinders)
        if (rng.nextf() < 0.45f * cl) {
            fuhgl::render::SolidPtr wood = fuhgl::render::makeCylinder(0.055f, 0.6f);
            for (int l = 0; l < 3; ++l)
                wood = wood | fuhgl::render::translated(fuhgl::render::makeCylinder(0.055f, 0.6f),
                                                        {-0.14f + l * 0.14f, 0.06f, 0.f});
            wood = wood | fuhgl::render::translated(fuhgl::render::makeCylinder(0.055f, 0.6f),
                                                    {0.f, 0.17f, 0.f});
            appendSolid(hm.details, wood, 8, pal.beam * 1.35f,
                        fuhgl::math::translate({-w * 0.32f, 0.06f, d * 0.5f + 0.45f}) *
                        fuhgl::math::rotateZ(fuhgl::math::kPi * 0.5f));
            ++hm.spec.props;
        }
        // clothesline in the front garden (only when there is a fence)
        if (der.fence && rng.nextf() < 0.6f * cl) {
            const float lx = (w * 0.5f + 1.3f) * 0.7f, lz = d * 0.5f + 0.9f;
            for (int s = -1; s <= 1; s += 2)
                appendBox(hm.details, {0.03f, 0.75f, 0.03f}, {s * lx, 0.75f, lz}, pal.beam);
            appendBox(hm.details, {lx, 0.012f, 0.012f}, {0.f, 1.48f, lz}, {0.8f, 0.8f, 0.78f});
            for (int c2 = 0; c2 < 2; ++c2) {
                fuhgl::render::MeshData cloth =
                    fuhgl::render::shapes::boxData({0.16f, 0.22f, 0.008f}, {0.9f, 0.9f, 0.92f});
                appendTransformed(hm.details, cloth,
                    fuhgl::math::translate({(c2 - 0.5f) * lx * 1.2f, 1.27f, lz}) *
                    fuhgl::math::rotateY((rng.nextf() - 0.5f) * 0.3f));
            }
            ++hm.spec.props;
        }
    }

    // --- ivy/vines climbing the front wall from the ground (humidity-driven growth) ---
    if (p.ivy && p.context.humidity > 0.55f && p.style != HouseStyle::Highrise) {
        Noise ivyN(p.seed ^ 0x1B7u);
        addIvy(hm.details, w, d, wallH, rng, ivyN, p.context.humidity, p.seed);
        hm.spec.hasIvy = true;
    }

    // --- flood zone / coast: lift the whole house onto posts over a high stone foundation ---
    if (der.stiltLift > 0.f && p.style != HouseStyle::Highrise) {
        const float lift = der.stiltLift;
        const float foundH = lift * 0.55f;
        shiftY(hm.walls, lift);
        shiftY(hm.details, lift);
        shiftY(hm.roof, lift);
        shiftY(hm.glass, lift);
        const fuhgl::math::Vec3 stoneCol = p.context.soilColor * 0.8f + fuhgl::math::Vec3{0.05f, 0.05f, 0.05f};
        fuhgl::render::MeshData found =
            fuhgl::render::shapes::boxData({w * 0.5f + 0.1f, foundH * 0.5f, d * 0.5f + 0.1f}, stoneCol);
        shiftY(found, foundH * 0.5f);
        appendTransformed(hm.walls, found, fuhgl::math::translate({0.f, 0.f, 0.f}));
        const fuhgl::math::Vec3 postCol = pal.beam * 0.8f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                appendBox(hm.details, {0.12f, (lift - foundH) * 0.5f, 0.12f},
                          {sx * (w * 0.5f - 0.18f), foundH + (lift - foundH) * 0.5f,
                           sz * (d * 0.5f - 0.18f)}, postCol);
        hm.spec.onStilts = true;
        hm.spec.stiltHeight = lift;
    }
    return hm;
}

} // namespace fuhglgraphics::procedural
