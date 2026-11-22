#pragma once

/// @file water_surface.hpp
/// @brief G13 water: `water::WaterSurface` — a Gerstner-wave water plane rendered by the G3
/// deferred renderer (render::Effect::Water) with planar reflection, refraction, fresnel,
/// wave-crest foam ("Gicht") and depth-based shore foam.
///
/// The surface is a flat grid mesh in the local XZ plane (y = 0); the vertex shader of the
/// renderer's water pass displaces it by the Gerstner wave sum and derives the wave normal
/// analytically. The CPU twin `evalGerstner()` is pure and unit-testable. Attach it to a
/// scene::Node via `scene::WaterComponent`; the renderer picks it up from
/// `FrameData::water` / the `render(..., water)` parameter.
///
/// Conventions like the rest of the library: PIMPL-free value types where possible, no
/// exceptions for domain errors (bool + log), namespace fuhglgraphics::water.

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <vector>

namespace fuhglgraphics::water {

/// @brief One Gerstner wave. `direction` is normalized internally (local XZ plane).
struct GerstnerWave {
    fuhgl::math::Vec2 direction{1.f, 0.f}; ///< propagation direction in the local XZ plane
    float amplitude = 0.25f;               ///< peak height in world units
    float wavelength = 12.f;               ///< meters
    float speed = 1.6f;                    ///< phase speed in m/s (omega = k * speed)
    float steepness = 0.6f;                ///< 0..1 horizontal pinching of the crests
};

/// @brief Evaluates the Gerstner wave sum at local position `xz` and `time` (pure CPU twin
/// of the water vertex shader). `posOut` receives the displaced position (input plane y=0),
/// `normalOut` (when non-null) the analytic wave normal.
void evalGerstner(const GerstnerWave* waves, int count, float time,
                  const fuhgl::math::Vec2& xz, fuhgl::math::Vec3& posOut,
                  fuhgl::math::Vec3* normalOut);

/// @brief Reflection matrix about the plane dot(n, x) + d = 0 (`planeN` normalized).
/// Pure — unit-testable.
fuhgl::math::Mat4 reflectionMatrix(const fuhgl::math::Vec3& planeN, float planeD);

/// @brief World-space water plane (normal + d) of a surface whose grid mesh is drawn with
/// model matrix `model`: the local y=0 plane transformed by `model`. Pure.
void waterPlaneFromModel(const fuhgl::math::Mat4& model, fuhgl::math::Vec3& planeN, float& planeD);

/// @brief A water surface: grid mesh + Gerstner waves + shading parameters.
/// Usage: `create(resolution, size)` once with a current GL context, tune `waves`/colors,
/// call `update(dt)` per frame, attach to a scene node via `scene::WaterComponent` (or pass
/// to `DeferredRenderer::render(..., &surface)` together with its mesh as a draw item).
class WaterSurface {
public:
    /// @brief Maximum number of Gerstner waves (shader uniform array size).
    static constexpr int kMaxWaves = 8;

    WaterSurface() = default;
    ~WaterSurface() = default;
    WaterSurface(const WaterSurface&) = delete;
    WaterSurface& operator=(const WaterSurface&) = delete;
    WaterSurface(WaterSurface&&) noexcept = default;
    WaterSurface& operator=(WaterSurface&&) noexcept = default;

    /// @brief Builds the flat grid mesh (resolution x resolution vertices over
    /// size x size world units, centered at the local origin, y = 0). Needs a GL context.
    bool create(int resolution = 128, float size = 120.f);
    bool isReady() const { return ready_; }

    fuhgl::render::Mesh& mesh() { return mesh_; }
    const fuhgl::render::Mesh& mesh() const { return mesh_; }

    /// @brief Advances the wave animation time.
    void update(float dt) { time += dt; }

    /// @brief Sum of the wave amplitudes (max crest height above the plane) — used to
    /// normalize the crest-foam factor and to pad the culling AABB.
    float maxWaveHeight() const;

    /// @brief Fills `waves` with a default 4-wave sea state (call after construction or to reset).
    void setDefaultWaves();

    // --- waves / animation ---
    std::vector<GerstnerWave> waves; ///< edit, keep size <= kMaxWaves
    float time = 0.f;                ///< wave animation time in seconds

    // --- shading parameters (consumed by the renderer's water pass) ---
    fuhgl::math::Vec3 deepColor{0.004f, 0.05f, 0.11f};    ///< absorbed deep-water tint
    fuhgl::math::Vec3 shallowColor{0.06f, 0.30f, 0.38f};    ///< shallow-water tint near shore
    fuhgl::math::Vec3 foamColor{0.92f, 0.96f, 1.0f};         ///< crest/shore foam ("Gicht")
    float fresnelF0 = 0.02f;          ///< dielectric fresnel reflectance at normal incidence
    float reflectionStrength = 1.f;   ///< planar-reflection intensity (0 = off)
    float distortion = 0.12f;         ///< reflection UV offset per unit of normal xz
    float refractionDistortion = 0.06f; ///< refraction UV offset per unit of normal xz
    float foamCrestStart = 0.72f;     ///< crest foam starts at this fraction of maxWaveHeight
    float foamCrestSoftness = 0.18f;  ///< smooth width of the crest-foam ramp
    float shoreFoamWidth = 1.2f;      ///< water depth (world units) over which shore foam fades in
    float foamStrength = 1.f;         ///< overall foam intensity (0 = off)
    float glintPower = 512.f;         ///< sun-glint specular exponent
    float detailNormalScale = 2.2f;   ///< strength of the procedural ripple detail normal

private:
    fuhgl::render::Mesh mesh_;
    bool ready_ = false;
};

} // namespace fuhglgraphics::water
