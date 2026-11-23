#include "fuhglgraphics/render/ssao.hpp"

#include <fuhgl/math/func.hpp>

namespace fuhglgraphics::render {

std::vector<fuhgl::math::Vec3> ssaoKernel(int samples, unsigned seed) {
    std::vector<fuhgl::math::Vec3> kernel;
    if (samples <= 0) return kernel;
    kernel.reserve(static_cast<std::size_t>(samples));
    // small deterministic LCG (same seed -> same sequence)
    unsigned s = seed ? seed : 0x9e3779b9u;
    auto rnd = [&s]() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.f; // [0, 1)
    };
    const float n = static_cast<float>(samples);
    for (int i = 0; i < samples; ++i) {
        fuhgl::math::Vec3 v{rnd() * 2.f - 1.f, rnd() * 2.f - 1.f, rnd()}; // hemisphere: z >= 0
        v = fuhgl::math::normalize(v);
        const float t = static_cast<float>(i) / n;
        const float scale = 0.1f + 0.9f * t * t; // more samples near the origin
        kernel.push_back(v * scale);
    }
    return kernel;
}

float ssaoRotation(unsigned px, unsigned py, unsigned seed) {
    const float h = std::sin(static_cast<float>(px) * 12.9898f + static_cast<float>(py) * 78.233f +
                             static_cast<float>(seed) * 37.719f) *
                    43758.5453f;
    const float f = h - std::floor(h); // fract in [0, 1)
    return f * 2.f * fuhgl::math::kPi;
}

} // namespace fuhglgraphics::render
