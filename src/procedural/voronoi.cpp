// G28 Voronoi: seeded points, cells by half-plane clipping (Sutherland-Hodgman against the
// perpendicular bisector of each seed pair), adjacency from shared polygon edges, F1/F2
// distances, and prism extrusion of cells into MeshData.

#include <fuhglgraphics/procedural/voronoi.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace fuhglgraphics::procedural {

namespace {

struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    std::uint32_t next() {
        s += 0x9E3779B9u;
        std::uint32_t z = s;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }
    float uniform() { return next() * (1.f / 4294967296.f); }
};

float cross2(const fuhgl::math::Vec2& a, const fuhgl::math::Vec2& b) { return a.x * b.y - a.y * b.x; }

// clip polygon (CCW) by the half-plane dot(p - mid, q - seed) <= 0 (points closer to seed)
std::vector<fuhgl::math::Vec2> clipBisector(const std::vector<fuhgl::math::Vec2>& poly,
                                             const fuhgl::math::Vec2& seed,
                                             const fuhgl::math::Vec2& q) {
    std::vector<fuhgl::math::Vec2> out;
    const fuhgl::math::Vec2 mid = (seed + q) * 0.5f;
    const fuhgl::math::Vec2 n = q - seed; // bisector normal (points away from seed)
    const int m = static_cast<int>(poly.size());
    for (int i = 0; i < m; ++i) {
        const fuhgl::math::Vec2& a = poly[i];
        const fuhgl::math::Vec2& b = poly[(i + 1) % m];
        const float da = fuhgl::math::dot(a - mid, n);
        const float db = fuhgl::math::dot(b - mid, n);
        const bool ina = da <= 1e-7f, inb = db <= 1e-7f;
        if (ina) out.push_back(a);
        if (ina != inb) {
            const float t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

double polyArea(const std::vector<fuhgl::math::Vec2>& poly) {
    double a = 0;
    const int m = static_cast<int>(poly.size());
    for (int i = 0; i < m; ++i) {
        const auto& p = poly[i];
        const auto& q = poly[(i + 1) % m];
        a += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    return std::fabs(a) * 0.5;
}

} // namespace

Voronoi2D voronoi2d(int n, const fuhgl::math::Vec2& min, const fuhgl::math::Vec2& max, unsigned seed) {
    Voronoi2D v;
    v.min = min;
    v.max = max;
    Rng rng(seed);
    const float w = max.x - min.x, h = max.y - min.y;
    v.points.reserve(n);
    for (int i = 0; i < n; ++i)
        v.points.push_back({min.x + rng.uniform() * w, min.y + rng.uniform() * h});
    // cell of seed i = bounds rect clipped by the bisector half-plane against every other seed
    std::vector<fuhgl::math::Vec2> rect = {{min.x, min.y}, {max.x, min.y}, {max.x, max.y}, {min.x, max.y}};
    v.cells.resize(n);
    for (int i = 0; i < n; ++i) {
        std::vector<fuhgl::math::Vec2> poly = rect;
        for (int j = 0; j < n && !poly.empty(); ++j)
            if (j != i) poly = clipBisector(poly, v.points[i], v.points[j]);
        v.cells[i] = std::move(poly);
    }
    // adjacency: pairs of seeds whose cells share >= 2 polygon vertices (a common edge)
    const float eps2 = 1e-8f * (w * w + h * h);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            int cnt = 0;
            for (const auto& p : v.cells[i])
                for (const auto& q : v.cells[j])
                    if (fuhgl::math::lengthSq(p - q) < eps2) ++cnt;
            if (cnt >= 2) v.edges.push_back({i, j});
        }
    return v;
}

void voronoiDistances(const Voronoi2D& v, const fuhgl::math::Vec2& p, float& f1, float& f2) {
    f1 = 1e30f;
    f2 = 1e30f;
    for (const auto& s : v.points) {
        const float d = fuhgl::math::length(p - s);
        if (d < f1) {
            f2 = f1;
            f1 = d;
        } else if (d < f2) {
            f2 = d;
        }
    }
}

fuhgl::render::MeshData extrudeCells(const Voronoi2D& v, float y0, float y1, float gap,
                                     float heightJitter, unsigned jitterSeed,
                                     const fuhgl::math::Vec3& color, float colorJitter) {
    fuhgl::render::MeshData md;
    Rng rng(jitterSeed);
    const int n = static_cast<int>(v.cells.size());
    for (int i = 0; i < n; ++i) {
        auto poly = v.cells[i];
        if (poly.size() < 3) continue;
        // centroid + shrink towards it (gap -> cracks between plates)
        fuhgl::math::Vec2 c{0.f, 0.f};
        for (const auto& p : poly) c = c + p;
        c = c / static_cast<float>(poly.size());
        if (gap > 0.f)
            for (auto& p : poly) p = c + (p - c) * (1.f - gap);
        const float top = y1 + (heightJitter > 0.f ? rng.uniform() * heightJitter : 0.f);
        // per-cell color variation
        const float cj = 1.f - colorJitter + 2.f * colorJitter * rng.uniform();
        const fuhgl::math::Vec3 col = color * cj;
        const int m = static_cast<int>(poly.size());
        const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
        auto pushV = [&](const fuhgl::math::Vec2& p, float y, const fuhgl::math::Vec3& nrm) {
            md.vertices.push_back(p.x); md.vertices.push_back(y); md.vertices.push_back(p.y);
            md.vertices.push_back(nrm.x); md.vertices.push_back(nrm.y); md.vertices.push_back(nrm.z);
            md.vertices.push_back(col.x); md.vertices.push_back(col.y); md.vertices.push_back(col.z);
            md.vertices.push_back(p.x); md.vertices.push_back(p.y);
        };
        // bottom ring (y0, normal down), top ring (top, normal up)
        for (int k = 0; k < m; ++k) pushV(poly[k], y0, {0.f, -1.f, 0.f});
        for (int k = 0; k < m; ++k) pushV(poly[k], top, {0.f, 1.f, 0.f});
        // side ring: two vertices per polygon vertex (outward normal)
        for (int k = 0; k < m; ++k) {
            const fuhgl::math::Vec2& a = poly[k];
            const fuhgl::math::Vec2& b = poly[(k + 1) % m];
            const fuhgl::math::Vec2 e = b - a;
            const fuhgl::math::Vec2 nrm = fuhgl::math::normalize(fuhgl::math::Vec2{e.y, -e.x}); // outward for CCW
            pushV(a, y0, {nrm.x, 0.f, nrm.y});
            pushV(a, top, {nrm.x, 0.f, nrm.y});
        }
        // bottom fan (normal down -> reverse winding), top fan
        for (int k = 1; k < m - 1; ++k) {
            md.indices.push_back(base + 0); md.indices.push_back(base + k + 1); md.indices.push_back(base + k);
            md.indices.push_back(base + m + 0); md.indices.push_back(base + m + k); md.indices.push_back(base + m + k + 1);
        }
        // sides
        for (int k = 0; k < m; ++k) {
            const std::uint32_t s = base + 2 * m + 2 * k;
            const std::uint32_t s2 = base + 2 * m + 2 * ((k + 1) % m);
            md.indices.push_back(s); md.indices.push_back(s + 1); md.indices.push_back(s2);
            md.indices.push_back(s2); md.indices.push_back(s + 1); md.indices.push_back(s2 + 1);
        }
    }
    return md;
}

} // namespace fuhglgraphics::procedural
