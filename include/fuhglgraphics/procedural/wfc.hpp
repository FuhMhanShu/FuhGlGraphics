#pragma once

/// @file wfc.hpp
/// @brief G27: Wave Function Collapse (tiled, 2D grid) — give a set of tiles with edge tags
/// ("sockets") + weights, and the solver collapses every grid cell to one tile+rotation so
/// that all adjacent edges match. Min-entropy cell selection, AC-3-style constraint
/// propagation, seeded backtracking on contradiction. Output placements are typically
/// instantiated as G9 scene-graph nodes (one node / instanced batch per tile).
/// Deterministic per seed; pure CPU, no GL.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fuhglgraphics::procedural {

/// @brief Directions on the xz grid in cyclic (rotation) order: 0 = +x (east), 1 = +z (south),
/// 2 = -x (west), 3 = -z (north). With this order a tile rotated by rot*90 deg around +y maps
/// rotatedTags(rot)[d] = tags[(d + rot) mod 4].
constexpr int kWfcDirs = 4;
constexpr int kWfcOpposite[4] = {2, 3, 0, 1};

/// @brief One WFC tile: a name, a weight and 4 edge tags in cyclic dir order (E, S, W, N)
/// for rotation 0. Two adjacent cells are compatible when the touching tags are equal.
/// Rotating a tile by rot*90 deg around +y maps rotatedTags(rot)[d] = tags[(d + rot) mod 4].
struct WfcTile {
    std::string name;
    float weight = 1.f;
    std::array<std::string, 4> tags{"", "", "", ""}; ///< E, S, W, N (rotation 0)
};

/// @brief Edge tags of `t` after rotating by `rot` * 90° around +y.
std::array<std::string, 4> wfcRotatedTags(const WfcTile& t, int rot);

/// @brief One collapsed cell: tile index + rotation (0..3). tile < 0 = not collapsed.
struct WfcCell {
    int tile = -1;
    int rot = 0;
};

/// @brief Tiled wave-function-collapse solver over a width x height grid.
/// The grid border treats the outside as `borderTag` (default "w"), so e.g. dungeon walls
/// ring the map automatically.
class Wfc {
public:
    Wfc(int width, int height, std::string borderTag = "w");

    void addTile(const WfcTile& tile);
    int tileCount() const { return static_cast<int>(tiles_.size()); }
    const WfcTile& tile(int i) const { return tiles_[i]; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief `dir` (0..3) leads from cell A to its neighbor B: A's rotated tag on `dir`
    /// must equal B's rotated tag on the opposite direction.
    bool compatible(int tileA, int rotA, int dir, int tileB, int rotB) const;

    /// @brief Solves the grid. Deterministic for a given seed. On contradiction the solver
    /// backtracks (up to `maxBacktracks`); cells that stay unsolved get tile = -1.
    /// Returns row-major placements (index = x + width * z).
    std::vector<WfcCell> solve(unsigned seed, int maxBacktracks = 20000);

    /// @brief Dungeon-style tile set ("o" = open, "w" = wall, "d" = door): floor, wall bands
    /// (H/V), doors, pillar. Border tag "w" rings the map with walls.
    static std::vector<WfcTile> makeDungeonTiles();

private:
    struct Candidate {
        int tile, rot;
    };
    int width_, height_;
    std::string borderTag_;
    std::vector<WfcTile> tiles_;
};

} // namespace fuhglgraphics::procedural
