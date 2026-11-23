#include "fuhglgraphics/scene/picking.hpp"

#include "fuhglgraphics/scene/component.hpp"
#include "fuhglgraphics/scene/node.hpp"
#include "fuhglgraphics/scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fuhglgraphics::scene {

Ray rayFromScreen(const fuhgl::Camera& cam, float aspect, int width, int height, float px, float py) {
    Ray r;
    if (width <= 0 || height <= 0) return r;
    const float nx = (px + 0.5f) / static_cast<float>(width) * 2.f - 1.f;
    const float ny = 1.f - (py + 0.5f) / static_cast<float>(height) * 2.f; // py from the top
    const fuhgl::math::Mat4 inv = cam.viewProj(aspect).inverse();
    const fuhgl::math::Vec4 p0 = inv * fuhgl::math::Vec4{nx, ny, -1.f, 1.f};
    const fuhgl::math::Vec4 p1 = inv * fuhgl::math::Vec4{nx, ny, 1.f, 1.f};
    const fuhgl::math::Vec3 a{p0.x / p0.w, p0.y / p0.w, p0.z / p0.w};
    const fuhgl::math::Vec3 b{p1.x / p1.w, p1.y / p1.w, p1.z / p1.w};
    r.origin = a;
    r.dir = fuhgl::math::normalize(b - a);
    return r;
}

bool rayIntersectAabb(const Ray& r, const fuhgl::math::Vec3& mn, const fuhgl::math::Vec3& mx,
                      float& tOut) {
    float tmin = 0.f, tmax = std::numeric_limits<float>::max();
    const float o[3] = {r.origin.x, r.origin.y, r.origin.z};
    const float d[3] = {r.dir.x, r.dir.y, r.dir.z};
    const float lo[3] = {mn.x, mn.y, mn.z};
    const float hi[3] = {mx.x, mx.y, mx.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-8f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
        } else {
            float t1 = (lo[i] - o[i]) / d[i];
            float t2 = (hi[i] - o[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = tmin > t1 ? tmin : t1;
            tmax = tmax < t2 ? tmax : t2;
            if (tmin > tmax) return false;
        }
    }
    tOut = tmin;
    return true;
}

bool rayIntersectTriangle(const Ray& r, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
                          const fuhgl::math::Vec3& c, float& t, float& u, float& v) {
    constexpr float kEps = 1e-7f;
    const fuhgl::math::Vec3 e1 = b - a;
    const fuhgl::math::Vec3 e2 = c - a;
    const fuhgl::math::Vec3 p = fuhgl::math::cross(r.dir, e2);
    const float det = fuhgl::math::dot(e1, p);
    if (std::fabs(det) < kEps) return false; // parallel
    const float invDet = 1.f / det;
    const fuhgl::math::Vec3 tv = r.origin - a;
    u = fuhgl::math::dot(tv, p) * invDet;
    if (u < -kEps || u > 1.f + kEps) return false;
    const fuhgl::math::Vec3 q = fuhgl::math::cross(tv, e1);
    v = fuhgl::math::dot(r.dir, q) * invDet;
    if (v < -kEps || u + v > 1.f + kEps) return false;
    t = fuhgl::math::dot(e2, q) * invDet;
    return t > kEps;
}

namespace {

void pickNode(const Node& node, const Ray& ray, PickHit& best) {
    if (node.hasAabb()) {
        fuhgl::math::Vec3 mn, mx;
        node.worldAabb(mn, mx);
        float tAabb = 0.f;
        if (!rayIntersectAabb(ray, mn, mx, tAabb)) return; // subtree outside the ray
        if (best.hit && tAabb > best.distance) return;      // already closer
    }
    if (const MeshComponent* mc = node.find<MeshComponent>()) {
        const fuhgl::math::Mat4& w = node.worldMatrix();
        for (std::size_t ei = 0; ei < mc->meshes.size(); ++ei) {
            const MeshEntry& e = mc->meshes[ei];
            if (!e.meshData) continue; // no CPU triangles: not pickable
            const fuhgl::math::Mat4 m = w * e.localOffset;
            const fuhgl::render::MeshData& md = *e.meshData;
            const std::size_t vcount = md.vertexCount();
            if (vcount == 0) continue;
            auto vertPos = [&](std::size_t vi) {
                const std::size_t o = vi * fuhgl::render::MeshData::kFloatsPerVertex;
                const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{md.vertices[o], md.vertices[o + 1],
                                                                 md.vertices[o + 2], 1.f};
                return fuhgl::math::Vec3{p.x, p.y, p.z};
            };
            const std::size_t triCount =
                !md.indices.empty() ? md.indices.size() / 3 : vcount / 3;
            for (std::size_t ti = 0; ti < triCount; ++ti) {
                std::size_t i0, i1, i2;
                if (!md.indices.empty()) {
                    i0 = md.indices[ti * 3];
                    i1 = md.indices[ti * 3 + 1];
                    i2 = md.indices[ti * 3 + 2];
                } else {
                    i0 = ti * 3;
                    i1 = ti * 3 + 1;
                    i2 = ti * 3 + 2;
                }
                if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;
                float t = 0.f, u = 0.f, v = 0.f;
                if (rayIntersectTriangle(ray, vertPos(i0), vertPos(i1), vertPos(i2), t, u, v)) {
                    if (!best.hit || t < best.distance) {
                        best.hit = true;
                        best.node = &node;
                        best.meshEntry = static_cast<int>(ei);
                        best.distance = t;
                        best.point = ray.origin + ray.dir * t;
                        best.triangle = static_cast<std::uint32_t>(ti);
                    }
                }
            }
        }
    }
    for (const auto& ch : node.children()) pickNode(*ch, ray, best);
}

} // namespace

bool pick(Scene& scene, const fuhgl::Camera* camera, int width, int height, float px, float py,
          PickHit& out) {
    out = PickHit{};
    if (!camera) return false;
    scene.updateWorldTransforms();
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.f;
    const Ray ray = rayFromScreen(*camera, aspect, width, height, px, py);
    PickHit best;
    pickNode(scene.root(), ray, best);
    out = best;
    return best.hit;
}

} // namespace fuhglgraphics::scene
