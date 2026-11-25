#pragma once

/// @file plant.hpp
/// @brief PlantGenerator: a class-based procedural generator for trees and bushes with real
/// variety — deciduous trees (Laubbaum), firs/spruces (Tanne, conical crown), birches (Birke,
/// white trunk + drooping light crown) and bushes (Busch). Built on the G26 L-systems
/// (procedural/lsystem.hpp) with per-kind rules, colors and crown shaping (conical squash for
/// firs, droop for birches). Pure CPU, deterministic per seed, no GL.

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>

namespace fuhglgraphics::procedural {

/// @brief Plant species the generator can build.
enum class PlantKind { Deciduous = 0, Fir = 1, Birch = 2, Bush = 3 };

/// @brief Parameters of one plant.
struct PlantParams {
    PlantKind kind = PlantKind::Deciduous;
    unsigned seed = 1;   ///< variant seed (rule jitter, crown shape)
    float scale = 1.f;   ///< overall size multiplier (world units)
};

/// @brief The generated plant: one MeshData (trunk+branches in wood color, crown/leaf quads in
/// leaf color — vertex colors distinguish them) plus the palette used.
struct PlantMesh {
    fuhgl::render::MeshData mesh;              ///< 11 floats/vertex, vertex-colored
    fuhgl::math::Vec3 trunkColor{0.32f, 0.22f, 0.14f};
    fuhgl::math::Vec3 leafColor{0.16f, 0.42f, 0.18f};
    PlantKind kind = PlantKind::Deciduous;
    float height = 0.f;                        ///< measured height of the generated mesh (world units)
};

/// @brief Class-based variadic plant generator. Stateless apart from the params passed in —
/// deterministic per seed.
class PlantGenerator {
public:
    /// @brief Generates one plant. Local space, base at y = 0, grows along +Y.
    PlantMesh generate(const PlantParams& p) const;

    /// @brief Random-but-deterministic params for a kind.
    static PlantParams randomParams(PlantKind kind, unsigned seed);
};

} // namespace fuhglgraphics::procedural
