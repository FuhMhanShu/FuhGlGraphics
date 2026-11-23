// Unit tests for G27 wave function collapse (label "unit"): rotation tag mapping,
// compatibility, determinism, full solve validity (all cells collapsed, all adjacent edge
// tags match, border constraint), dungeon tile set produces walls at the border + open floor,
// backtracking terminates.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/wfc.hpp>

#include <set>

namespace {

using fuhglgraphics::procedural::Wfc;
using fuhglgraphics::procedural::WfcTile;
using fuhglgraphics::procedural::wfcRotatedTags;

void testRotation() {
    // tags in cyclic dir order E, S, W, N
    const WfcTile t{"t", 1.f, {"E", "S", "W", "N"}};
    CHECK(wfcRotatedTags(t, 0)[0] == "E");
    const auto r1 = wfcRotatedTags(t, 1); // +90° around +y: old E tag moves to N
    CHECK(r1[0] == "S" && r1[1] == "W" && r1[2] == "N" && r1[3] == "E");
    const auto r4 = wfcRotatedTags(t, 4);
    CHECK(r4 == t.tags);
    const auto rm1 = wfcRotatedTags(t, -1); // -90°: old E tag moves to S
    CHECK(rm1[0] == "N" && rm1[1] == "E" && rm1[2] == "S" && rm1[3] == "W");
}

void testCompatibility() {
    Wfc w(2, 1, "b");
    w.addTile({"a", 1.f, {"x", "y", "y", "y"}}); // tile 0: E=x, S=y, W=y, N=y
    w.addTile({"b", 1.f, {"x", "x", "x", "x"}}); // tile 1: all x
    // A(0,rot0) east edge "x" vs B(1,rot0) west edge "x" -> compatible
    CHECK(w.compatible(0, 0, 0, 1, 0));
    // A(0,rot0) west edge "y" vs B(1,rot0) east edge "x" -> incompatible
    CHECK(!w.compatible(0, 0, 2, 1, 0));
    // border: cell at x=0 has west neighbor = border tag "b"
    Wfc wb(1, 1, "x");
    wb.addTile({"t", 1.f, {"x", "x", "x", "x"}});
    // tile with west tag "x" is allowed at the border; one with "y" is not (checked via solve)
    const auto sol = wb.solve(1u);
    CHECK(sol.size() == 1);
    CHECK(sol[0].tile == 0);
}

void testSolveValidity() {
    Wfc w(10, 8, "w");
    for (const auto& t : Wfc::makeDungeonTiles()) w.addTile(t);
    const auto cells = w.solve(12345u);
    CHECK(cells.size() == 80u);
    // all collapsed
    for (const auto& c : cells) CHECK(c.tile >= 0 && c.tile < w.tileCount() && c.rot >= 0 && c.rot < 4);
    // all adjacent edges match (incl. border = "w")
    const auto tag = [&](int x, int z, int dir) -> std::string {
        if (x < 0 || x >= w.width() || z < 0 || z >= w.height()) return "w";
        const auto& c = cells[x + w.width() * z];
        return wfcRotatedTags(w.tile(c.tile), c.rot)[dir];
    };
    for (int z = 0; z < w.height(); ++z)
        for (int x = 0; x < w.width(); ++x) {
            CHECK(tag(x, z, 0) == tag(x + 1, z, 2)); // E vs W
            CHECK(tag(x, z, 1) == tag(x, z + 1, 3)); // S vs N
        }
    // determinism
    const auto cells2 = w.solve(12345u);
    CHECK(cells2.size() == cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i)
        CHECK(cells2[i].tile == cells[i].tile && cells2[i].rot == cells[i].rot);
    // different seed -> (very likely) different layout
    const auto cells3 = w.solve(999u);
    bool anyDiff = false;
    for (std::size_t i = 0; i < cells.size(); ++i)
        if (cells3[i].tile != cells[i].tile || cells3[i].rot != cells[i].rot) anyDiff = true;
    CHECK(anyDiff);
}

void testDungeonStructure() {
    Wfc w(12, 12, "w");
    for (const auto& t : Wfc::makeDungeonTiles()) w.addTile(t);
    const auto cells = w.solve(777u);
    // border cells must have "w" on their outward edge -> wall-ish tiles at the border
    int borderWalls = 0, floorCount = 0, wallCount = 0, doorCount = 0;
    for (int z = 0; z < w.height(); ++z)
        for (int x = 0; x < w.width(); ++x) {
            const auto& c = cells[x + w.width() * z];
            const std::string n = w.tile(c.tile).name;
            if (n == "floor") ++floorCount;
            if (n == "wallH" || n == "wallV" || n == "pillar") ++wallCount;
            if (n == "doorH" || n == "doorV") ++doorCount;
            if (x == 0 || z == 0 || x == w.width() - 1 || z == w.height() - 1) {
                const auto tags = wfcRotatedTags(w.tile(c.tile), c.rot);
                const bool outerWall = (x == 0 && tags[2] == "w") || (x == w.width() - 1 && tags[0] == "w") ||
                                       (z == 0 && tags[3] == "w") || (z == w.height() - 1 && tags[1] == "w");
                if (outerWall) ++borderWalls;
            }
        }
    CHECK(borderWalls == 4 * 12 - 4); // the whole ring is walled
    CHECK(floorCount > 20);          // plenty of open floor
    CHECK(wallCount > 0);
    std::fprintf(stderr, "wfc dungeon: floor=%d wall=%d door=%d\n", floorCount, wallCount, doorCount);
}

void testBacktrackTerminates() {
    // an impossible tile set: single tile that cannot touch the border tag -> contradiction
    Wfc w(3, 3, "zzz");
    w.addTile({"only", 1.f, {"q", "q", "q", "q"}});
    const auto cells = w.solve(42u, 100); // must terminate, cells unsolved (tile = -1)
    CHECK(cells.size() == 9u);
    bool anyUnsolved = false;
    for (const auto& c : cells)
        if (c.tile < 0) anyUnsolved = true;
    CHECK(anyUnsolved);
}

} // namespace

void runProceduralWfcTests() {
    testRotation();
    testCompatibility();
    testSolveValidity();
    testDungeonStructure();
    testBacktrackTerminates();
}
