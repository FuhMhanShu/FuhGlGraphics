// HouseGenerator implementation: winding-safe mesh helpers (shading normal derived from the
// triangle winding, CCW seen from outside -> nothing is culled), four roof solids (gable /
// lean-to / hipped / flat) and the feature set (bay, dormers, annex, porch, chimney, timber
// frame, highrise window grid + setback). Deterministic per seed via splitmix32.

#include <fuhglgraphics/procedural/house.hpp>

#include <fuhgl/render.hpp>

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

fuhgl::render::MeshData gableRoof(float w, float d, float h, const fuhgl::math::Vec3& col) {
    const float hw = w * 0.5f + 0.14f, hd = d * 0.5f + 0.14f;
    fuhgl::render::MeshData md;
    addQuad(md, {-hw, 0.f, hd}, {hw, 0.f, hd}, {hw, h, 0.f}, {-hw, h, 0.f}, col);   // south slope
    addQuad(md, {hw, 0.f, -hd}, {-hw, 0.f, -hd}, {-hw, h, 0.f}, {hw, h, 0.f}, col);  // north slope
    addTri(md, {hw, 0.f, hd}, {hw, 0.f, -hd}, {hw, h, 0.f}, col);                    // east gable
    addTri(md, {-hw, 0.f, -hd}, {-hw, 0.f, hd}, {-hw, h, 0.f}, col);                 // west gable
    return md;
}

fuhgl::render::MeshData shedRoof(float w, float d, float h, const fuhgl::math::Vec3& col) {
    const float hw = w * 0.5f + 0.14f, hd = d * 0.5f + 0.14f;
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

fuhgl::render::MeshData hippedRoof(float w, float d, float h, const fuhgl::math::Vec3& col) {
    const float hw = w * 0.5f + 0.14f, hd = d * 0.5f + 0.14f;
    const float rh = std::max(0.f, hw - h); // ridge half-length (hip run ~ h horizontally)
    fuhgl::render::MeshData md;
    addQuad(md, {-hw, 0.f, hd}, {hw, 0.f, hd}, {rh, h, 0.f}, {-rh, h, 0.f}, col);  // south slope
    addQuad(md, {hw, 0.f, -hd}, {-hw, 0.f, -hd}, {-rh, h, 0.f}, {rh, h, 0.f}, col); // north slope
    addTri(md, {hw, 0.f, hd}, {hw, 0.f, -hd}, {rh, h, 0.f}, col);                  // east hip
    addTri(md, {-hw, 0.f, -hd}, {-hw, 0.f, hd}, {-rh, h, 0.f}, col);               // west hip
    return md;
}

fuhgl::render::MeshData flatRoof(float w, float d, float t, const fuhgl::math::Vec3& col) {
    fuhgl::render::MeshData md =
        fuhgl::render::shapes::boxData({w * 0.5f + 0.1f, t * 0.5f, d * 0.5f + 0.1f}, col);
    shiftY(md, t * 0.5f);
    return md;
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

} // namespace

HouseGenerator::Params HouseGenerator::randomParams(HouseStyle style, float cellSize, unsigned seed) {
    Rng rng(seed);
    Params p;
    p.style = style;
    p.seed = seed;
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

HouseMesh HouseGenerator::generate(const Params& p) const {
    Rng rng(p.seed);
    HouseMesh hm;
    hm.spec.width = p.width;
    hm.spec.depth = p.depth;
    hm.spec.floors = p.floors;
    const float w = p.width, d = p.depth;
    const float wallH = p.floors * p.floorHeight;
    hm.spec.wallHeight = wallH;
    const Palette pal = pickPalette(p.style, rng);

    // --- walls ---
    hm.walls = fuhgl::render::shapes::boxData({w * 0.5f, wallH * 0.5f, d * 0.5f}, pal.wall);
    shiftY(hm.walls, wallH * 0.5f);

    // --- door (front = +z) ---
    appendBox(hm.details, {0.45f, 0.95f, 0.04f}, {0.f, 0.95f, d * 0.5f + 0.03f}, pal.door);

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

    // --- windows per floor, front + back ---
    for (int f = 0; f < p.floors; ++f) {
        const float y = p.floorHeight * (0.45f + f * 1.0f);
        if (y > wallH - 0.7f) break;
        for (int s = -1; s <= 1; s += 2) {
            appendBox(hm.details, {0.30f, 0.42f, 0.03f}, {s * w * 0.24f, y, d * 0.5f + 0.02f}, pal.window);
            appendBox(hm.details, {0.30f, 0.42f, 0.03f}, {s * w * 0.24f, y, -d * 0.5f - 0.02f}, pal.window);
        }
    }

    // --- roof: gable / lean-to / hipped ---
    const float roofRoll = rng.nextf();
    const float roofH = std::max(1.1f, wallH * (p.style == HouseStyle::Village ? 0.5f : 0.38f));
    if (roofRoll < 0.45f) {
        hm.roof = gableRoof(w, d, roofH, pal.roof);
        hm.spec.roofType = RoofType::Gable;
    } else if (roofRoll < 0.7f) {
        hm.roof = shedRoof(w, d, roofH, pal.roof);
        hm.spec.roofType = RoofType::Shed;
    } else {
        hm.roof = hippedRoof(w, d, roofH, pal.roof);
        hm.spec.roofType = RoofType::Hipped;
    }
    shiftY(hm.roof, wallH);

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
    if (rng.nextf() < 0.6f) {
        hm.spec.hasChimney = true;
        appendBox(hm.details, {0.16f, roofH * 0.7f + 0.4f, 0.16f},
                  {w * 0.28f, wallH + roofH * 0.5f, -d * 0.22f}, {0.55f, 0.53f, 0.50f});
    }
    if (p.style == HouseStyle::Village && rng.nextf() < 0.5f) {
        // Fachwerk: dark timber posts + bands + diagonal braces on the front facade
        hm.spec.timberFrame = true;
        const float zf = d * 0.5f + 0.015f;
        for (int f = 0; f <= p.floors; ++f)
            appendBox(hm.details, {w * 0.5f + 0.02f, 0.07f, 0.02f}, {0.f, f * p.floorHeight, zf}, pal.beam);
        for (int s = -1; s <= 1; s += 1)
            appendBox(hm.details, {0.07f, wallH * 0.5f, 0.02f}, {s * w * 0.5f, wallH * 0.5f, zf}, pal.beam);
        appendBox(hm.details, {0.05f, wallH * 0.35f, 0.02f}, {-w * 0.18f, wallH * 0.5f, zf}, pal.beam);
        appendBox(hm.details, {0.05f, wallH * 0.35f, 0.02f}, {w * 0.18f, wallH * 0.5f, zf}, pal.beam);
    }
    return hm;
}

} // namespace fuhglgraphics::procedural
