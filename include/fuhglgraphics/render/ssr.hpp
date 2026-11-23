#pragma once

/// @file ssr.hpp
/// @brief G16 SSR — pure (GL-free) helpers of the screen-space reflections pass:
/// the screen-edge fade and the roughness/fresnel reflection mask. Unit-testable.

#include <fuhgl/math/vec.hpp>

#include <cmath>

namespace fuhglgraphics::render {

/// @brief Fade factor in [0, 1] that goes to 0 within `margin` of the screen border
/// (uv in [0,1]); reflections marching off-screen would pop otherwise.
inline float ssrEdgeFade(fuhgl::math::Vec2 uv, float margin = 0.08f) {
    const float m = margin < 0.f ? 0.f : (margin > 0.5f ? 0.5f : margin);
    const float fx = (uv.x < m) ? uv.x / m : ((1.f - uv.x) < m ? (1.f - uv.x) / m : 1.f);
    const float fy = (uv.y < m) ? uv.y / m : ((1.f - uv.y) < m ? (1.f - uv.y) / m : 1.f);
    const float f = fx < fy ? fx : fy;
    return f * f * (3.f - 2.f * f); // smoothstep
}

/// @brief Reflection mask: rough surfaces reflect little, metals reflect at all angles.
/// `roughness` in [0,1], `ndotv` = dot(N, V), `metallic` in [0,1]. In [0, 1].
inline float ssrMask(float roughness, float ndotv, float metallic) {
    const float r = roughness < 0.f ? 0.f : (roughness > 1.f ? 1.f : roughness);
    const float nv = ndotv < 0.f ? 0.f : (ndotv > 1.f ? 1.f : ndotv);
    // schlick fresnel with F0 = mix(0.04, 1, metallic)
    const float f0 = 0.04f + 0.96f * (metallic < 0.f ? 0.f : (metallic > 1.f ? 1.f : metallic));
    const float f = f0 + (1.f - f0) * std::pow(1.f - nv, 5.f);
    const float roughFade = (1.f - r) * (1.f - r);
    return f * roughFade;
}

} // namespace fuhglgraphics::render
