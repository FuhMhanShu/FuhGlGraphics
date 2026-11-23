// G27 wave function collapse: min-entropy selection + AC-3-style propagation + seeded
// backtracking. Deterministic per seed (splitmix32). Candidates are (tile, rot) pairs;
// compatibility = equal edge tags on the shared border (outside the grid = borderTag).

#include <fuhglgraphics/procedural/wfc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fuhglgraphics::procedural {

namespace {

struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    std::uint32_t next() {
        s += 0x9E3779B9u;
        std::uint32_t z = s;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }
    float uniform() { return next() * (1.f / 4294967296.f); }        // [0, 1)
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

} // namespace

std::array<std::string, 4> wfcRotatedTags(const WfcTile& t, int rot) {
    std::array<std::string, 4> out;
    const int r = ((rot % 4) + 4) % 4;
    for (int d = 0; d < 4; ++d) out[d] = t.tags[(d + r) % 4];
    return out;
}

Wfc::Wfc(int width, int height, std::string borderTag)
    : width_(width > 0 ? width : 1), height_(height > 0 ? height : 1), borderTag_(std::move(borderTag)) {}

void Wfc::addTile(const WfcTile& tile) { tiles_.push_back(tile); }

bool Wfc::compatible(int tileA, int rotA, int dir, int tileB, int rotB) const {
    return wfcRotatedTags(tiles_[tileA], rotA)[dir] == wfcRotatedTags(tiles_[tileB], rotB)[kWfcOpposite[dir]];
}

namespace {

constexpr int kDx[4] = {1, 0, -1, 0};
constexpr int kDz[4] = {0, 1, 0, -1};

} // namespace

std::vector<WfcCell> Wfc::solve(unsigned seed, int maxBacktracks) {
    const int n = width_ * height_;
    std::vector<std::vector<Candidate>> wave(n);
    std::vector<Candidate> all;
    for (int t = 0; t < static_cast<int>(tiles_.size()); ++t)
        for (int r = 0; r < 4; ++r) all.push_back({t, r});
    for (auto& c : wave) c = all;

    // border constraint: the outside of the grid is `borderTag_` on every outward edge
    for (int z = 0; z < height_; ++z)
        for (int x = 0; x < width_; ++x)
            for (int d = 0; d < 4; ++d) {
                const int nx = x + kDx[d], nz = z + kDz[d];
                if (nx < 0 || nx >= width_ || nz < 0 || nz >= height_) {
                    auto& cands = wave[x + width_ * z];
                    cands.erase(std::remove_if(cands.begin(), cands.end(),
                                               [&](const Candidate& c) {
                                                   return wfcRotatedTags(tiles_[c.tile], c.rot)[d] != borderTag_;
                                               }),
                                cands.end());
                }
            }

    Rng rng(seed);
    std::vector<WfcCell> result(n, WfcCell{-1, 0});
    std::vector<std::vector<Candidate>> snapshot;
    int backtracks = 0;

    // propagate from `start` (AC-3): returns false on contradiction (empty cell)
    std::vector<int> queue;
    auto propagate = [&](int start) -> bool {
        queue.clear();
        queue.push_back(start);
        while (!queue.empty()) {
            const int ci = queue.back();
            queue.pop_back();
            const int cx = ci % width_, cz = ci / width_;
            for (int d = 0; d < 4; ++d) {
                const int nx = cx + kDx[d], nz = cz + kDz[d];
                if (nx < 0 || nx >= width_ || nz < 0 || nz >= height_) continue;
                const int ni = nx + width_ * nz;
                // allowed candidates at ni: those compatible with >=1 candidate at ci
                std::vector<Candidate> allowed;
                for (const Candidate& cb : wave[ni]) {
                    for (const Candidate& ca : wave[ci]) {
                        if (compatible(ca.tile, ca.rot, d, cb.tile, cb.rot)) {
                            allowed.push_back(cb);
                            break;
                        }
                    }
                }
                if (allowed.size() != wave[ni].size()) {
                    if (allowed.empty()) return false;
                    wave[ni] = std::move(allowed);
                    queue.push_back(ni);
                }
            }
        }
        return true;
    };

    while (true) {
        // find the uncollapsed cell with the fewest candidates (min entropy, random tie-break)
        int best = -1;
        std::vector<int> bestCells;
        for (int i = 0; i < n; ++i) {
            if (wave[i].size() <= 1) continue;
            if (best < 0 || wave[i].size() < wave[best].size()) {
                best = i;
                bestCells.clear();
                bestCells.push_back(i);
            } else if (wave[i].size() == wave[best].size()) {
                bestCells.push_back(i);
            }
        }
        if (best < 0) break; // all collapsed
        const int cell = bestCells[rng.below(static_cast<int>(bestCells.size()))];

        // weighted-random collapse
        float totalW = 0.f;
        for (const Candidate& c : wave[cell]) totalW += tiles_[c.tile].weight;
        float pick = rng.uniform() * totalW;
        Candidate chosen = wave[cell].back();
        for (const Candidate& c : wave[cell]) {
            pick -= tiles_[c.tile].weight;
            if (pick <= 0.f) {
                chosen = c;
                break;
            }
        }

        snapshot = wave; // for backtracking
        wave[cell] = {chosen};
        if (!propagate(cell)) {
            // contradiction: backtrack — restore and ban the chosen candidate at `cell`
            ++backtracks;
            if (backtracks > maxBacktracks) {
                wave = snapshot;
                // leave remaining cells unsolved
                for (int i = 0; i < n; ++i)
                    if (wave[i].size() == 1) {
                        result[i] = {wave[i][0].tile, wave[i][0].rot};
                    }
                return result;
            }
            wave = snapshot;
            auto& cands = wave[cell];
            cands.erase(std::remove_if(cands.begin(), cands.end(),
                                       [&](const Candidate& c) {
                                           return c.tile == chosen.tile && c.rot == chosen.rot;
                                       }),
                        cands.end());
            if (cands.empty() || !propagate(cell)) {
                // deeper contradiction — give up on this branch after restoring
                wave = snapshot;
                for (int i = 0; i < n; ++i)
                    if (wave[i].size() == 1) result[i] = {wave[i][0].tile, wave[i][0].rot};
                return result;
            }
            continue;
        }
    }

    for (int i = 0; i < n; ++i)
        if (wave[i].size() == 1) result[i] = {wave[i][0].tile, wave[i][0].rot};
    return result;
}

std::vector<WfcTile> Wfc::makeDungeonTiles() {
    // tags in cyclic dir order E, S, W, N. "o" = open, "w" = wall.
    return {
        {"floor", 8.f, {"o", "o", "o", "o"}},
        {"wallH", 2.f, {"w", "o", "w", "o"}},  // wall band running E-W (E+W edges are wall)
        {"wallV", 2.f, {"o", "w", "o", "w"}},  // wall band running N-S (S+N edges are wall)
        {"wallCap", 2.f, {"w", "o", "o", "o"}},// wall on one edge only (rotations cover all sides)
        {"corner", 1.f, {"w", "w", "o", "o"}}, // wall on two adjacent edges (E+S)
        {"doorH", 1.f, {"w", "o", "w", "o"}},  // door inside an E-W wall band (same socket)
        {"doorV", 1.f, {"o", "w", "o", "w"}},  // door inside an N-S wall band (same socket)
        {"pillar", 0.5f, {"w", "w", "w", "w"}},
    };
}

} // namespace fuhglgraphics::procedural
