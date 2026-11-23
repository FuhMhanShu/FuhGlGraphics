// Unit tests for the G9 scene graph (no GL context needed): TRS + world matrices,
// Effects list/merge, instancing layouts, components (attach/find/detach), and
// Scene::fillFrame (items incl. several meshes per node, materialIds/castShadow,
// lights, camera component, effect union, frustum culling, world AABBs).
// Meshes are default-constructed (never create()d) — the Mesh/Buffer ctors/dtors
// are guarded by gl::isInitialized(), so this runs without a GL context.

#include "test_framework.hpp"

#include <fuhglgraphics/scene/scene.hpp>

#include <fuhgl/cam/first_person.hpp>
#include <fuhgl/render/materials.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cmath>
#include <vector>

using namespace fuhglgraphics::scene;
using fuhglgraphics::render::Effect;

namespace {

bool matNear(const fuhgl::math::Mat4& a, const fuhgl::math::Mat4& b) {
    for (int i = 0; i < 16; ++i)
        if (std::fabs(a.m[i] - b.m[i]) > 1e-5f) return false;
    return true;
}

} // namespace

void runSceneTests() {
    // --- TRS: local matrix = translate * rotate * scale ---
    {
        Node n("n");
        n.translation = {1.f, 2.f, 3.f};
        n.scale = {2.f, 2.f, 2.f};
        fuhgl::math::Vec4 p = n.localMatrix() * fuhgl::math::Vec4{1.f, 0.f, 0.f, 1.f};
        CHECK_NEAR(p.x, 3.f, 1e-5);
        CHECK_NEAR(p.y, 2.f, 1e-5);
        CHECK_NEAR(p.z, 3.f, 1e-5);
        // rotation 180 deg about Y maps +X to -X
        n.rotation = fuhgl::math::fromAxisAngle(fuhgl::math::kPi, {0.f, 1.f, 0.f});
        n.scale = {1.f, 1.f, 1.f};
        p = n.localMatrix() * fuhgl::math::Vec4{1.f, 0.f, 0.f, 1.f};
        CHECK_NEAR(p.x, 0.f, 1e-5);
        CHECK_NEAR(p.y, 2.f, 1e-5);
        CHECK_NEAR(p.z, 3.f, 1e-5);
    }

    // --- hierarchy: world matrix composition, parent/children ---
    {
        Scene scene;
        Node& parent = scene.root().createChild("parent");
        parent.translation = {10.f, 0.f, 0.f};
        Node& child = parent.createChild("child");
        child.translation = {0.f, 5.f, 0.f};
        scene.updateWorldTransforms();
        const fuhgl::math::Vec4 o = child.worldMatrix() * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
        CHECK_NEAR(o.x, 10.f, 1e-5);
        CHECK_NEAR(o.y, 5.f, 1e-5);
        CHECK_NEAR(o.z, 0.f, 1e-5);
        CHECK(child.parent() == &parent);
        CHECK(scene.root().childCount() == 1);
        CHECK(parent.childCount() == 1);
        CHECK(parent.children().front().get() == &child);
    }

    // --- Effects: enable/disable/setAll + merge (union, later params win) ---
    {
        Effects a, b;
        a.enable(Effect::Taa);
        a.enable(Effect::Taa); // no duplicates
        CHECK(a.list.size() == 1);
        CHECK(a.enabled(Effect::Taa));
        b.enable(Effect::Upscale);
        b.enable(Effect::Shadows);
        a.params.exposure = 1.5f;
        b.params.exposure = 2.5f;
        a.merge(b);
        CHECK(a.enabled(Effect::Taa) && a.enabled(Effect::Upscale) && a.enabled(Effect::Shadows));
        CHECK_NEAR(a.params.exposure, 2.5f, 1e-6);
        a.disable(Effect::Taa);
        CHECK(!a.enabled(Effect::Taa) && a.enabled(Effect::Upscale));
        Effects all;
        all.setAll(true);
        CHECK(all.list.size() == static_cast<std::size_t>(fuhglgraphics::render::kEffectCount));
        all.setAll(false);
        CHECK(all.list.empty());
    }

    // --- Instancing: grid layout + seeded random layout (deterministic, bounded) ---
    {
        Instancing g;
        g.enabled = true;
        g.count = 9;
        g.layout = InstanceLayout::Grid;
        g.extent = {6.f, 0.f, 6.f};
        const std::vector<fuhgl::math::Mat4> m = g.makeLocalInstances();
        CHECK(m.size() == 9);
        for (const auto& mat : m) {
            const fuhgl::math::Vec4 p = mat * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
            CHECK(std::fabs(p.x) <= 3.0001f);
            CHECK(std::fabs(p.y) < 1e-6f);
            CHECK(std::fabs(p.z) <= 3.0001f);
        }
        const fuhgl::math::Vec4 first = m.front() * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
        CHECK_NEAR(first.x, -3.f, 1e-5);
        CHECK_NEAR(first.z, -3.f, 1e-5);

        Instancing r1;
        r1.layout = InstanceLayout::Random;
        r1.count = 16;
        r1.extent = {4.f, 2.f, 4.f};
        r1.seed = 42;
        Instancing r2 = r1;
        const std::vector<fuhgl::math::Mat4> a = r1.makeLocalInstances();
        const std::vector<fuhgl::math::Mat4> b = r2.makeLocalInstances();
        CHECK(a.size() == 16 && b.size() == 16);
        bool same = true;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!matNear(a[i], b[i])) same = false;
            const fuhgl::math::Vec4 p = a[i] * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
            CHECK(std::fabs(p.x) <= 2.0001f);
            CHECK(std::fabs(p.y) <= 1.0001f);
            CHECK(std::fabs(p.z) <= 2.0001f);
        }
        CHECK(same); // fixed seed -> deterministic

        Instancing off;
        off.count = 0;
        CHECK(off.makeLocalInstances().empty());

        // G20/sunrise: heightFn snaps instances onto the ground (grass on a heightmap)
        Instancing h;
        h.layout = InstanceLayout::Random;
        h.count = 32;
        h.extent = {10.f, 1.f, 10.f};
        h.seed = 7;
        h.heightFn = [](float x, float z) { return 2.f * x + z; };
        const std::vector<fuhgl::math::Mat4> hm = h.makeLocalInstances();
        CHECK(hm.size() == 32);
        for (const auto& mat : hm) {
            const fuhgl::math::Vec4 p = mat * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
            CHECK_NEAR(p.y, 2.f * p.x + p.z, 1e-5f); // y follows the height callback
        }
        // without heightFn the behavior is unchanged (random y in the box)
        h.heightFn = nullptr;
        const std::vector<fuhgl::math::Mat4> hm2 = h.makeLocalInstances();
        CHECK(hm2.size() == 32);
        bool anyY = false;
        for (const auto& mat : hm2) {
            const fuhgl::math::Vec4 p = mat * fuhgl::math::Vec4{0.f, 0.f, 0.f, 1.f};
            if (std::fabs(p.y) > 1e-4f) anyY = true;
        }
        CHECK(anyY);
    }

    // --- components: attach / find / findAll / detach ---
    {
        Node n("c");
        MeshComponent& mc = n.attach<MeshComponent>();
        LightComponent& lc = n.attach<LightComponent>();
        CHECK(n.find<MeshComponent>() == &mc);
        CHECK(n.find<LightComponent>() == &lc);
        CHECK(n.find<CameraComponent>() == nullptr);
        CHECK(mc.node() == &n);
        n.attach<MeshComponent>();
        CHECK(n.findAll<MeshComponent>().size() == 2);
        Component* second = n.components().back().get();
        CHECK(n.detach(second));
        CHECK(n.findAll<MeshComponent>().size() == 1);
        Component dummy;
        CHECK(!n.detach(&dummy));
        CHECK(n.components().size() == 2);
    }

    // --- fillFrame: camera component, several meshes per node, ids/castShadow,
    //     owned preset sync, explicit preset passthrough, light transform,
    //     effect union + param override, world AABB ---
    {
        fuhgl::render::Mesh meshA, meshB; // no GL: default-constructed only
        fuhgl::FirstPersonCamera cam;
        cam.position = {0.f, 1.5f, 6.f};
        cam.pitch = -0.2f;

        Scene scene;
        Node& camNode = scene.root().createChild("cam");
        CameraComponent& cc = camNode.attach<CameraComponent>();
        cc.camera = &cam;

        Node& lightNode = scene.root().createChild("sun"); // created before obj: obj is traversed last
        lightNode.translation = {0.f, 10.f, 0.f};
        lightNode.rotation = fuhgl::math::fromAxisAngle(fuhgl::math::kPi, {0.f, 1.f, 0.f});
        LightComponent& lc = lightNode.attach<LightComponent>();
        lc.light = fuhgl::render::Light::directional({1.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, 2.f);

        Node& obj = scene.root().createChild("obj");
        obj.translation = {1.f, 0.f, 0.f};
        MeshComponent& mc = obj.attach<MeshComponent>();
        MeshEntry& e1 = mc.addMesh(&meshA);
        e1.materialId = 7;
        e1.castShadow = true;
        e1.albedo = {0.9f, 0.2f, 0.1f};
        e1.metallic = 0.8f;
        e1.roughness = 0.3f;
        e1.hasAabb = true;
        e1.aabbMin = {-0.5f, -0.5f, -0.5f};
        e1.aabbMax = {0.5f, 0.5f, 0.5f};
        e1.localOffset = fuhgl::math::translate({0.f, 1.f, 0.f});
        MeshEntry& e2 = mc.addMesh(&meshB); // second mesh on the same node
        e2.materialId = 8;
        e2.castShadow = false;
        e2.hasAabb = true; // small explicit AABB inside e1's box (same offset) -> union stays e1's
        e2.aabbMin = {-0.1f, -0.1f, -0.1f};
        e2.aabbMax = {0.1f, 0.1f, 0.1f};
        e2.localOffset = fuhgl::math::translate({0.f, 1.f, 0.f});
        static fuhgl::render::Material preset = fuhgl::render::materials::pbr({0.2f, 0.8f, 0.2f}, 0.f, 0.6f);
        e2.material = &preset;

        scene.root().effects.enable(Effect::Taa);
        scene.root().effects.params.exposure = 9.f;
        obj.effects.enable(Effect::Upscale);
        obj.effects.params.exposure = 3.f; // obj is traversed after root -> its params win

        FrameData frame;
        scene.fillFrame(frame, nullptr, 320, 200); // camera from the CameraComponent
        CHECK(frame.view.camera == &cam);
        CHECK(frame.view.width == 320 && frame.view.height == 200);
        CHECK_NEAR(frame.view.aspect, 1.6, 1e-5);
        CHECK(frame.view.items.size() == 2);
        CHECK(frame.materialIds.size() == 2 && frame.castShadow.size() == 2);
        CHECK(frame.materialIds[0] == 7u && frame.materialIds[1] == 8u);
        CHECK(frame.castShadow[0] && !frame.castShadow[1]);
        // model = world * localOffset: obj at x=1, entry offset y=1
        const fuhgl::render::DrawItem& it0 = frame.view.items[0];
        CHECK(it0.mesh == &meshA);
        CHECK_NEAR(it0.model.m[12], 1.f, 1e-5);
        CHECK_NEAR(it0.model.m[13], 1.f, 1e-5);
        CHECK_NEAR(it0.model.m[14], 0.f, 1e-5);
        // inline entry fields synced into an owned preset; explicit preset passed through
        CHECK(it0.material && it0.material->builtinShader);
        CHECK_NEAR(it0.material->albedo.x, 0.9, 1e-6);
        CHECK_NEAR(it0.material->metallic, 0.8, 1e-6);
        CHECK(frame.view.items[1].material == &preset);
        // light: position transformed by the world matrix, direction rotated 180 deg about Y
        CHECK(frame.view.lights.size() == 1);
        CHECK_NEAR(frame.view.lights[0].position.x, 0.f, 1e-5);
        CHECK_NEAR(frame.view.lights[0].position.y, 10.f, 1e-5);
        CHECK_NEAR(frame.view.lights[0].direction.x, -1.f, 1e-4);
        CHECK_NEAR(frame.view.lights[0].direction.y, 0.f, 1e-4);
        // effects merged over the graph: union of lists, deeper node's params win
        CHECK(frame.effects.enabled(Effect::Taa) && frame.effects.enabled(Effect::Upscale));
        CHECK_NEAR(frame.effects.params.exposure, 3.f, 1e-6);
        // world AABB of obj: explicit local AABB through world * localOffset
        fuhgl::math::Vec3 mn, mx;
        obj.worldAabb(mn, mx);
        CHECK(obj.hasAabb());
        CHECK_NEAR(mn.x, 0.5f, 1e-5);
        CHECK_NEAR(mx.x, 1.5f, 1e-5);
        CHECK_NEAR(mn.y, 0.5f, 1e-5);
        CHECK_NEAR(mx.y, 1.5f, 1e-5);
    }

    // --- frustum culling in fillFrame ---
    {
        fuhgl::render::Mesh mesh;
        fuhgl::FirstPersonCamera cam; // default position (0,0,3), yaw 0 -> looks down -Z
        Scene scene;
        Node& nearNode = scene.root().createChild("near"); // around the origin: in front
        MeshEntry& e1 = nearNode.attach<MeshComponent>().addMesh(&mesh);
        e1.hasAabb = true;
        e1.aabbMin = {-1.f, -1.f, -1.f};
        e1.aabbMax = {1.f, 1.f, 1.f};
        Node& farNode = scene.root().createChild("far"); // behind the camera: culled
        farNode.translation = {0.f, 0.f, 100.f};
        MeshEntry& e2 = farNode.attach<MeshComponent>().addMesh(&mesh);
        e2.hasAabb = true;
        e2.aabbMin = {-1.f, -1.f, -1.f};
        e2.aabbMax = {1.f, 1.f, 1.f};

        FrameData culled;
        scene.fillFrame(culled, &cam, 100, 100, true);
        CHECK(culled.view.items.size() == 1);
        FrameData all;
        scene.fillFrame(all, &cam, 100, 100, false);
        CHECK(all.view.items.size() == 2);

        Node bare("bare"); // no geometry -> always visible
        const fuhgl::math::Frustum fr = fuhgl::math::Frustum::fromViewProj(cam.viewProj(1.f));
        CHECK(bare.visibleIn(fr));
        CHECK(!bare.hasAabb());
        CHECK(nearNode.visibleIn(fr));
        CHECK(!farNode.visibleIn(fr));
    }
}
