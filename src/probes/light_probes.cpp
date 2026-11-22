#include "fuhglgraphics/probes/light_probes.hpp"

#include <fuhgl/fuhgl.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fuhglgraphics::probes {

namespace {

constexpr float kPi = 3.14159265358979323846f;

/// float -> IEEE half (binary16), good enough for SH coefficients.
std::uint16_t floatToHalf(float f) {
    std::uint32_t x = 0;
    std::memcpy(&x, &f, sizeof(x));
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    const int exp = static_cast<int>((x >> 23) & 0xffu) - 127 + 15;
    const std::uint32_t mant = x & 0x7fffffu;
    if (exp <= 0) return static_cast<std::uint16_t>(sign);          // underflow -> 0
    if (exp >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u); // overflow -> inf
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exp) << 10) | (mant >> 13));
}

/// Fibonacci sphere direction k of N (uniform over the sphere).
fuhgl::math::Vec3 fibonacciSphere(int k, int n) {
    const float phi = static_cast<float>(k) * 2.39996322972865332f; // golden angle
    const float z = 1.f - 2.f * (static_cast<float>(k) + 0.5f) / static_cast<float>(n);
    const float r = std::sqrt(std::max(1.f - z * z, 0.f));
    return {r * std::cos(phi), r * std::sin(phi), z};
}

/// Closest hit of ray ro+rd*t against the soup (t in (eps, bestT]); returns the index or -1.
int rayScene(const fuhgl::math::Vec3& ro, const fuhgl::math::Vec3& rd, const std::vector<ProbeTriangle>& tris,
             float maxT, float& tOut, fuhgl::math::Vec3& nOut) {
    int best = -1;
    float bestT = maxT;
    for (std::size_t i = 0; i < tris.size(); ++i) {
        float t = 0.f;
        fuhgl::math::Vec3 n{};
        if (rayTriangle(ro, rd, tris[i], t, n) && t > 1e-4f && t < bestT) {
            bestT = t;
            best = static_cast<int>(i);
            tOut = t;
            nOut = n;
        }
    }
    return best;
}

/// True when the segment from `from` along `dir` up to `maxDist` hits any triangle.
bool occluded(const fuhgl::math::Vec3& from, const fuhgl::math::Vec3& dir, float maxDist,
              const std::vector<ProbeTriangle>& tris) {
    float t = 0.f;
    fuhgl::math::Vec3 n{};
    return rayScene(from, dir, tris, maxDist, t, n) >= 0;
}

} // namespace

void shBasis(const fuhgl::math::Vec3& d, float y[9]) {
    const float c1 = 0.4886025119f;  // sqrt(3/4pi)
    const float c2 = 1.0925484306f;  // sqrt(15/4pi)
    const float c20 = 0.3153915653f; // sqrt(5/16pi)
    const float c22 = 0.5462742153f; // sqrt(15/16pi)
    y[0] = 0.2820947918f; // Y00 = sqrt(1/4pi)
    y[1] = c1 * d.y;      // Y1-1
    y[2] = c1 * d.z;      // Y10
    y[3] = c1 * d.x;      // Y11
    y[4] = c2 * d.x * d.y;             // Y2-2
    y[5] = c2 * d.y * d.z;             // Y2-1
    y[6] = c20 * (3.f * d.z * d.z - 1.f); // Y20
    y[7] = c2 * d.x * d.z;             // Y21
    y[8] = c22 * (d.x * d.x - d.y * d.y); // Y22
}

void shProject(const std::vector<std::pair<fuhgl::math::Vec3, fuhgl::math::Vec3>>& samples, Sh9& out) {
    out = Sh9{};
    if (samples.empty()) return;
    const float w = 4.f * kPi / static_cast<float>(samples.size());
    for (const auto& s : samples) {
        float y[9];
        shBasis(s.first, y);
        for (int i = 0; i < 9; ++i) out[static_cast<std::size_t>(i)] += s.second * (w * y[i]);
    }
}

fuhgl::math::Vec3 shIrradiance(const Sh9& c, const fuhgl::math::Vec3& n) {
    float y[9];
    shBasis(n, y);
    static const float A[9] = {kPi, 2.f * kPi / 3.f, 2.f * kPi / 3.f, 2.f * kPi / 3.f,
                               kPi / 4.f, kPi / 4.f, kPi / 4.f, kPi / 4.f, kPi / 4.f};
    fuhgl::math::Vec3 e{};
    for (int i = 0; i < 9; ++i) e += c[static_cast<std::size_t>(i)] * (A[i] * y[i]);
    return e;
}

bool rayTriangle(const fuhgl::math::Vec3& ro, const fuhgl::math::Vec3& rd, const ProbeTriangle& tri,
                 float& t, fuhgl::math::Vec3& n) {
    const fuhgl::math::Vec3 e1 = tri.b - tri.a;
    const fuhgl::math::Vec3 e2 = tri.c - tri.a;
    const fuhgl::math::Vec3 p = fuhgl::math::cross(rd, e2);
    const float det = fuhgl::math::dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;
    const float inv = 1.f / det;
    const fuhgl::math::Vec3 tv = ro - tri.a;
    const float u = fuhgl::math::dot(tv, p) * inv;
    if (u < 0.f || u > 1.f) return false;
    const fuhgl::math::Vec3 q = fuhgl::math::cross(tv, e1);
    const float v = fuhgl::math::dot(rd, q) * inv;
    if (v < 0.f || u + v > 1.f) return false;
    t = fuhgl::math::dot(e2, q) * inv;
    if (t <= 0.f) return false;
    n = fuhgl::math::normalize(fuhgl::math::cross(e1, e2));
    return true;
}

struct LightProbeGrid::Impl {
    std::array<fuhgl::Texture, kTextureCount> tex;
    int nx = 0, ny = 0, nz = 0;
    Impl() {
        for (auto& t : tex) t = fuhgl::Texture(fuhgl::Texture::Target::Tex3D);
    }
};

LightProbeGrid::LightProbeGrid() : impl_(std::make_unique<Impl>()) {}
LightProbeGrid::~LightProbeGrid() = default;
LightProbeGrid::LightProbeGrid(LightProbeGrid&&) noexcept = default;
LightProbeGrid& LightProbeGrid::operator=(LightProbeGrid&&) noexcept = default;

bool LightProbeGrid::create(int nx, int ny, int nz, const fuhgl::math::Vec3& min, const fuhgl::math::Vec3& max) {
    if (nx < 2 || ny < 2 || nz < 2) {
        std::fprintf(stderr, "probes::LightProbeGrid::create: dims must be >= 2 per axis\n");
        return false;
    }
    if (!(max.x > min.x && max.y > min.y && max.z > min.z)) {
        std::fprintf(stderr, "probes::LightProbeGrid::create: max must be > min on every axis\n");
        return false;
    }
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;
    min_ = min;
    max_ = max;
    coeffs_.assign(static_cast<std::size_t>(nx) * ny * nz, Sh9{});
    dirty_ = true;
    uploaded_ = false;
    return true;
}

fuhgl::math::Vec3 LightProbeGrid::cell() const {
    return {(max_.x - min_.x) / static_cast<float>(nx_ - 1), (max_.y - min_.y) / static_cast<float>(ny_ - 1),
            (max_.z - min_.z) / static_cast<float>(nz_ - 1)};
}

fuhgl::math::Vec3 LightProbeGrid::probePos(int x, int y, int z) const {
    const fuhgl::math::Vec3 c = cell();
    return {min_.x + static_cast<float>(x) * c.x, min_.y + static_cast<float>(y) * c.y,
            min_.z + static_cast<float>(z) * c.z};
}

bool LightProbeGrid::bake(const BakeInput& in) {
    if (coeffs_.empty()) {
        std::fprintf(stderr, "probes::LightProbeGrid::bake: grid not created\n");
        return false;
    }
    if (!in.skyRadiance) {
        std::fprintf(stderr, "probes::LightProbeGrid::bake: skyRadiance is required\n");
        return false;
    }
    const int n = std::max(8, in.raySamples);
    std::vector<std::pair<fuhgl::math::Vec3, fuhgl::math::Vec3>> samples;
    samples.reserve(static_cast<std::size_t>(n));
    for (int z = 0; z < nz_; ++z)
        for (int y = 0; y < ny_; ++y)
            for (int x = 0; x < nx_; ++x) {
                const fuhgl::math::Vec3 p = probePos(x, y, z);
                samples.clear();
                for (int k = 0; k < n; ++k) {
                    const fuhgl::math::Vec3 d = fibonacciSphere(k, n);
                    float t = 0.f;
                    fuhgl::math::Vec3 nHit{};
                    const int hit = rayScene(p, d, in.triangles, 1e9f, t, nHit);
                    if (hit < 0) {
                        samples.emplace_back(d, in.skyRadiance(d));
                        continue;
                    }
                    const ProbeTriangle& tri = in.triangles[static_cast<std::size_t>(hit)];
                    const fuhgl::math::Vec3 hn = fuhgl::math::dot(nHit, d) > 0.f ? -nHit : nHit;
                    const fuhgl::math::Vec3 hp = p + d * (t - 1e-3f);
                    // incoming radiance at the hit point from direction -d (one bounce):
                    // sky (visibility-tested) + direct lights (shadow-ray occluded)
                    fuhgl::math::Vec3 lin = tri.emissive;
                    {
                        const fuhgl::math::Vec3 sd = -d;
                        if (!occluded(hp + hn * 1e-3f, sd, 1e9f, in.triangles)) lin += in.skyRadiance(sd);
                    }
                    for (const fuhgl::render::Light& l : in.lights) {
                        fuhgl::math::Vec3 toL;
                        float maxDist = 1e9f;
                        if (l.type == fuhgl::render::LightType::Directional) {
                            toL = -fuhgl::math::normalize(l.direction);
                        } else {
                            toL = l.position - hp;
                            const float dist = fuhgl::math::length(toL);
                            toL = dist > 1e-6f ? toL / dist : fuhgl::math::Vec3{0.f, 1.f, 0.f};
                            maxDist = dist;
                        }
                        const float ndotl = fuhgl::math::dot(hn, toL);
                        if (ndotl <= 0.f) continue;
                        if (occluded(hp + hn * 1e-3f, toL, maxDist, in.triangles)) continue;
                        float atten = l.type == fuhgl::render::LightType::Directional
                                          ? l.intensity
                                          : l.attenuation(maxDist) * l.intensity;
                        if (l.type == fuhgl::render::LightType::Spot) atten *= l.coneFactor(toL);
                        lin += l.color * (atten * ndotl);
                    }
                    // Lambertian bounce toward the probe: albedo/pi * L_in * cos(theta)
                    const float cosTheta = std::max(fuhgl::math::dot(hn, -d), 0.f);
                    samples.emplace_back(d, tri.albedo * ((1.f / kPi) * cosTheta) * lin);
                }
                shProject(samples, coeffs_[index(x, y, z)]);
            }
    dirty_ = true;
    uploaded_ = false;
    return true;
}

bool LightProbeGrid::ensureUploaded() const {
    if (!dirty_ && uploaded_ && impl_->nx == nx_ && impl_->ny == ny_ && impl_->nz == nz_) return true;
    if (coeffs_.empty()) return false;
    // pack the 27 floats of each probe into kTextureCount RGBA16F texels, one 3D
    // texture per group of 4 floats (hardware trilinear filtering across the grid)
    const std::size_t probes = coeffs_.size();
    std::vector<float> fl(probes * 27, 0.f);
    for (std::size_t p = 0; p < probes; ++p)
        for (int i = 0; i < 9; ++i) {
            fl[p * 27 + 3 * i + 0] = coeffs_[p][static_cast<std::size_t>(i)].x;
            fl[p * 27 + 3 * i + 1] = coeffs_[p][static_cast<std::size_t>(i)].y;
            fl[p * 27 + 3 * i + 2] = coeffs_[p][static_cast<std::size_t>(i)].z;
        }
    std::vector<std::uint16_t> slice(probes * 4, 0);
    for (int k = 0; k < kTextureCount; ++k) {
        for (std::size_t p = 0; p < probes; ++p)
            for (int c = 0; c < 4; ++c) {
                const int fi = 4 * k + c;
                slice[p * 4 + c] = floatToHalf(fi < 27 ? fl[p * 27 + fi] : 0.f);
            }
        fuhgl::Texture& t = impl_->tex[static_cast<std::size_t>(k)];
        t.bind();
        t.setImage3D(0, fuhgl::gl::GL_RGBA16F, nx_, ny_, nz_, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_HALF_FLOAT,
                     slice.data());
        t.setParameteri(fuhgl::gl::GL_TEXTURE_MIN_FILTER, fuhgl::gl::GL_LINEAR);
        t.setParameteri(fuhgl::gl::GL_TEXTURE_MAG_FILTER, fuhgl::gl::GL_LINEAR);
        t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_S, fuhgl::gl::GL_CLAMP_TO_EDGE);
        t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_T, fuhgl::gl::GL_CLAMP_TO_EDGE);
        t.setParameteri(fuhgl::gl::GL_TEXTURE_WRAP_R, fuhgl::gl::GL_CLAMP_TO_EDGE);
    }
    impl_->nx = nx_;
    impl_->ny = ny_;
    impl_->nz = nz_;
    dirty_ = false;
    uploaded_ = true;
    return true;
}

const fuhgl::Texture& LightProbeGrid::texture(int i) const { return impl_->tex[static_cast<std::size_t>(i)]; }

} // namespace fuhglgraphics::probes
