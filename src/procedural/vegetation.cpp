// G37 ecophysiological vegetation generator — see include/fuhglgraphics/procedural/
// vegetation.hpp and docs/VEGETATION_GENERATION.md. Pipeline (all pure CPU, deterministic
// per seed): EcoEnvironment -> deriveGrowthForm() (climate-to-geometry rules) ->
// spaceColonization() (light attractors, kill distance, wind vector, self-pruning) ->
// applyPipeModel() (Da Vinci rule R^n = sum r_i^n) -> branch cylinders + leaf kites/needles/
// thorns with baked tints (leaf age + autumn ramp, bark cracks via stretched simplex, moss on
// the wet side) and per-vertex growth attributes in uv.x/uv.y for the shader side.

#include <fuhglgraphics/procedural/vegetation.hpp>

#include <fuhglgraphics/procedural/noise.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fuhglgraphics::procedural {

namespace {

using fuhgl::math::Vec3;

/// @brief Deterministic splitmix32 RNG (same stream as plant.cpp/worldgen.cpp).
struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() {
        state += 0x9E3779B9u;
        std::uint32_t z = state;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }
    float nextf() { return static_cast<float>(next() * (1.0 / 4294967296.0)); }
    float range(float a, float b) { return a + (b - a) * nextf(); }
};

float clamp01(float x) { return std::min(1.f, std::max(0.f, x)); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float smoothstep01(float e0, float e1, float x) {
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

/// @brief Rodrigues rotation of `v` around unit `axis` by `angle` radians.
Vec3 rotateAroundAxis(Vec3 v, const Vec3& axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + fuhgl::math::cross(axis, v) * s + axis * (fuhgl::math::dot(axis, v) * (1.f - c));
}

void pushVertex(fuhgl::render::MeshData& md, const Vec3& p, const Vec3& n, const Vec3& c, float u,
                float v) {
    md.vertices.push_back(p.x); md.vertices.push_back(p.y); md.vertices.push_back(p.z);
    md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(u); md.vertices.push_back(v);
}

/// @brief Tapered cylinder p0 (radius r0) -> p1 (radius r1). Per-vertex: uv.x = `u` (bark age),
/// uv.y = wet-side factor, color = `color` blended to `moss` by the moss mask on the wet side.
void addCylinder(fuhgl::render::MeshData& md, const Vec3& p0, const Vec3& p1, float r0, float r1,
                 int sides, const Vec3& color, const Vec3& moss, float mossCoverage, float u,
                 const Vec3& windDir) {
    const Vec3 d = p1 - p0;
    const float len = fuhgl::math::length(d);
    if (len < 1e-6f || r0 <= 1e-7f) return;
    const Vec3 axis = d / len;
    const Vec3 h = std::fabs(axis.y) < 0.9f ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
    const Vec3 uu = fuhgl::math::normalize(fuhgl::math::cross(axis, h));
    const Vec3 vv = fuhgl::math::cross(axis, uu);
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    for (int i = 0; i < sides; ++i) {
        const float a = 2.f * fuhgl::math::kPi * i / sides;
        const Vec3 n = uu * std::cos(a) + vv * std::sin(a);
        const float wet = wetSideFactor(n, windDir);
        const float mossMask = clamp01(mossCoverage * wet - 0.15f);
        const Vec3 c = fuhgl::math::lerp(color, moss, mossMask);
        pushVertex(md, p0 + n * r0, n, c, u, wet);
        pushVertex(md, p1 + n * r1, n, c, u, wet);
    }
    for (int i = 0; i < sides; ++i) {
        const std::uint32_t a = base + 2 * i, b = base + 2 * ((i + 1) % sides);
        md.indices.push_back(a); md.indices.push_back(a + 1); md.indices.push_back(b);
        md.indices.push_back(b); md.indices.push_back(a + 1); md.indices.push_back(b + 1);
    }
}

/// @brief One leaf-shaped kite quad: narrow base at `pos`, two side corners along `axisB`
/// (half-width `width`), pointed tip along `axisA` elongated by `tipElong` and drooping by
/// `tipDroop` (tropical drip tip). uv = (age, wetSide).
void addLeafKite(fuhgl::render::MeshData& md, const Vec3& pos, const Vec3& axisA,
                 const Vec3& axisB, float size, float width, float tipElong, float tipDroop,
                 const Vec3& color, float u, float wet) {
    const Vec3 v0 = pos - axisA * (0.25f * size);
    const Vec3 v1 = pos + axisB * (width * size) - axisA * (0.15f * size);
    const Vec3 v2 = pos + axisA * ((1.35f + tipElong) * size) - Vec3{0.f, tipDroop * size, 0.f};
    const Vec3 v3 = pos - axisB * (width * size) - axisA * (0.15f * size);
    const Vec3 n = fuhgl::math::normalize(fuhgl::math::cross(v2 - v0, v1 - v0));
    pushVertex(md, v0, n, color, u, wet);
    pushVertex(md, v1, n, color, u, wet);
    pushVertex(md, v2, n, color, u, wet);
    pushVertex(md, v3, n, color, u, wet);
    const std::uint32_t b0 = static_cast<std::uint32_t>(md.vertexCount()) - 4;
    md.indices.push_back(b0); md.indices.push_back(b0 + 1); md.indices.push_back(b0 + 2);
    md.indices.push_back(b0); md.indices.push_back(b0 + 2); md.indices.push_back(b0 + 3);
}

/// @brief One thorn: a small spike triangle along `dir`.
void addThorn(fuhgl::render::MeshData& md, const Vec3& pos, const Vec3& dir, const Vec3& perp,
              float size, const Vec3& color, float u, float wet) {
    const Vec3 b0 = pos - perp * (0.09f * size);
    const Vec3 b1 = pos + perp * (0.09f * size);
    const Vec3 tip = pos + dir * (0.55f * size);
    const Vec3 n = fuhgl::math::normalize(fuhgl::math::cross(tip - b0, b1 - b0));
    pushVertex(md, b0, n, color, u, wet);
    pushVertex(md, b1, n, color, u, wet);
    pushVertex(md, tip, n, color, u, wet);
    const std::uint32_t b = static_cast<std::uint32_t>(md.vertexCount()) - 3;
    md.indices.push_back(b); md.indices.push_back(b + 1); md.indices.push_back(b + 2);
}

} // namespace

// --- climate-to-geometry rules --------------------------------------------------------------

GrowthForm deriveGrowthForm(const EcoEnvironment& eco) {
    GrowthForm f;
    const float t = clamp01(eco.temperature);
    const float h = clamp01(eco.humidity);
    const float w = clamp01(eco.windExposure);
    const float d = clamp01(eco.standDensity);

    // base size: warm grows taller; wind reduces the max height (Windflüchter / Bodendecker)
    f.maxHeight = lerp(3.0f, 9.0f, t) * (1.f - 0.55f * w);
    f.crownRadius = lerp(1.7f, 3.1f, t);

    // temperature & snow: cold -> needles, conical crown, drooping branches (snow slides off);
    // warm frost-free -> broad leaves on wide horizontal branches
    if (t < 0.32f) {
        f.leaf = LeafMorphology::Needle;
        f.crownRadius *= 0.55f;      // kegelförmige Krone
        f.branchAngleDeg = 115.f;    // Äste hängen nach unten
        f.droop = 0.55f;
        f.leafSize = 0.34f;
    } else if (h > 0.72f) {
        // very wet (tropics): huge thin leaves with drip tips (rain runs off, no mold)
        f.leaf = LeafMorphology::DripTip;
        f.leafSize = 0.95f;
        f.leafThickness = 0.12f;
        f.dripTip = 1.f;
        f.branchAngleDeg = 72.f;
    } else if (h < 0.28f) {
        // dry (steppe/mediterranean): small thick waxy leaves, thorns, or succulent
        if (h < 0.12f && t > 0.45f) {
            f.leaf = LeafMorphology::None; // sukkulenter Stamm, keine Blätter
            f.succulent = true;
            f.succulence = 1.f;
        } else if (h < 0.18f) {
            f.leaf = LeafMorphology::Thorn;
            f.leafSize = 0.30f;
        } else {
            f.leaf = LeafMorphology::Broad;
            f.leafSize = 0.30f;
            f.leafThickness = 1.f; // dick, wachsig -> wenig Verdunstung
        }
        f.branchAngleDeg = 80.f;
    } else {
        f.leaf = LeafMorphology::Broad;
        f.leafSize = 0.55f;
        f.leafThickness = 0.5f;
        f.branchAngleDeg = 70.f;
    }
    // wetter -> bigger leaves (all morphologies)
    f.leafSize *= lerp(0.6f, 1.25f, h);

    // wind: constant wind vector added to the growth direction + reduced height; at high
    // exposure the tree becomes crooked/stocky or a flat ground cover
    const Vec3 windDir = fuhgl::math::normalize(eco.windDir);
    f.windLean = windDir * (w * 0.22f * f.maxHeight);
    if (w > 0.75f) {
        f.maxHeight *= 0.55f;
        f.crownRadius *= 1.6f;
        f.droop += 0.6f;
    }

    // stand density (light competition): crown starts high, lower branches self-prune
    f.selfPrune = d > 0.45f;
    f.crownBase = lerp(1.0f, f.maxHeight * 0.45f, d);
    f.pruneBelow = f.selfPrune ? d * f.maxHeight * 0.55f : 0.f;
    return f;
}

// --- space colonization ----------------------------------------------------------------------

ScaTree spaceColonization(const GrowthForm& form, const EcoEnvironment& eco, const ScaParams& p,
                          unsigned seed) {
    Rng rng(seed);
    ScaTree tree;
    const float dens = clamp01(eco.standDensity);

    // crown envelope: density shrinks the crown radius (narrow top crown in a dense forest)
    const float crownR = std::max(0.2f, form.crownRadius * (1.f - 0.75f * dens));
    const float crownBase = std::min(form.crownBase, form.maxHeight * 0.9f);
    const float crownTop = form.maxHeight;

    // light attractors: disc samples in the crown volume; with rising density the y exponent
    // drops below 1 -> attractors concentrate near the top (light only from above); the wind
    // lean shifts them so the crown grows into the wind
    std::vector<Vec3> attractors;
    attractors.reserve(static_cast<std::size_t>(p.attractors));
    for (int i = 0; i < p.attractors; ++i) {
        const float a = 2.f * fuhgl::math::kPi * rng.nextf();
        const float r = crownR * std::sqrt(rng.nextf());
        const float y01 = std::pow(rng.nextf(), 1.f - 0.8f * dens);
        const float y = crownBase + (crownTop - crownBase) * y01;
        attractors.push_back({r * std::cos(a) + form.windLean.x * y, y,
                              r * std::sin(a) + form.windLean.z * y});
    }

    // skeleton: root + initial trunk tip
    ScaNode root;
    tree.nodes.push_back(root);
    ScaNode tip;
    tip.pos = {form.windLean.x * p.step, p.step, form.windLean.z * p.step};
    tip.parent = 0;
    tip.depth = 1;
    tree.nodes.push_back(tip);
    std::vector<int> tips{1};
    std::vector<int> grace(tree.nodes.size(), 0);     // per-node: iterations without light capture
    std::vector<char> captured(tree.nodes.size(), 0); // per-node: captured an attractor this iteration

    auto addNode = [&](const Vec3& pos, int parent, int depth) -> int {
        ScaNode nd;
        nd.pos = pos;
        nd.parent = parent;
        nd.depth = depth;
        tree.nodes.push_back(nd);
        grace.push_back(0);
        captured.push_back(0);
        return static_cast<int>(tree.nodes.size()) - 1;
    };

    int iter = 0;
    while (iter < p.maxIterations && !attractors.empty() &&
           static_cast<int>(tree.nodes.size()) < p.maxNodes) {
        ++iter;
        std::vector<int> nextTips;
        std::fill(captured.begin(), captured.end(), 0);
        for (const int t : tips) {
            if (static_cast<int>(tree.nodes.size()) >= p.maxNodes) break; // hard node cap
            // copy: addNode() may reallocate tree.nodes, so no references into it across calls
            const ScaNode tn = tree.nodes[t];
            if (!tn.alive) continue;
            if (tn.pos.y > form.maxHeight * 1.05f) { tree.nodes[t].alive = false; continue; }
            // nearest light attractor
            int best = -1;
            float bestD = 1e30f;
            for (std::size_t i = 0; i < attractors.size(); ++i) {
                const float d = fuhgl::math::lengthSq(attractors[i] - tn.pos);
                if (d < bestD) { bestD = d; best = static_cast<int>(i); }
            }
            if (best < 0) continue;
            if (std::sqrt(bestD) < p.killDistance) {
                attractors.erase(attractors.begin() + best); // attractor consumed
                captured[t] = 1;
                nextTips.push_back(t);
                continue;
            }
            // grow toward the attractor; constant wind vector on the growth direction, slight
            // upward bias in dense stands (apical dominance), droop bends branches downward
            Vec3 dir = fuhgl::math::normalize(attractors[best] - tn.pos);
            const float relH = std::min(1.f, tn.pos.y / std::max(1e-3f, form.maxHeight));
            dir = fuhgl::math::normalize(dir + form.windLean * (0.45f * relH) +
                                         Vec3{0.f, dens * 0.30f, 0.f});
            if (form.droop > 0.f && tn.depth > 1)
                dir = fuhgl::math::normalize(dir + Vec3{0.f, -form.droop * 0.35f, 0.f});
            const Vec3 np = tn.pos + dir * p.step;
            const int ci = addNode(np, t, tn.depth + 1);
            nextTips.push_back(ci); // the branch continues from the new node
            // side branch with probability branchProbability
            if (rng.nextf() < p.branchProbability &&
                static_cast<int>(tree.nodes.size()) < p.maxNodes) {
                Vec3 axis = fuhgl::math::cross(dir, {rng.nextf() - 0.5f, rng.nextf() - 0.5f,
                                                      rng.nextf() - 0.5f});
                if (fuhgl::math::lengthSq(axis) < 1e-6f) axis = fuhgl::math::cross(dir, {1.f, 0.f, 0.f});
                axis = fuhgl::math::normalize(axis);
                const float ang = fuhgl::math::radians(form.branchAngleDeg) * rng.range(0.8f, 1.2f);
                const Vec3 sdir = fuhgl::math::normalize(rotateAroundAxis(dir, axis, ang));
                nextTips.push_back(addNode(np + sdir * p.step * 0.8f, ci, tn.depth + 2));
            }
        }
        // self-pruning (apical dominance): the highest living tip (the leader) always survives;
        // every other tip that captured no light for pruneGrace iterations dies off — in a
        // dense stand these are exactly the shaded lower side branches
        int leader = -1;
        float leaderY = -1.f;
        for (const int t : nextTips)
            if (tree.nodes[t].alive && tree.nodes[t].pos.y > leaderY) {
                leaderY = tree.nodes[t].pos.y;
                leader = t;
            }
        // shaded zone: below the crown base + margin there is (in a dense stand) no light
        const float shadeTop = crownBase + 0.25f * (crownTop - crownBase);
        tips.clear();
        for (const int t : nextTips) {
            if (t == leader || captured[t]) grace[t] = 0;
            else ++grace[t];
            if (t != leader && form.selfPrune && grace[t] > p.pruneGrace &&
                tree.nodes[t].pos.y < shadeTop) {
                tree.nodes[t].alive = false;
                continue;
            }
            tips.push_back(t);
        }
    }

    // dense stand: lower branches below the crown base die off (self-pruning). The trunk —
    // the chain from the highest node down to the root — is protected; parents are created
    // before children, so one pass propagates death to whole subtrees
    std::vector<char> trunk(tree.nodes.size(), 0);
    int top = 0;
    for (std::size_t i = 0; i < tree.nodes.size(); ++i)
        if (tree.nodes[i].pos.y > tree.nodes[top].pos.y) top = static_cast<int>(i);
    for (int c = top; c >= 0; c = tree.nodes[c].parent) trunk[c] = 1;
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        ScaNode& nd = tree.nodes[i];
        if (nd.parent >= 0 && !tree.nodes[nd.parent].alive) nd.alive = false;
        if (nd.alive && !trunk[i] && nd.pos.y < form.pruneBelow) nd.alive = false;
    }

    // age = normalized creation order (0 root .. 1 youngest tip) for the leaf tint
    const float inv = tree.nodes.size() > 1 ? 1.f / static_cast<float>(tree.nodes.size() - 1) : 0.f;
    float maxY = 0.f;
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        tree.nodes[i].age = static_cast<float>(i) * inv;
        if (tree.nodes[i].alive) maxY = std::max(maxY, tree.nodes[i].pos.y);
    }
    tree.height = maxY;
    tree.attractorsLeft = static_cast<int>(attractors.size());
    return tree;
}

// --- Da Vinci rule / pipe model ---------------------------------------------------------------

void applyPipeModel(ScaTree& tree, float tipRadius, float exponent) {
    const float n = std::max(0.5f, exponent);
    const int count = static_cast<int>(tree.nodes.size());
    std::vector<float> sumR(static_cast<std::size_t>(count), 0.f); // sum of children radii^n
    std::vector<int> childCount(static_cast<std::size_t>(count), 0);
    // nodes are created children-after-parent -> reverse order processes children first
    for (int i = count - 1; i >= 0; --i) {
        ScaNode& nd = tree.nodes[i];
        float r = 0.f;
        if (nd.alive) r = childCount[i] == 0 ? tipRadius : std::pow(sumR[i], 1.f / n);
        nd.radius = r;
        if (nd.parent >= 0 && nd.alive) {
            sumR[nd.parent] += std::pow(r, n);
            ++childCount[nd.parent];
        }
    }
}

// --- shader/material contract ------------------------------------------------------------------

Vec3 autumnRampColor(const VegetationMaterial& m, float t) {
    const float x = clamp01(t);
    // green(0) -> yellow(0.45) -> orange(0.75) -> red-brown(1); below 0.45 green->yellow blend
    if (x < 0.45f) return fuhgl::math::lerp(m.leafOld, m.autumnLow, smoothstep01(0.08f, 0.45f, x));
    if (x < 0.75f) return fuhgl::math::lerp(m.autumnLow, m.autumnMid, smoothstep01(0.45f, 0.75f, x));
    return fuhgl::math::lerp(m.autumnMid, m.autumnHigh, smoothstep01(0.75f, 1.f, x));
}

Vec3 leafTint(const VegetationMaterial& m, float age, float autumn) {
    const Vec3 green = fuhgl::math::lerp(m.leafOld, m.leafYoung, clamp01(age));
    return fuhgl::math::lerp(green, autumnRampColor(m, autumn), clamp01(autumn));
}

float wetSideFactor(const Vec3& normal, const Vec3& windDir) {
    // the wet weather side faces the incoming wind: normal . (-windDir)
    return clamp01(-fuhgl::math::dot(normal, windDir));
}

// --- generator ----------------------------------------------------------------------------------

VegetationParams VegetationGenerator::randomParams(const EcoEnvironment& eco, unsigned seed) {
    Rng rng(seed);
    VegetationParams p;
    p.eco = eco;
    p.eco.seed = seed;
    p.scale = rng.range(0.7f, 1.4f);
    p.sca.attractors = 380 + static_cast<int>(rng.nextf() * 320.f);
    p.sca.branchProbability = rng.range(0.4f, 0.65f);
    return p;
}

VegetationMesh VegetationGenerator::generate(const VegetationParams& p) const {
    // 1. climate-to-geometry rules, scaled into world units
    GrowthForm form = deriveGrowthForm(p.eco);
    form.maxHeight *= p.scale;
    form.crownBase *= p.scale;
    form.crownRadius *= p.scale;
    form.leafSize *= p.scale;
    form.pruneBelow *= p.scale;
    form.windLean = form.windLean * p.scale;
    ScaParams sca = p.sca;
    sca.step *= p.scale;
    sca.killDistance = std::max(0.05f, sca.killDistance * p.scale);

    // 2. space colonization + 3. pipe model
    ScaTree tree = spaceColonization(form, p.eco, sca, p.eco.seed);
    const float tipRadius = 0.030f * p.scale;
    applyPipeModel(tree, tipRadius, 2.f);
    if (form.succulent) // thick water-storing trunk, fattest at the base
        for (ScaNode& nd : tree.nodes)
            if (nd.alive) nd.radius *= 1.f + form.succulence * 2.2f * (1.f - nd.age);

    // 4. material from the environment (shader contract + baked vertex tints)
    VegetationMaterial mat;
    const float hum = clamp01(p.eco.humidity);
    mat.autumn = std::max(clamp01(p.eco.season), smoothstep01(0.30f, 0.05f, hum) * 0.85f);
    mat.mossCoverage = clamp01(hum * 0.9f);
    mat.sss = lerp(0.85f, 0.15f, form.leafThickness); // thin leaves glow more
    if (form.leaf == LeafMorphology::Needle) {
        mat.leafYoung = {0.38f, 0.58f, 0.22f};
        mat.leafOld = {0.06f, 0.22f, 0.09f};
    } else if (form.leaf == LeafMorphology::Thorn || form.leaf == LeafMorphology::None) {
        mat.leafYoung = {0.50f, 0.44f, 0.24f};
        mat.leafOld = {0.30f, 0.26f, 0.15f};
    }

    VegetationMesh out;
    out.material = mat;
    out.form = form;
    out.height = tree.height;
    out.attractorsLeft = tree.attractorsLeft;

    const Vec3 windDir = fuhgl::math::normalize(p.eco.windDir);
    const Noise crackNoise(p.eco.seed ^ 0xBAA5u);
    float rootRadius = 1e-6f;
    for (const ScaNode& nd : tree.nodes) rootRadius = std::max(rootRadius, nd.radius);

    // branches: bark tint = age (radius) + vertically stretched simplex cracks + moss on the
    // wet side; uv.x = radius/rootRadius, uv.y = wet-side factor
    for (std::size_t i = 1; i < tree.nodes.size(); ++i) {
        const ScaNode& nd = tree.nodes[i];
        if (!nd.alive || nd.parent < 0) continue;
        const ScaNode& par = tree.nodes[nd.parent];
        const float radiusFactor = clamp01(nd.radius / rootRadius);
        Vec3 col = fuhgl::math::lerp(mat.barkYoung, mat.barkOld, radiusFactor);
        const float crack = crackNoise.simplex3(nd.pos.x * mat.barkCrackScale,
                                                 nd.pos.y * mat.barkCrackScale / mat.barkCrackStretch,
                                                 nd.pos.z * mat.barkCrackScale);
        const float darken = smoothstep01(0.45f, 0.85f, crack) * (0.25f + 0.75f * radiusFactor);
        col = col * (1.f - 0.55f * darken);
        addCylinder(out.branches, par.pos, nd.pos, par.radius, nd.radius, 6, col, mat.moss,
                    mat.mossCoverage, radiusFactor, windDir);
    }

    // leaves at the living tips (no living children): morphology from the growth form
    Rng rng(p.eco.seed ^ 0x1EAFu);
    for (std::size_t i = 1; i < tree.nodes.size(); ++i) {
        const ScaNode& nd = tree.nodes[i];
        if (!nd.alive || nd.parent < 0) continue;
        bool hasLivingChild = false;
        for (std::size_t c = i + 1; c < tree.nodes.size(); ++c)
            if (tree.nodes[c].parent == static_cast<int>(i) && tree.nodes[c].alive) {
                hasLivingChild = true;
                break;
            }
        if (hasLivingChild) continue;
        if (form.leaf == LeafMorphology::None) continue;
        const Vec3 dir = fuhgl::math::normalize(nd.pos - tree.nodes[nd.parent].pos);
        const Vec3 col = leafTint(mat, nd.age, mat.autumn);
        const float size = form.leafSize * rng.range(0.85f, 1.15f);
        Vec3 perp = fuhgl::math::cross(dir, {rng.nextf() - 0.5f, rng.nextf() - 0.5f, rng.nextf() - 0.5f});
        if (fuhgl::math::lengthSq(perp) < 1e-6f) perp = fuhgl::math::cross(dir, {0.f, 1.f, 0.f});
        perp = fuhgl::math::normalize(perp);
        const float wet = wetSideFactor(fuhgl::math::normalize(dir + Vec3{0.f, 0.4f, 0.f}), windDir);
        switch (form.leaf) {
            case LeafMorphology::Broad: {
                // two crossed horizontal-ish kites
                const Vec3 outDir = fuhgl::math::normalize(perp + dir * 0.35f);
                const Vec3 side = fuhgl::math::normalize(fuhgl::math::cross(outDir, {0.f, 1.f, 0.f}));
                addLeafKite(out.leaves, nd.pos, outDir, side, size, 0.55f, 0.f, 0.15f, col, nd.age, wet);
                addLeafKite(out.leaves, nd.pos, side, outDir, size, 0.55f, 0.f, 0.15f, col, nd.age, wet);
                break;
            }
            case LeafMorphology::DripTip: {
                // huge thin leaf with a long drooping drip tip (rain runs off)
                const Vec3 outDir = fuhgl::math::normalize(perp + dir * 0.2f);
                const Vec3 side = fuhgl::math::normalize(fuhgl::math::cross(outDir, {0.f, 1.f, 0.f}));
                addLeafKite(out.leaves, nd.pos, outDir, side, size, 0.62f, form.dripTip * 1.8f,
                            form.dripTip * 0.9f, col, nd.age, wet);
                addLeafKite(out.leaves, nd.pos, side, outDir, size * 0.8f, 0.5f, form.dripTip * 1.4f,
                            form.dripTip * 0.7f, col, nd.age, wet);
                break;
            }
            case LeafMorphology::Needle: {
                // 3 thin crossed needle quads along the branch direction
                for (int k = 0; k < 3; ++k) {
                    const Vec3 p2 = rotateAroundAxis(perp, dir, 2.f * fuhgl::math::kPi * k / 3.f);
                    addLeafKite(out.leaves, nd.pos, dir, p2, size, 0.10f, 0.9f, 0.f, col, nd.age, wet);
                }
                break;
            }
            case LeafMorphology::Thorn:
                addThorn(out.leaves, nd.pos, dir, perp, size, col, nd.age, wet);
                break;
            case LeafMorphology::None:
                break;
        }
    }
    return out;
}

} // namespace fuhglgraphics::procedural
