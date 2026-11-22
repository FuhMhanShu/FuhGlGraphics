#pragma once

/// @file light_probes.hpp
/// @brief G7 light probes: an SH (L2) irradiance probe grid with trilinear interpolation
/// in the G3 lighting pass (GL 3.3-capable: seven RGBA16F 3D textures, units 17..23),
/// baked on the CPU (one bounce: sky radiance + direct lights with shadow rays against
/// a world-space triangle soup). Provides diffuse indirect light (color bleeding);
/// toggle with render::Effect::LightProbes and scale with
/// scene::EffectParams::lightProbeStrength / DeferredRenderer::lightProbeStrength.
///
/// Conventions like FuhGLCore: PIMPL for the GL textures, no exceptions (bool + log),
/// namespace fuhglgraphics::probes.

#include <fuhgl/gl/texture.hpp>
#include <fuhgl/math.hpp>
#include <fuhgl/render/light.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace fuhglgraphics::probes {

/// @brief SH L2 coefficients: 9 RGB vectors in the order
/// Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22.
using Sh9 = std::array<fuhgl::math::Vec3, 9>;

/// @brief SH L2 basis functions Y_lm(d) written into y[0..8] (same order as Sh9). Pure.
void shBasis(const fuhgl::math::Vec3& d, float y[9]);

/// @brief Projects directional radiance samples (direction -> radiance) onto SH L2:
/// c_lm = (4*pi/N) * sum_i L_i * Y_lm(d_i). Pure — unit-testable.
void shProject(const std::vector<std::pair<fuhgl::math::Vec3, fuhgl::math::Vec3>>& samples, Sh9& out);

/// @brief Irradiance E(n) = sum_l A_l * sum_m c_lm Y_lm(n) with the Ramamoorthi A_l
/// (pi, 2pi/3 x3, pi/4 x5). For constant radiance L this yields E = pi * L everywhere.
/// The diffuse outgoing radiance is albedo/pi * E. Pure.
fuhgl::math::Vec3 shIrradiance(const Sh9& c, const fuhgl::math::Vec3& n);

/// @brief One world-space triangle for the CPU bake (soup).
struct ProbeTriangle {
    fuhgl::math::Vec3 a, b, c;
    fuhgl::math::Vec3 albedo{1.f, 1.f, 1.f};
    fuhgl::math::Vec3 emissive{0.f, 0.f, 0.f};
};

/// @brief Ray vs triangle (Moeller-Trumbore, two-sided). On hit returns true with the
/// distance `t` and the geometric normal `n` (not flipped towards the ray). Pure.
bool rayTriangle(const fuhgl::math::Vec3& ro, const fuhgl::math::Vec3& rd, const ProbeTriangle& tri,
                 float& t, fuhgl::math::Vec3& n);

/// @brief Input of LightProbeGrid::bake().
struct BakeInput {
    std::vector<ProbeTriangle> triangles; ///< world-space scene soup (for visibility rays)
    std::vector<fuhgl::render::Light> lights; ///< direct light sources (with shadow rays)
    /// @brief Sky/environment radiance per direction: miss rays return it directly, and
    /// bounce rays sample it at the hit point (visibility-tested).
    std::function<fuhgl::math::Vec3(const fuhgl::math::Vec3&)> skyRadiance;
    int raySamples = 96; ///< fibonacci-sphere directions per probe
};

/// @brief Regular grid of SH L2 irradiance probes over an AABB. CPU bake (bake()) fills
/// the coefficients; ensureUploaded() packs them into seven RGBA16F 3D textures
/// (27 floats per probe packed into 7 RGBA texels, hardware-trilinear across the grid)
/// that the G3 lighting pass samples on units kFirstTextureUnit .. +6.
class LightProbeGrid {
public:
    LightProbeGrid();
    ~LightProbeGrid();

    LightProbeGrid(const LightProbeGrid&) = delete;
    LightProbeGrid& operator=(const LightProbeGrid&) = delete;
    LightProbeGrid(LightProbeGrid&&) noexcept;
    LightProbeGrid& operator=(LightProbeGrid&&) noexcept;

    static constexpr int kCoeffCount = 9;      ///< SH L2 coefficients per probe
    static constexpr int kTextureCount = 7;    ///< 27 floats packed into 7 RGBA16F texels
    static constexpr int kFirstTextureUnit = 17; ///< 3D textures on units 17..23 (sky cube = 16)

    /// @brief Allocates the CPU grid (dims >= 2 per axis, max > min). Probes sit on the
    /// grid nodes: position = min + (i,j,k) * cell, cell = (max - min) / (dims - 1).
    bool create(int nx, int ny, int nz, const fuhgl::math::Vec3& min, const fuhgl::math::Vec3& max);

    int nx() const { return nx_; }
    int ny() const { return ny_; }
    int nz() const { return nz_; }
    fuhgl::math::Vec3 min() const { return min_; }
    fuhgl::math::Vec3 max() const { return max_; }
    /// @brief World-space distance between neighbouring probes.
    fuhgl::math::Vec3 cell() const;
    std::size_t probeCount() const { return coeffs_.size(); }
    bool valid() const { return !coeffs_.empty(); }

    /// @brief SH coefficients of probe (x,y,z) — mutable so tests/apps can set them
    /// directly (call invalidate() afterwards).
    Sh9& coeffs(int x, int y, int z) { return coeffs_[index(x, y, z)]; }
    const Sh9& coeffs(int x, int y, int z) const { return coeffs_[index(x, y, z)]; }
    /// @brief World position of probe (x,y,z).
    fuhgl::math::Vec3 probePos(int x, int y, int z) const;

    /// @brief CPU bake: per probe, fibonacci-sphere rays against the soup; hits gather
    /// one bounce (albedo/pi * incoming * cos) from sky + direct lights (shadow-ray
    /// occluded), misses take skyRadiance; the samples are projected onto SH L2.
    bool bake(const BakeInput& in);

    /// @brief (Re)uploads the 7 3D textures when dirty or the dims changed. Needs a
    /// current GL context. Const so DeferredRenderer::render() can call it on the
    /// const grid pointer from FrameData.
    bool ensureUploaded() const;
    bool uploaded() const { return uploaded_; }
    /// @brief Marks the GL textures dirty (call after changing coeffs()).
    void invalidate() { dirty_ = true; }
    /// @brief GL texture i in [0, kTextureCount) — valid after ensureUploaded().
    const fuhgl::Texture& texture(int i) const;

private:
    std::size_t index(int x, int y, int z) const {
        return (static_cast<std::size_t>(z) * ny_ + y) * nx_ + x;
    }

    struct Impl; ///< GL textures (PIMPL)
    mutable std::unique_ptr<Impl> impl_;

    int nx_ = 0, ny_ = 0, nz_ = 0;
    fuhgl::math::Vec3 min_{}, max_{};
    std::vector<Sh9> coeffs_;
    mutable bool dirty_ = true;
    mutable bool uploaded_ = false;
};

} // namespace fuhglgraphics::probes
