#pragma once

/// @file gbuffer.hpp
/// @brief Extended G-buffer layout of the G3 deferred renderer (docs/REQUIREMENTS.md G3):
/// color attachment indices, texture formats and material-ID packing. Shared by the
/// renderer, by later features that read the G-buffer (G8 decals use position+normal+
/// material ID) and by the unit tests.

#include <cstdint>

namespace fuhglgraphics::render {

/// @brief Color attachment indices of the extended G-buffer (MRT draw order).
enum class GBufferAttachment : int {
    Albedo = 0,     ///< RGBA8:    rgb = albedo (linear), a = 1
    Normal = 1,     ///< RGBA8:    rgb = world-space normal * 0.5 + 0.5
    Pbr = 2,        ///< RGBA8:    r = metallic, g = roughness, b = ambient occlusion
    Emissive = 3,   ///< RGBA16F:  HDR emissive (bloom-able)
    Motion = 4,     ///< RG16F:    motion vector in UV units (prevUV - curUV), for TAA
    MaterialId = 5, ///< RGBA8:    packed 32-bit material ID (decals, G8)
    kColorCount = 6 ///< number of color attachments (+ one depth attachment)
};

/// @brief Packs a 32-bit material ID into RGBA8 bytes (channel i = byte i, little-endian).
inline void packMaterialId(std::uint32_t id, unsigned char rgba[4]) {
    rgba[0] = static_cast<unsigned char>(id & 0xFFu);
    rgba[1] = static_cast<unsigned char>((id >> 8) & 0xFFu);
    rgba[2] = static_cast<unsigned char>((id >> 16) & 0xFFu);
    rgba[3] = static_cast<unsigned char>((id >> 24) & 0xFFu);
}

/// @brief Inverse of packMaterialId().
inline std::uint32_t unpackMaterialId(const unsigned char rgba[4]) {
    return static_cast<std::uint32_t>(rgba[0]) | (static_cast<std::uint32_t>(rgba[1]) << 8) |
           (static_cast<std::uint32_t>(rgba[2]) << 16) | (static_cast<std::uint32_t>(rgba[3]) << 24);
}

/// @brief Clamps a render scale into the supported range [0.25, 1].
inline float clampedRenderScale(float scale) {
    return scale < 0.25f ? 0.25f : (scale > 1.f ? 1.f : scale);
}

/// @brief Internal (pre-upscale) render resolution for an output size and render scale.
/// Always at least 1x1. Pure — unit-testable.
inline void internalSize(int width, int height, float renderScale, int& outW, int& outH) {
    const float s = clampedRenderScale(renderScale);
    outW = width > 0 ? static_cast<int>(static_cast<float>(width) * s + 0.5f) : 0;
    outH = height > 0 ? static_cast<int>(static_cast<float>(height) * s + 0.5f) : 0;
    if (outW < 1 && width > 0) outW = 1;
    if (outH < 1 && height > 0) outH = 1;
}

} // namespace fuhglgraphics::render
