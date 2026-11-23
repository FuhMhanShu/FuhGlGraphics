// Unit tests for the G19 scene serialization (no GL context needed): io::writeJson round-trips
// and a full scene::Scene <-> io::Json round-trip (nodes TRS, effects, instancing, mesh entries
// with a null-mesh resolver, lights, decals, animation clips) plus saveScene/loadScene files.

#include "test_framework.hpp"

#include <fuhglgraphics/io/json.hpp>
#include <fuhglgraphics/scene/serialize.hpp>

#include <cstdio>
#include <filesystem>

using namespace fuhglgraphics;

void runSerializeTests() {
    // --- writeJson / parseJson round-trip ---
    {
        io::Json j;
        j.type = io::Json::Type::Object;
        j.obj["name"] = io::Json{};
        j.obj["name"].type = io::Json::Type::String;
        j.obj["name"].str = "crate \"A\"\nline2\ttab äöü €";
        io::Json arr;
        arr.type = io::Json::Type::Array;
        for (double v : {0.0, 1.5, -2.25, 1e300, 0.1})
            arr.arr.push_back(io::Json{});
        arr.arr.clear();
        for (double v : {0.0, 1.5, -2.25, 1e300, 0.1}) {
            io::Json n;
            n.type = io::Json::Type::Number;
            n.number = v;
            arr.arr.push_back(n);
        }
        arr.arr.push_back(io::Json{}); // null
        io::Json b;
        b.type = io::Json::Type::Bool;
        b.boolean = true;
        arr.arr.push_back(b);
        j.obj["values"] = arr;
        j.obj["nested"] = io::Json{};
        j.obj["nested"].type = io::Json::Type::Object;
        j.obj["nested"].obj["emptyArr"] = io::Json{};
        j.obj["nested"].obj["emptyArr"].type = io::Json::Type::Array;
        j.obj["nested"].obj["emptyObj"] = io::Json{};
        j.obj["nested"].obj["emptyObj"].type = io::Json::Type::Object;

        const std::string s = io::writeJson(j, 2);
        io::Json back;
        CHECK(io::parseJson(s, back));
        CHECK(back.type == io::Json::Type::Object);
        CHECK(back.at("name").asString() == j.obj["name"].str);
        CHECK(back.at("values").size() == 7); // 5 numbers + null + bool
        CHECK_NEAR(back.at("values")[1].asNumber(0), 1.5, 1e-12);
        CHECK_NEAR(back.at("values")[4].asNumber(0), 0.1, 1e-15);
        CHECK(back.at("values")[5].isNull());
        CHECK(back.at("values")[6].asBool(false) == true);
        CHECK(back.at("values")[4].isNumber());
        CHECK(back.at("nested").at("emptyArr").isArray());
        CHECK(back.at("nested").at("emptyObj").isObject());
        // compact mode parses too
        io::Json back2;
        CHECK(io::parseJson(io::writeJson(j, 0), back2));
        CHECK(back2.at("name").asString() == j.obj["name"].str);
        // invalid JSON rejected
        io::Json bad;
        CHECK(!io::parseJson("{ \"a\": }", bad));
        CHECK(!io::parseJson("[1, 2", bad));
    }

    // --- scene round-trip (resolver returns nullptr meshes — structure only) ---
    {
        scene::Scene scene;
        scene.root().setName("root");
        scene.root().translation = {1.f, 2.f, 3.f};
        scene.root().effects.enable(render::Effect::Ssao);
        scene.root().effects.enable(render::Effect::Bloom);
        scene.root().effects.params.ssaoRadius = 2.5f;
        scene.root().effects.params.bloomThreshold = 3.5f;
        scene.root().instancing.enabled = true;
        scene.root().instancing.count = 12;
        scene.root().instancing.layout = scene::InstanceLayout::Random;
        scene.root().instancing.seed = 42;

        scene::Node& box = scene.root().createChild("boxNode");
        box.translation = {0.f, 1.f, 0.f};
        box.scale = {2.f, 2.f, 2.f};
        scene::MeshEntry& e = box.attach<scene::MeshComponent>().addMesh(nullptr);
        e.albedo = {0.8f, 0.2f, 0.1f};
        e.metallic = 0.7f;
        e.roughness = 0.25f;
        e.ao = 0.9f;
        e.materialId = 7;
        e.castShadow = false;

        scene.root().createChild("sun").attach<scene::LightComponent>().light =
            fuhgl::render::Light::directional({0.f, -1.f, 0.2f}, {1.f, 0.9f, 0.7f}, 3.f);

        scene::Node& dec = scene.root().createChild("mark");
        scene::DecalComponent& dcc = dec.attach<scene::DecalComponent>();
        dcc.decal.halfExtents = {0.5f, 0.1f, 0.5f};
        dcc.decal.materialIdMask = 0x2u;

        scene::Node& anim = scene.root().createChild("anim");
        anim::AnimationClip clip;
        clip.name = "spin";
        clip.duration = 1.f;
        anim::TrsChannel ch;
        ch.nodeName = "boxNode";
        ch.times = {0.f, 1.f};
        ch.rotations = {fuhgl::math::Quat{0, 0, 0, 1}, fuhgl::math::fromAxisAngle(1.5f, {0, 1, 0})};
        clip.channels.push_back(ch);
        anim.attach<scene::AnimationComponent>().player.addClip(clip);

        const io::Json j = scene::sceneToJson(scene);
        const std::string text = io::writeJson(j, 2);

        scene::Scene scene2;
        std::vector<std::unique_ptr<fuhgl::render::Mesh>> arena;
        scene::MeshResolver nullResolver = [](const io::Json&) { return nullptr; };
        std::string err;
        CHECK(scene::sceneFromJson(scene2, j, nullResolver, arena, &err));
        if (!err.empty()) std::fprintf(stderr, "sceneFromJson err: %s\n", err.c_str());

        // structure round-trips: re-serialize and compare the JSON values
        const io::Json j2 = scene::sceneToJson(scene2);
        CHECK(io::writeJson(j2, 0) == io::writeJson(j, 0));

        // spot-check fields
        CHECK(scene2.root().name() == "root");
        CHECK_NEAR(scene2.root().translation.y, 2.f, 1e-6);
        CHECK(scene2.root().effects.enabled(render::Effect::Ssao));
        CHECK(scene2.root().effects.enabled(render::Effect::Bloom));
        CHECK_NEAR(scene2.root().effects.params.ssaoRadius, 2.5, 1e-6);
        CHECK_NEAR(scene2.root().effects.params.bloomThreshold, 3.5, 1e-6);
        CHECK(scene2.root().instancing.enabled && scene2.root().instancing.count == 12);
        CHECK(scene2.root().instancing.layout == scene::InstanceLayout::Random);
        CHECK(scene2.root().instancing.seed == 42u);
        const scene::Node* box2 = nullptr;
        for (const auto& c : scene2.root().children())
            if (c->name() == "boxNode") box2 = c.get();
        CHECK(box2 != nullptr);
        CHECK_NEAR(box2->scale.x, 2.f, 1e-6);
        const scene::MeshComponent* mc = box2->find<scene::MeshComponent>();
        CHECK(mc != nullptr && mc->meshes.size() == 1);
        CHECK_NEAR(mc->meshes[0].albedo.x, 0.8, 1e-6);
        CHECK_NEAR(mc->meshes[0].roughness, 0.25, 1e-6);
        CHECK(mc->meshes[0].materialId == 7u);
        CHECK(!mc->meshes[0].castShadow);
        const scene::Node* sun2 = nullptr;
        for (const auto& c : scene2.root().children())
            if (c->name() == "sun") sun2 = c.get();
        CHECK(sun2 != nullptr);
        const scene::LightComponent* lc = sun2->find<scene::LightComponent>();
        CHECK(lc != nullptr);
        CHECK(lc->light.type == fuhgl::render::LightType::Directional);
        CHECK_NEAR(lc->light.intensity, 3.f, 1e-6);
        const scene::Node* dec2 = nullptr;
        for (const auto& c : scene2.root().children())
            if (c->name() == "mark") dec2 = c.get();
        CHECK(dec2 != nullptr);
        const scene::DecalComponent* dc = dec2->find<scene::DecalComponent>();
        CHECK(dc != nullptr);
        CHECK_NEAR(dc->decal.halfExtents.x, 0.5, 1e-6);
        CHECK(dc->decal.materialIdMask == 0x2u);
        const scene::Node* anim2 = nullptr;
        for (const auto& c : scene2.root().children())
            if (c->name() == "anim") anim2 = c.get();
        CHECK(anim2 != nullptr);
        const scene::AnimationComponent* ac = anim2->find<scene::AnimationComponent>();
        CHECK(ac != nullptr && ac->player.clip("spin") != nullptr);
        CHECK_NEAR(ac->player.clip("spin")->duration, 1.f, 1e-6);
        CHECK(ac->player.clip("spin")->channels.size() == 1);
        CHECK(ac->player.clip("spin")->channels[0].rotations.size() == 2);

        // --- save/load file round-trip ---
        const auto dir = std::filesystem::temp_directory_path() / "fgg-test-serialize";
        std::filesystem::create_directories(dir);
        const std::string path = (dir / "scene.json").string();
        CHECK(scene::saveScene(scene, path, &err));
        scene::Scene scene3;
        CHECK(scene::loadScene(scene3, path, nullResolver, arena, &err));
        CHECK(io::writeJson(scene::sceneToJson(scene3), 0) == io::writeJson(j, 0));
        // malformed input rejected
        CHECK(!scene::sceneFromJson(scene3, io::Json{}, nullResolver, arena, &err));
    }
}
