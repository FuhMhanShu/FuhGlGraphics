#include "fuhglgraphics/water/water_surface.hpp"

#include <fuhgl/gl/info.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fuhglgraphics::water {

void evalGerstner(const GerstnerWave* waves, int count, float time,
                  const fuhgl::math::Vec2& xz, fuhgl::math::Vec3& posOut,
                  fuhgl::math::Vec3* normalOut) {
    using fuhgl::math::Vec2;
    using fuhgl::math::Vec3;
    Vec3 p{xz.x, 0.f, xz.y};
    Vec3 dPdx{1.f, 0.f, 0.f}; // d(displaced)/d(x)
    Vec3 dPdz{0.f, 0.f, 1.f}; // d(displaced)/d(z)
    const int n = std::min(count, WaterSurface::kMaxWaves);
    for (int i = 0; i < n; ++i) {
        const Vec2 d = fuhgl::math::normalize(waves[i].direction);
        const float k = 2.f * fuhgl::math::kPi / std::max(waves[i].wavelength, 1e-3f);
        const float omega = k * waves[i].speed;
        const float a = waves[i].amplitude;
        // standard normalization: Q_i = steepness / (k * A * numWaves)
        const float q = waves[i].steepness / std::max(k * a * static_cast<float>(n), 1e-6f);
        const float phase = k * (d.x * xz.x + d.y * xz.y) - omega * time;
        const float s = std::sin(phase), c = std::cos(phase);
        p.x += q * a * d.x * c;
        p.z += q * a * d.y * c;
        p.y += a * s;
        const float wa = k * a;
        dPdx = dPdx + Vec3{-q * d.x * d.x * wa * s, d.x * wa * c, -q * d.x * d.y * wa * s};
        dPdz = dPdz + Vec3{-q * d.x * d.y * wa * s, d.y * wa * c, -q * d.y * d.y * wa * s};
    }
    posOut = p;
    if (normalOut) *normalOut = fuhgl::math::normalize(fuhgl::math::cross(dPdz, dPdx));
}

fuhgl::math::Mat4 reflectionMatrix(const fuhgl::math::Vec3& planeN, float planeD) {
    const fuhgl::math::Vec3 n = fuhgl::math::normalize(planeN);
    fuhgl::math::Mat4 m{}; // identity; Mat4::operator()(col, row)
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m(c, r) = (r == c ? 1.f : 0.f) - 2.f * n[r] * n[c];
    for (int r = 0; r < 3; ++r)
        m(3, r) = -2.f * planeD * n[r];
    m(3, 3) = 1.f;
    return m;
}

void waterPlaneFromModel(const fuhgl::math::Mat4& model, fuhgl::math::Vec3& planeN, float& planeD) {
    const fuhgl::math::Mat3 r{model};
    const fuhgl::math::Vec3 n = fuhgl::math::normalize(r * fuhgl::math::Vec3{0.f, 1.f, 0.f});
    const fuhgl::math::Vec4 p0 = model * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
    planeN = n;
    planeD = -fuhgl::math::dot(n, fuhgl::math::Vec3{p0.x, p0.y, p0.z});
}

bool WaterSurface::create(int resolution, float size) {
    if (!fuhgl::gl::isInitialized()) {
        std::fprintf(stderr, "water::WaterSurface::create: no GL context\n");
        return false;
    }
    resolution = std::max(resolution, 2);
    const int n = resolution;
    fuhgl::render::MeshData data;
    data.vertices.reserve(static_cast<std::size_t>(n) * n * fuhgl::render::MeshData::kFloatsPerVertex);
    const float half = size * 0.5f;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float x = -half + size * static_cast<float>(i) / static_cast<float>(n - 1);
            const float z = -half + size * static_cast<float>(j) / static_cast<float>(n - 1);
            const float u = static_cast<float>(i) / static_cast<float>(n - 1);
            const float v = static_cast<float>(j) / static_cast<float>(n - 1);
            const float vtx[11] = {x, 0.f, z, 0.f, 1.f, 0.f, 1.f, 1.f, 1.f, u, v};
            data.vertices.insert(data.vertices.end(), vtx, vtx + 11);
        }
    }
    data.indices.reserve(static_cast<std::size_t>(n - 1) * (n - 1) * 6);
    for (int j = 0; j < n - 1; ++j) {
        for (int i = 0; i < n - 1; ++i) {
            const unsigned int a = static_cast<unsigned int>(j * n + i);
            const unsigned int b = a + 1, c = a + static_cast<unsigned int>(n), d = c + 1;
            // CCW seen from +Y
            data.indices.insert(data.indices.end(), {a, c, b, b, c, d});
        }
    }
    if (!mesh_.create()) return false;
    mesh_.setMeshData(data);
    ready_ = true;
    return true;
}

float WaterSurface::maxWaveHeight() const {
    float sum = 0.f;
    for (const GerstnerWave& w : waves) sum += std::fabs(w.amplitude);
    return sum;
}

void WaterSurface::setDefaultWaves() {
    waves = {
        {{1.f, 0.f}, 0.28f, 14.f, 1.9f, 0.65f},
        {{0.7f, 0.7f}, 0.18f, 7.5f, 1.3f, 0.55f},
        {{-0.4f, 0.9f}, 0.10f, 4.2f, 0.9f, 0.45f},
        {{0.2f, -1.f}, 0.06f, 2.6f, 0.7f, 0.35f},
    };
}

} // namespace fuhglgraphics::water
