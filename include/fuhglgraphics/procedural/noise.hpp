#pragma once

/// @file noise.hpp
/// @brief G24: seeded, deterministic CPU noise — value, Perlin (gradient) and Simplex
/// noise in 2D + 3D, plus fBm / ridged / turbulence fractals and domain warping.
/// Header-only value type (like fuhgl::math::Noise): no GL, no allocation, no I/O.
/// Same seed -> same values on every platform. Shared by the G25-G29 generators
/// (marching cubes volumes, L-system jitter, WFC seeds, voronoi jitter, SDF fields)
/// and usable for heightmaps / textures / clouds.

#include <fuhgl/math.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Piecewise 1D mapping curve over sorted control points (x, y) with smoothstep
/// interpolation between knots and clamped ends. Maps a noise value (typically in [0,1])
/// through user breakpoints — e.g. continentalness -> deep ocean / coast / plains /
/// steep mountains (Minecraft-style continentalness curve).
struct Spline1D {
    std::vector<std::pair<float, float>> pts; ///< (x, y) knots, x ascending

    /// @brief Evaluate the curve at `x` (clamped to the end knots outside the range).
    float eval(float x) const {
        if (pts.empty()) return x;
        if (x <= pts.front().first) return pts.front().second;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            if (x <= pts[i].first) {
                const float x0 = pts[i - 1].first, y0 = pts[i - 1].second;
                const float x1 = pts[i].first, y1 = pts[i].second;
                const float t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.f;
                const float sm = t * t * (3.f - 2.f * t); // smoothstep between knots
                return y0 + (y1 - y0) * sm;
            }
        }
        return pts.back().second;
    }
};

/// @brief Seeded noise source. Copyable value type (~1 KiB: two permutation tables).
/// Perlin delegates to fuhgl::math::Noise (improved Perlin); value/simplex use their
/// own seeded permutation built with splitmix32.
class Noise {
public:
    /// @brief Which base noise the fractal helpers (fbm/ridged/turbulence) sum over.
    enum class Base { Value, Perlin, Simplex };

    explicit Noise(std::uint32_t seed = 0) : seed_(seed), perlin_(seed) {
        std::uint8_t base[256];
        for (int i = 0; i < 256; ++i) base[i] = static_cast<std::uint8_t>(i);
        std::uint32_t s = seed;
        for (int i = 255; i > 0; --i) {
            s += 0x9E3779B9u;
            std::uint32_t z = s;
            z = (z ^ (z >> 16)) * 0x21F0AAADu;
            z = (z ^ (z >> 15)) * 0x735A2D97u;
            z ^= z >> 15;
            const int j = static_cast<int>(z % static_cast<std::uint32_t>(i + 1));
            const std::uint8_t t = base[i];
            base[i] = base[j];
            base[j] = t;
        }
        for (int i = 0; i < 512; ++i) perm_[i] = base[i & 255];
    }

    std::uint32_t seed() const { return seed_; }

    // --- base noises ---------------------------------------------------------------

    /// @brief Value noise 2D: bilinear interpolation of hashed lattice values, [0, 1].
    float value2(float x, float y) const {
        const int xi = static_cast<int>(std::floor(x));
        const int yi = static_cast<int>(std::floor(y));
        const float fx = x - xi, fy = y - yi;
        const float u = fade(fx), v = fade(fy);
        const float a = lattice2(xi, yi), b = lattice2(xi + 1, yi);
        const float c = lattice2(xi, yi + 1), d = lattice2(xi + 1, yi + 1);
        return lerp(lerp(a, b, u), lerp(c, d, u), v);
    }

    /// @brief Value noise 3D: trilinear interpolation of hashed lattice values, [0, 1].
    float value3(float x, float y, float z) const {
        const int xi = static_cast<int>(std::floor(x));
        const int yi = static_cast<int>(std::floor(y));
        const int zi = static_cast<int>(std::floor(z));
        const float fx = x - xi, fy = y - yi, fz = z - zi;
        const float u = fade(fx), v = fade(fy), w = fade(fz);
        const float c000 = lattice3(xi, yi, zi), c100 = lattice3(xi + 1, yi, zi);
        const float c010 = lattice3(xi, yi + 1, zi), c110 = lattice3(xi + 1, yi + 1, zi);
        const float c001 = lattice3(xi, yi, zi + 1), c101 = lattice3(xi + 1, yi, zi + 1);
        const float c011 = lattice3(xi, yi + 1, zi + 1), c111 = lattice3(xi + 1, yi + 1, zi + 1);
        const float x00 = lerp(c000, c100, u), x10 = lerp(c010, c110, u);
        const float x01 = lerp(c001, c101, u), x11 = lerp(c011, c111, u);
        const float y0 = lerp(x00, x10, v), y1 = lerp(x01, x11, v);
        return lerp(y0, y1, w);
    }

    /// @brief Improved Perlin gradient noise 2D (delegates to fuhgl::math::Noise), ~[-1, 1].
    float perlin2(float x, float y) const { return perlin_.perlin2(x, y); }
    /// @brief Improved Perlin gradient noise 3D, ~[-1, 1].
    float perlin3(float x, float y, float z) const { return perlin_.perlin3(x, y, z); }

    /// @brief Simplex noise 2D (Gustavson), scaled to ~[-1, 1].
    float simplex2(float x, float y) const {
        constexpr float F2 = 0.366025403784439f;  // (sqrt(3)-1)/2
        constexpr float G2 = 0.211324865405187f;  // (3-sqrt(3))/6
        const float s = (x + y) * F2;
        const int i = static_cast<int>(std::floor(x + s));
        const int j = static_cast<int>(std::floor(y + s));
        const float t = (i + j) * G2;
        const float x0 = x - (i - t), y0 = y - (j - t);
        const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
        const float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
        const float x2 = x0 - 1.f + 2.f * G2, y2 = y0 - 1.f + 2.f * G2;
        float n = 0.f;
        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 > 0.f) { t0 *= t0; n += t0 * t0 * grad2(perm_[(perm_[i & 255] + j) & 255] & 7, x0, y0); }
        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 > 0.f) { t1 *= t1; n += t1 * t1 * grad2(perm_[(perm_[(i + i1) & 255] + (j + j1)) & 255] & 7, x1, y1); }
        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 > 0.f) { t2 *= t2; n += t2 * t2 * grad2(perm_[(perm_[(i + 1) & 255] + (j + 1)) & 255] & 7, x2, y2); }
        return 70.f * n;
    }

    /// @brief Simplex noise 3D (Gustavson), scaled to ~[-1, 1].
    float simplex3(float x, float y, float z) const {
        constexpr float F3 = 1.f / 3.f, G3 = 1.f / 6.f;
        const float s = (x + y + z) * F3;
        const int i = static_cast<int>(std::floor(x + s));
        const int j = static_cast<int>(std::floor(y + s));
        const int k = static_cast<int>(std::floor(z + s));
        const float t = (i + j + k) * G3;
        const float x0 = x - (i - t), y0 = y - (j - t), z0 = z - (k - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        } else {
            if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
            else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
            else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        }
        const float x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
        const float x2 = x0 - i2 + 2.f * G3, y2 = y0 - j2 + 2.f * G3, z2 = z0 - k2 + 2.f * G3;
        const float x3 = x0 - 1.f + 3.f * G3, y3 = y0 - 1.f + 3.f * G3, z3 = z0 - 1.f + 3.f * G3;
        float n = 0.f;
        float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
        if (t0 > 0.f) { t0 *= t0; n += t0 * t0 * grad3(perm_[(perm_[(perm_[i & 255] + j) & 255] + k) & 255] & 11, x0, y0, z0); }
        float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
        if (t1 > 0.f) { t1 *= t1; n += t1 * t1 * grad3(perm_[(perm_[(perm_[(i + i1) & 255] + (j + j1)) & 255] + (k + k1)) & 255] & 11, x1, y1, z1); }
        float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
        if (t2 > 0.f) { t2 *= t2; n += t2 * t2 * grad3(perm_[(perm_[(perm_[(i + i2) & 255] + (j + j2)) & 255] + (k + k2)) & 255] & 11, x2, y2, z2); }
        float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
        if (t3 > 0.f) { t3 *= t3; n += t3 * t3 * grad3(perm_[(perm_[(perm_[(i + 1) & 255] + (j + 1)) & 255] + (k + 1)) & 255] & 11, x3, y3, z3); }
        return 32.f * n;
    }

    // --- fractal helpers over a chosen base ----------------------------------------

    /// @brief Base noise dispatch.
    float base2(Base b, float x, float y) const {
        switch (b) {
            case Base::Value: return value2(x, y) * 2.f - 1.f;
            case Base::Simplex: return simplex2(x, y);
            case Base::Perlin:
            default: return perlin2(x, y);
        }
    }
    /// @brief Base noise dispatch (3D).
    float base3(Base b, float x, float y, float z) const {
        switch (b) {
            case Base::Value: return value3(x, y, z) * 2.f - 1.f;
            case Base::Simplex: return simplex3(x, y, z);
            case Base::Perlin:
            default: return perlin3(x, y, z);
        }
    }

    /// @brief Fractal Brownian motion, amplitude-normalized (stays in the base range).
    float fbm2(float x, float y, int octaves, Base base = Base::Perlin,
               float lacunarity = 2.f, float gain = 0.5f) const {
        float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * base2(base, x * freq, y * freq);
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }
    /// @brief Fractal Brownian motion 3D, amplitude-normalized.
    float fbm3(float x, float y, float z, int octaves, Base base = Base::Perlin,
               float lacunarity = 2.f, float gain = 0.5f) const {
        float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * base3(base, x * freq, y * freq, z * freq);
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }

    /// @brief Ridged multifractal: 1-|n| squared per octave — sharp crests for mountains, [0, 1].
    float ridged2(float x, float y, int octaves, Base base = Base::Perlin,
                  float lacunarity = 2.f, float gain = 0.5f) const {
        float sum = 0.f, amp = 0.5f, freq = 1.f, norm = 0.f;
        for (int o = 0; o < octaves; ++o) {
            const float r = 1.f - std::fabs(base2(base, x * freq, y * freq));
            sum += amp * r * r;
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }
    /// @brief Ridged multifractal 3D, [0, 1].
    float ridged3(float x, float y, float z, int octaves, Base base = Base::Perlin,
                  float lacunarity = 2.f, float gain = 0.5f) const {
        float sum = 0.f, amp = 0.5f, freq = 1.f, norm = 0.f;
        for (int o = 0; o < octaves; ++o) {
            const float r = 1.f - std::fabs(base3(base, x * freq, y * freq, z * freq));
            sum += amp * r * r;
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }

    /// @brief Turbulence: sum of |n| per octave, [0, 1] — billowy clouds.
    float turbulence2(float x, float y, int octaves, Base base = Base::Perlin,
                      float lacunarity = 2.f, float gain = 0.5f) const {
        float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * std::fabs(base2(base, x * freq, y * freq));
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }
    /// @brief Turbulence 3D, [0, 1].
    float turbulence3(float x, float y, float z, int octaves, Base base = Base::Perlin,
                      float lacunarity = 2.f, float gain = 0.5f) const {
        float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * std::fabs(base3(base, x * freq, y * freq, z * freq));
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }

    // --- domain warp ---------------------------------------------------------------

    /// @brief Domain-warped 2D coordinates: p + strength * fBm(p), fBm(p + offset).
    fuhgl::math::Vec2 warp2(float x, float y, float strength, int octaves = 2) const {
        const float qx = fbm2(x, y, octaves);
        const float qy = fbm2(x + 5.2f, y + 1.3f, octaves);
        return {x + strength * qx, y + strength * qy};
    }
    /// @brief Domain-warped 3D coordinates.
    fuhgl::math::Vec3 warp3(float x, float y, float z, float strength, int octaves = 2) const {
        const float qx = fbm3(x, y, z, octaves);
        const float qy = fbm3(x + 5.2f, y + 1.3f, z + 2.8f, octaves);
        const float qz = fbm3(x + 9.7f, y + 4.1f, z + 7.9f, octaves);
        return {x + strength * qx, y + strength * qy, z + strength * qz};
    }

    /// @brief Convenience terrain height field in [0, 1]: ridged fBm mapped from [-1,1]-ish
    /// base noise (value/simplex are shifted to [0,1] inside base2, ridged output already [0,1]).
    float height2(float x, float y, int octaves = 5, Base base = Base::Perlin) const {
        return ridged2(x, y, octaves, base);
    }

private:
    static float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    std::uint32_t hash2i(int x, int y) const {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^ seed_;
        h ^= h >> 13; h *= 0x9E3779B1u; h ^= h >> 16;
        return h;
    }
    std::uint32_t hash3i(int x, int y, int z) const {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^
                          static_cast<std::uint32_t>(z) * 0xCB1AB31Fu ^ seed_;
        h ^= h >> 13; h *= 0x9E3779B1u; h ^= h >> 16;
        return h;
    }
    /// @brief Hashed lattice value in [0, 1] (value noise).
    float lattice2(int x, int y) const { return hash2i(x, y) * (1.f / 4294967296.f); }
    float lattice3(int x, int y, int z) const { return hash3i(x, y, z) * (1.f / 4294967296.f); }

    static float grad2(int h, float x, float y) {
        switch (h & 7) {
            case 0: return x + y;  case 1: return -x + y;
            case 2: return x - y;  case 3: return -x - y;
            case 4: return x;      case 5: return -x;
            case 6: return y;      default: return -y;
        }
    }
    static float grad3(int h, float x, float y, float z) {
        const int hh = h & 15;
        const float u = hh < 8 ? x : y;
        const float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : z);
        return ((hh & 1) ? -u : u) + ((hh & 2) ? -v : v);
    }

    std::uint32_t seed_;
    fuhgl::math::Noise perlin_;
    std::uint8_t perm_[512];
};

} // namespace fuhglgraphics::procedural
