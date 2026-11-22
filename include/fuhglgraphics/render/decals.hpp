#pragma once

/// @file decals.hpp
/// @brief G8 deferred decals: plain-data decal boxes that the G3 DeferredRenderer projects
/// into the G-buffer (albedo + normal attachments) between the geometry pass and the
/// lighting pass, gated by the G-buffer material ID (the MaterialId attachment from G3).
/// Limited count (kMaxDecals, farthest dropped with a warning), sorted back-to-front.
/// Toggle: render::Effect::Decals. Pure header — no GL.

#include <fuhgl/gl/texture.hpp>
#include <fuhgl/math.hpp>

#include <cmath>
#include <cstdint>

namespace fuhglgraphics::render {

/// @brief Maximum number of decals per frame (the rest are dropped with a one-time warning).
inline constexpr int kMaxDecals = 16;

/// @brief One projected decal: an oriented box (`world` * unit box scaled by
/// `halfExtents`). The renderer reconstructs the G-buffer world position per pixel and
/// keeps pixels inside the box; the decal albedo (+ optional normal map) is projected onto
/// the dominant face axis of the surface normal and written into gAlbedo/gNormal.
struct Decal {
    fuhgl::math::Mat4 world{};                        ///< box transform (node world matrix)
    fuhgl::math::Vec3 halfExtents{1.f, 1.f, 1.f};     ///< box half size in world units
    fuhgl::math::Vec3 albedo{1.f, 1.f, 1.f};          ///< multiplied with the albedo texture
    const fuhgl::Texture* albedoTexture = nullptr;    ///< projected texture (decal pass unit 3)
    const fuhgl::Texture* normalMap = nullptr;        ///< optional projected normal map (unit 4)
    float opacity = 1.f;                              ///< blend factor into albedo/normal
    std::uint32_t materialIdMask = 0xffffffffu;       ///< applies where (materialId & mask) != 0
};

/// @brief Pure projection helper (mirrors the decal shader): transforms `worldPos` into
/// decal-local space, rejects points outside the box, and computes the projection UV from
/// the dominant axis of the surface normal (in decal space). Returns false when the point
/// is outside the box (or the UV outside [0,1]). Unit-testable, no GL.
inline bool decalProject(const fuhgl::math::Mat4& invDecalWorld, const fuhgl::math::Vec3& halfExtents,
                         const fuhgl::math::Vec3& worldPos, const fuhgl::math::Vec3& worldNormal,
                         fuhgl::math::Vec2& uv) {
    const fuhgl::math::Vec4 lp4 = invDecalWorld * fuhgl::math::Vec4{worldPos.x, worldPos.y, worldPos.z, 1.f};
    const fuhgl::math::Vec3 lp{lp4.x, lp4.y, lp4.z};
    if (std::fabs(lp.x) > halfExtents.x || std::fabs(lp.y) > halfExtents.y || std::fabs(lp.z) > halfExtents.z)
        return false;
    const fuhgl::math::Vec4 ln4 = invDecalWorld * fuhgl::math::Vec4{worldNormal.x, worldNormal.y, worldNormal.z, 0.f};
    const fuhgl::math::Vec3 ln = fuhgl::math::normalize(fuhgl::math::Vec3{ln4.x, ln4.y, ln4.z});
    const float ax = std::fabs(ln.x), ay = std::fabs(ln.y), az = std::fabs(ln.z);
    fuhgl::math::Vec2 duv;
    fuhgl::math::Vec2 he;
    if (ay >= ax && ay >= az) {
        duv = {lp.x, lp.z};
        he = {halfExtents.x, halfExtents.z};
    } else if (ax >= az) {
        duv = {lp.z, lp.y};
        he = {halfExtents.z, halfExtents.y};
    } else {
        duv = {lp.x, lp.y};
        he = {halfExtents.x, halfExtents.y};
    }
    uv = {duv.x / (2.f * he.x) + 0.5f, duv.y / (2.f * he.y) + 0.5f};
    return uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f;
}

} // namespace fuhglgraphics::render
