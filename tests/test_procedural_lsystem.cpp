// Unit tests for G26 L-systems (label "unit"): derive() rewriting rules + determinism +
// bracket balance of presets, generate() mesh validity (finite, indexed, unit normals,
// expected bounds), presets differ, vertex colors.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/lsystem.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

using fuhglgraphics::procedural::LSystem;

void testDerive() {
    LSystem s;
    s.axiom = "F";
    s.rules = {{'F', "FF"}};
    s.iterations = 2;
    CHECK(s.derive() == "FFFF");
    s.iterations = 0;
    CHECK(s.derive() == "F");
    // unknown symbols pass through, multiple rules
    LSystem s2;
    s2.axiom = "X";
    s2.rules = {{'X', "F[+X]"}, {'F', "FF"}};
    s2.iterations = 1;
    CHECK(s2.derive() == "F[+X]");
    s2.iterations = 2;
    CHECK(s2.derive() == "FF[+F[+X]]");
    // determinism
    CHECK(s2.derive() == s2.derive());
}

void testPresetBrackets() {
    for (const LSystem& s : {LSystem::makeTree(0), LSystem::makeTree(1), LSystem::makeTree(2),
                             LSystem::makeFern(), LSystem::makeBush()}) {
        const std::string code = s.derive();
        CHECK(!code.empty());
        int depth = 0;
        for (const char c : code) {
            if (c == '[') ++depth;
            if (c == ']') --depth;
            CHECK(depth >= 0);
        }
        CHECK(depth == 0); // balanced
        CHECK(code.size() < 200000); // sane growth
    }
}

void testGenerate() {
    const LSystem tree = LSystem::makeTree(0);
    const fuhgl::render::MeshData md = tree.generate({0.3f, 0.2f, 0.1f}, {0.1f, 0.4f, 0.1f});
    CHECK(!md.indices.empty());
    CHECK(md.indices.size() % 3 == 0);
    CHECK(md.vertices.size() == md.vertexCount() * 11);
    // indices in range, all finite, unit normals
    fuhgl::math::Vec3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i) {
        const float* v = &md.vertices[i * 11];
        for (int k = 0; k < 11; ++k) CHECK(std::isfinite(v[k]));
        const fuhgl::math::Vec3 p{v[0], v[1], v[2]};
        const fuhgl::math::Vec3 n{v[3], v[4], v[5]};
        CHECK_NEAR(fuhgl::math::length(n), 1.f, 1e-3f);
        mn = {std::fmin(mn.x, p.x), std::fmin(mn.y, p.y), std::fmin(mn.z, p.z)};
        mx = {std::fmax(mx.x, p.x), std::fmax(mx.y, p.y), std::fmax(mx.z, p.z)};
    }
    for (const std::uint32_t idx : md.indices) CHECK(idx < md.vertexCount());
    // tree grows upward from the origin and stays bounded
    CHECK(mn.y >= -0.01f);
    CHECK(mx.y > 1.f); // grows upward
    CHECK(mx.x - mn.x < 500.f && mx.z - mn.z < 500.f);
    // trunk color on early (cylinder) vertices
    CHECK_NEAR(md.vertices[6], 0.3f, 1e-6f);
}

void testPresetsDiffer() {
    const auto a = LSystem::makeTree(0).generate();
    const auto b = LSystem::makeTree(1).generate();
    const auto c = LSystem::makeFern().generate();
    const auto d = LSystem::makeBush().generate();
    CHECK(!a.indices.empty() && !b.indices.empty() && !c.indices.empty() && !d.indices.empty());
    CHECK(a.indices.size() != b.indices.size() || a.vertices.size() != b.vertices.size());
    CHECK(a.indices.size() != c.indices.size());
    // leaf vertices carry the leaf color somewhere
    bool hasLeaf = false;
    for (std::uint32_t i = 0; i < c.vertexCount(); ++i)
        if (std::fabs(c.vertices[i * 11 + 6] - 0.16f) < 1e-6f &&
            std::fabs(c.vertices[i * 11 + 7] - 0.42f) < 1e-6f)
            hasLeaf = true;
    CHECK(hasLeaf);
    // determinism
    CHECK(LSystem::makeTree(0).generate().vertices == a.vertices);
}

} // namespace

void runProceduralLsystemTests() {
    testDerive();
    testPresetBrackets();
    testGenerate();
    testPresetsDiffer();
}
