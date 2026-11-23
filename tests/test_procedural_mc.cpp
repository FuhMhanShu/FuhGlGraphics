// Unit tests for G25 marching cubes (label "unit"): sampleField, empty/degenerate volumes,
// sphere iso-surface (manifold, outward normals, radius bounds), plane field (flat, consistent
// normals, exact triangle count), chunked == single triangle count, seam coverage.

#include "test_framework.hpp"

#include <fuhglgraphics/procedural/marching_cubes.hpp>
#include <fuhglgraphics/procedural/noise.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

using fuhglgraphics::procedural::VolumeGrid;
using fuhglgraphics::procedural::marchingCubes;
using fuhglgraphics::procedural::marchingCubesChunked;
using fuhglgraphics::procedural::sampleField;
using fuhgl::math::Vec3;

Vec3 vertexPos(const fuhgl::render::MeshData& md, std::uint32_t i) {
    return {md.vertices[i * 11 + 0], md.vertices[i * 11 + 1], md.vertices[i * 11 + 2]};
}
Vec3 vertexNormal(const fuhgl::render::MeshData& md, std::uint32_t i) {
    return {md.vertices[i * 11 + 3], md.vertices[i * 11 + 4], md.vertices[i * 11 + 5]};
}

void testSampleField() {
    const VolumeGrid g = sampleField([](const Vec3& p) { return p.x + 2.f * p.y; },
                                     {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, 5, 4, 3);
    CHECK(g.ok());
    CHECK(g.size() == 5u * 4u * 3u);
    CHECK_NEAR(g.at(0, 0, 0), 0.f, 1e-6f);
    CHECK_NEAR(g.at(4, 3, 2), 1.f + 2.f * 1.f, 1e-6f); // pos(4,3,2) = (1,1,1)
    CHECK_NEAR(g.pos(4, 0, 0).x, 1.f, 1e-6f);
}

void testDegenerate() {
    VolumeGrid g;
    CHECK(!g.ok());
    CHECK(marchingCubes(g, 0.f).indices.empty());
    // all inside / all outside -> empty
    const VolumeGrid allIn = sampleField([](const Vec3&) { return -1.f; }, {0, 0, 0}, {1, 1, 1}, 4, 4, 4);
    CHECK(marchingCubes(allIn, 0.f).indices.empty());
    const VolumeGrid allOut = sampleField([](const Vec3&) { return 1.f; }, {0, 0, 0}, {1, 1, 1}, 4, 4, 4);
    CHECK(marchingCubes(allOut, 0.f).indices.empty());
}

void testSphere() {
    const int N = 24;
    const VolumeGrid g = sampleField(
        [](const Vec3& p) { return fuhgl::math::length(p) - 0.7f; },
        {-1.2f, -1.2f, -1.2f}, {1.2f, 1.2f, 1.2f}, N, N, N);
    const fuhgl::render::MeshData md = marchingCubes(g, 0.f, {0.8f, 0.6f, 0.4f});
    CHECK(!md.indices.empty());
    CHECK(md.indices.size() % 3 == 0);
    CHECK(md.vertexCount() * 11 == md.vertices.size());
    // every vertex on the sphere shell
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i) {
        const float r = fuhgl::math::length(vertexPos(md, i));
        CHECK(r > 0.55f && r < 0.85f);
        CHECK_NEAR(md.vertices[i * 11 + 6], 0.8f, 1e-6f); // color r
    }
    // normals point outward (away from the center) for the vast majority
    int outward = 0;
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i)
        if (fuhgl::math::dot(vertexNormal(md, i), vertexPos(md, i)) > 0.f) ++outward;
    CHECK(outward > static_cast<int>(md.vertexCount()) * 9 / 10);
    // manifold: every undirected edge shared by exactly 2 triangles
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edgeCount;
    for (std::size_t i = 0; i < md.indices.size(); i += 3) {
        const std::uint32_t a = md.indices[i], b = md.indices[i + 1], c = md.indices[i + 2];
        CHECK(a != b && b != c && a != c);
        const std::uint32_t tri[3] = {a, b, c};
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t u = tri[e], v = tri[(e + 1) % 3];
            ++edgeCount[std::minmax(u, v)];
        }
    }
    CHECK(!edgeCount.empty());
    for (const auto& kv : edgeCount) CHECK(kv.second == 2);
    // Euler characteristic of a sphere-like closed mesh: V - E + F = 2
    const long V = static_cast<long>(md.vertexCount());
    const long E = static_cast<long>(edgeCount.size());
    const long F = static_cast<long>(md.indices.size() / 3);
    CHECK(V - E + F == 2);
}

void testPlane() {
    const int N = 8;
    const VolumeGrid g = sampleField([](const Vec3& p) { return p.y - 0.5f; },
                                     {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, N, N, N);
    const fuhgl::render::MeshData md = marchingCubes(g, 0.f);
    CHECK(!md.indices.empty());
    // all vertices on the y = 0.5 plane
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i)
        CHECK_NEAR(vertexPos(md, i).y, 0.5f, 1e-4f);
    // normals consistent (all +y or all -y)
    int up = 0, down = 0;
    for (std::uint32_t i = 0; i < md.vertexCount(); ++i) {
        const Vec3 n = vertexNormal(md, i);
        if (n.y > 0.9f) ++up;
        else if (n.y < -0.9f) ++down;
    }
    CHECK(up == 0 || down == 0);
    CHECK(up + down == static_cast<int>(md.vertexCount()));
    // a plane through the volume cuts (N-1)^2 cells with 2 triangles each
    CHECK(md.indices.size() / 3 == static_cast<std::size_t>(2 * (N - 1) * (N - 1)));
}

void testChunked() {
    const int N = 20;
    const VolumeGrid g = sampleField(
        [](const Vec3& p) { return fuhgl::math::length(p) - 0.6f; },
        {-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}, N, N, N);
    const fuhgl::render::MeshData single = marchingCubes(g, 0.f);
    const auto chunks = marchingCubesChunked(g, 0.f, 3, 2, 4);
    CHECK(!chunks.empty());
    CHECK(chunks.size() <= 3u * 2u * 4u);
    std::size_t tris = 0;
    for (const auto& c : chunks) {
        CHECK(!c.indices.empty());
        CHECK(c.indices.size() % 3 == 0);
        tris += c.indices.size() / 3;
    }
    // chunks cover every cell exactly once -> identical triangle count to the single mesh
    CHECK(tris == single.indices.size() / 3);
    // degenerate chunk counts fall back to 1 per axis
    const auto one = marchingCubesChunked(g, 0.f, 0, 0, 0);
    CHECK(one.size() == 1);
    CHECK(one[0].indices.size() == single.indices.size());
}

void testNoiseVolume() {
    // G24 + G25 together: fBm volume -> organic blob, must be manifold-closed where the
    // iso-surface does not touch the volume border
    const fuhglgraphics::procedural::Noise noise(1234u);
    const int N = 24;
    const VolumeGrid g = sampleField(
        [&](const Vec3& p) {
            const Vec3 q = p * 2.2f;
            return fuhgl::math::length(p) - 0.55f + 0.15f * noise.fbm3(q.x, q.y, q.z, 4);
        },
        {-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}, N, N, N);
    const fuhgl::render::MeshData md = marchingCubes(g, 0.f);
    CHECK(md.indices.size() > 300);
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edgeCount;
    for (std::size_t i = 0; i < md.indices.size(); i += 3) {
        const std::uint32_t tri[3] = {md.indices[i], md.indices[i + 1], md.indices[i + 2]};
        for (int e = 0; e < 3; ++e) ++edgeCount[std::minmax(tri[e], tri[(e + 1) % 3])];
    }
    for (const auto& kv : edgeCount) CHECK(kv.second == 2); // closed blob -> manifold
}

} // namespace

void runProceduralMcTests() {
    testSampleField();
    testDegenerate();
    testSphere();
    testPlane();
    testChunked();
    testNoiseVolume();
}
