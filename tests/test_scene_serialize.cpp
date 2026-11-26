// Unit tests for scene/serialize.hpp (.fasset scene serialization, G12): roundtrip
// scene -> Json -> writeJson -> parseJson -> scene -> Json must be identical; field
// spot-checks; error paths. Pure CPU, label "unit".

#include "test_framework.hpp"

#include <fuhglgraphics/graphics.hpp>

#include <string>

namespace {

using namespace fuhglgraphics;

void buildDemoScene(scene::Scene& scene) {
    scene.root().setName("root");
    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.exposure = 1.25f;
    scene.root().effects.params.shadowRadius = 33.f;

    scene::Node& box = scene.root().createChild("box");
    box.translation = {1.f, 2.f, 3.f};
    box.rotation = fuhgl::math::Quat{0.f, 0.7071f, 0.f, 0.7071f};
    box.scale = {2.f, 2.f, 2.f};
    box.instancing.enabled = true;
    box.instancing.count = 12;
    box.instancing.layout = scene::InstanceLayout::Random;
    box.instancing.extent = {5.f, 1.f, 5.f};
    box.instancing.seed = 7;
    auto& mc = box.attach<scene::MeshComponent>();
    scene::MeshEntry& e = mc.addMesh(nullptr);
    e.assetRef = "primitive:box";
    e.albedo = {0.8f, 0.2f, 0.1f};
    e.metallic = 0.9f;
    e.roughness = 0.3f;
    e.castShadow = false;
    e.materialId = 5;
    e.localOffset = fuhgl::math::translate({0.f, 1.f, 0.f});
    e.albedoTexturePath = "textures/wood.png";

    scene::Node& sun = scene.root().createChild("sun");
    auto& lc = sun.attach<scene::LightComponent>();
    lc.light = fuhgl::render::Light::directional({-0.5f, -1.f, -0.3f}, {1.f, 0.9f, 0.8f}, 3.f);

    scene::Node& child = box.createChild("child");
    child.translation = {0.f, 0.5f, 0.f};
    child.attach<scene::CameraComponent>(); // runtime-only: must be skipped on load
}

void testSerializeRoundtrip() {
    std::string err;
    scene::Scene scene;
    buildDemoScene(scene);

    io::Json j;
    CHECK(scene::serializeScene(scene, j, &err));

    std::string text;
    CHECK(io::writeJson(j, text, true, &err));
    io::Json parsed;
    CHECK(io::parseJson(text, parsed, &err));

    scene::Scene scene2;
    CHECK(scene::deserializeScene(scene2, parsed, &err));

    io::Json j2;
    CHECK(scene::serializeScene(scene2, j2, &err));
    std::string text2;
    CHECK(io::writeJson(j2, text2, true, &err));
    CHECK(text == text2); // deterministic: std::map key order + fixed layout

    // field spot-checks on the reloaded scene
    CHECK(scene2.root().name() == "root");
    CHECK(scene2.root().childCount() == 2);
    const scene::Node* box = nullptr;
    for (const auto& c : scene2.root().children())
        if (c->name() == "box") box = c.get();
    CHECK(box != nullptr);
    if (box) {
        CHECK(box->translation.x == 1.f && box->translation.y == 2.f && box->translation.z == 3.f);
        CHECK(std::fabs(box->rotation.y - 0.7071f) < 1e-6f);
        CHECK(box->scale.x == 2.f);
        CHECK(box->instancing.enabled && box->instancing.count == 12 &&
              box->instancing.layout == scene::InstanceLayout::Random && box->instancing.seed == 7);
        CHECK(box->childCount() == 1);
        CHECK(box->children()[0]->name() == "child");
        const auto* mc = box->find<scene::MeshComponent>();
        CHECK(mc != nullptr && mc->meshes.size() == 1);
        if (mc && mc->meshes.size() == 1) {
            const scene::MeshEntry& e = mc->meshes[0];
            CHECK(e.mesh == nullptr); // GL object recreated by the editor, not serialized
            CHECK(e.assetRef == "primitive:box");
            CHECK(e.albedoTexturePath == "textures/wood.png");
            CHECK(e.albedo.x == 0.8f && e.metallic == 0.9f && e.roughness == 0.3f);
            CHECK(!e.castShadow && e.materialId == 5);
            CHECK(std::fabs(e.localOffset(3, 1) - 1.f) < 1e-6f); // translate y = 1
        }
        // camera component is runtime-only -> not restored
        CHECK(box->children()[0]->find<scene::CameraComponent>() == nullptr);
    }
    const scene::Node* sun = nullptr;
    for (const auto& c : scene2.root().children())
        if (c->name() == "sun") sun = c.get();
    CHECK(sun != nullptr);
    if (sun) {
        const auto* lc = sun->find<scene::LightComponent>();
        CHECK(lc != nullptr);
        if (lc) {
            CHECK(lc->light.type == fuhgl::render::LightType::Directional);
            CHECK(lc->light.intensity == 3.f);
            // Light::directional normalizes the direction internally
            const fuhgl::math::Vec3 d = lc->light.direction;
            CHECK(std::fabs(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) - 1.f) < 1e-5f);
            CHECK(d.y < 0.f && d.x < 0.f && d.z < 0.f);
            CHECK(lc->light.color.x == 1.f && std::fabs(lc->light.color.y - 0.9f) < 1e-6f);
        }
    }
    // root effects survive
    CHECK(scene2.root().effects.enabled(render::Effect::Taa));
    CHECK(scene2.root().effects.enabled(render::Effect::CsmShadows));
    CHECK(std::fabs(scene2.root().effects.params.exposure - 1.25f) < 1e-6f);
    CHECK(std::fabs(scene2.root().effects.params.shadowRadius - 33.f) < 1e-6f);
}

void testSerializeErrors() {
    std::string err;
    scene::Scene scene;
    buildDemoScene(scene);
    io::Json j;
    CHECK(scene::serializeScene(scene, j, &err));

    // not an object
    io::Json notObj;
    notObj.type = io::Json::Type::Array;
    scene::Scene s2;
    CHECK(!scene::deserializeScene(s2, notObj, &err));
    CHECK(!err.empty());

    // empty object is accepted (empty scene)
    io::Json emptyObj;
    emptyObj.type = io::Json::Type::Object;
    scene::Scene s3;
    CHECK(scene::deserializeScene(s3, emptyObj, &err));

    // non-empty target scene refused
    scene::Scene s4;
    buildDemoScene(s4);
    CHECK(!scene::deserializeScene(s4, j, &err));

    // unknown effect key rejected
    io::Json bad = j;
    bad.obj["children"].arr[0].obj["effects"].obj["list"].arr.clear();
    bad.obj["children"].arr[0].obj["effects"].obj["list"].arr.push_back([] {
        io::Json e;
        e.type = io::Json::Type::String;
        e.str = "does_not_exist";
        return e;
    }());
    scene::Scene s5;
    err.clear();
    CHECK(!scene::deserializeScene(s5, bad, &err));
    CHECK(err.find("unknown effect") != std::string::npos);
}

} // namespace

void runSceneSerializeTests() {
    testSerializeRoundtrip();
    testSerializeErrors();
}
