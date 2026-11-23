// G29 SDF: primitives + smooth-min + Mandelbulb DE, and meshFromSdf() which samples the field
// into a VolumeGrid and polygonises it with the G25 marching cubes (isolevel 0, negative
// inside — same convention as the SDF).

#include <fuhglgraphics/procedural/sdf.hpp>

#include <fuhglgraphics/procedural/marching_cubes.hpp>

#include <algorithm>
#include <cmath>

namespace fuhglgraphics::procedural {

namespace sdf {

float sphere(const fuhgl::math::Vec3& p, float r) { return fuhgl::math::length(p) - r; }

float box(const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& b) {
    const fuhgl::math::Vec3 q{std::fabs(p.x) - b.x, std::fabs(p.y) - b.y, std::fabs(p.z) - b.z};
    const float outside = fuhgl::math::length(fuhgl::math::Vec3{std::max(q.x, 0.f), std::max(q.y, 0.f),
                                                                std::max(q.z, 0.f)});
    return outside + std::min(std::max(q.x, std::max(q.y, q.z)), 0.f);
}

float roundedBox(const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& b, float r) {
    return box(p, b - fuhgl::math::Vec3{r, r, r}) - r;
}

float plane(const fuhgl::math::Vec3& p, float height) { return p.y - height; }

float cylinder(const fuhgl::math::Vec3& p, float radius, float height) {
    const float d = fuhgl::math::length(fuhgl::math::Vec2{p.x, p.z}) - radius;
    const float h = std::fabs(p.y - height * 0.5f) - height * 0.5f;
    const float outside = std::hypot(std::max(d, 0.f), std::max(h, 0.f));
    return outside + std::min(std::max(d, h), 0.f);
}

float smoothMin(float a, float b, float k) {
    const float h = std::max(k - std::fabs(a - b), 0.f) / k;
    return std::min(a, b) - h * h * h * k * (1.f / 6.f);
}

float mandelbulb(const fuhgl::math::Vec3& p, float power, int iterations) {
    fuhgl::math::Vec3 z = p;
    float dr = 1.f;
    float r = 0.f;
    constexpr float bailout = 2.f;
    for (int i = 0; i < iterations; ++i) {
        r = fuhgl::math::length(z);
        if (r > bailout) break;
        if (r < 1e-9f) {
            z = p;
            dr = 1.f;
            continue;
        }
        const float theta = std::acos(std::min(1.f, std::max(-1.f, z.z / r)));
        const float phi = std::atan2(z.y, z.x);
        dr = power * std::pow(r, power - 1.f) * dr + 1.f;
        const float zr = std::pow(r, power);
        const float tp = theta * power, pp = phi * power;
        z = fuhgl::math::Vec3{zr * std::sin(tp) * std::cos(pp), zr * std::sin(tp) * std::sin(pp),
                              zr * std::cos(tp)} +
            p;
    }
    if (r < 1e-9f) return 0.f;
    return 0.5f * std::log(r) * r / dr;
}

} // namespace sdf

fuhgl::render::MeshData meshFromSdf(const std::function<float(const fuhgl::math::Vec3&)>& field,
                                    const fuhgl::math::Vec3& min, const fuhgl::math::Vec3& max,
                                    int resolution, const fuhgl::math::Vec3& color) {
    const VolumeGrid grid = sampleField(field, min, max, resolution, resolution, resolution);
    return marchingCubes(grid, 0.f, color);
}

} // namespace fuhglgraphics::procedural
