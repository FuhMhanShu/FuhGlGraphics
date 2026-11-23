#pragma once

/// @file parallax.hpp
/// @brief Parallax occlusion mapping (POM) — pure (GL-free) CPU twin of the POM block in the
/// G3 G-buffer fragment shader (kGBufferFrag): marches the view ray (in tangent space)
/// through the height layers and returns the UV offset. Unit-testable; the material side
/// lives in FuhGLCore render::Material (heightMap / parallaxScale / parallaxLayers).

#include <fuhgl/math/vec.hpp>

#include <functional>

namespace fuhglgraphics::render {

/// @brief Linear POM search (mirrors the GLSL loop): `viewTS` = view direction in tangent
/// space, `heightAtUV(uv)` samples the height map in [0, 1] (1 = surface, 0 = deepest).
/// Returns the UV the albedo/normal should be sampled at (= `uv` + offset).
/// `scale` is the POM depth in uv units at full depth, `layers` the search steps (max 64).
inline fuhgl::math::Vec2 parallaxOffsetUV(fuhgl::math::Vec3 viewTS, float scale, int layers,
                                           const std::function<float(fuhgl::math::Vec2)>& heightAtUV,
                                           fuhgl::math::Vec2 uv) {
    const float z = viewTS.z > 0.05f ? viewTS.z : 0.05f; // clamp: grazing angles
    const fuhgl::math::Vec2 p{viewTS.x / z * scale, viewTS.y / z * scale};
    const float n = layers > 0 ? static_cast<float>(layers > 64 ? 64 : layers) : 1.f;
    const fuhgl::math::Vec2 duv{p.x / n, p.y / n};
    float layerDepth = 0.f;
    fuhgl::math::Vec2 curUV = uv;
    float h = heightAtUV ? heightAtUV(curUV) : 0.f;
    for (int i = 0; i < 64 && static_cast<float>(i) < n; ++i) {
        if (layerDepth >= h) break;
        curUV.x -= duv.x;
        curUV.y -= duv.y;
        layerDepth += 1.f / n;
        h = heightAtUV ? heightAtUV(curUV) : 0.f;
    }
    return curUV;
}

/// @brief POM UV offset (uv_out - uv_in) for convenience/tests.
inline fuhgl::math::Vec2 parallaxOffset(fuhgl::math::Vec3 viewTS, float scale, int layers,
                                        const std::function<float(fuhgl::math::Vec2)>& heightAtUV,
                                        fuhgl::math::Vec2 uv) {
    const fuhgl::math::Vec2 out = parallaxOffsetUV(viewTS, scale, layers, heightAtUV, uv);
    return {out.x - uv.x, out.y - uv.y};
}

} // namespace fuhglgraphics::render
