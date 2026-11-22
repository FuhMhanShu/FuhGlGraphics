// Unit tests for the G6 animation module (no GL context needed): channel sampling
// (lerp/slerp, clamping), AnimationPlayer loop modes (Once/Loop/PingPong), applying poses
// to named nodes, and CPU skinning (identity palette = bind pose, translated joint moves
// weighted vertices).

#include "test_framework.hpp"

#include <fuhglgraphics/anim/animation.hpp>
#include <fuhglgraphics/scene/node.hpp>

#include <cmath>
#include <map>

using namespace fuhglgraphics;

namespace {

anim::AnimationClip makeSpinClip() {
    anim::AnimationClip clip;
    clip.name = "spin";
    clip.duration = 1.f;
    anim::TrsChannel ch;
    ch.nodeName = "arm";
    ch.times = {0.f, 1.f};
    ch.rotations = {fuhgl::math::Quat{0, 0, 0, 1}, fuhgl::math::fromAxisAngle(fuhgl::math::kPi / 2.f, {0, 0, 1})};
    clip.channels.push_back(ch);
    return clip;
}

void testSampleChannel() {
    anim::TrsChannel ch;
    ch.nodeName = "n";
    ch.times = {0.f, 2.f};
    ch.translations = {{0, 0, 0}, {4, 0, 0}};
    ch.rotations = {fuhgl::math::Quat{0, 0, 0, 1}, fuhgl::math::fromAxisAngle(fuhgl::math::kPi, {0, 1, 0})};
    ch.scales = {{1, 1, 1}, {3, 3, 3}};
    fuhgl::math::Vec3 t;
    fuhgl::math::Quat r;
    fuhgl::math::Vec3 s;
    anim::sampleChannel(ch, -1.f, t, r, s); // clamped at start
    CHECK_NEAR(t.x, 0.f, 1e-6);
    CHECK_NEAR(r.w, 1.f, 1e-6);
    anim::sampleChannel(ch, 1.f, t, r, s); // halfway: lerp t=2, slerp 90 deg around Y
    CHECK_NEAR(t.x, 2.f, 1e-5);
    CHECK_NEAR(s.x, 2.f, 1e-5);
    CHECK_NEAR(std::fabs(r.y), std::sin(fuhgl::math::kPi / 4.f), 1e-4); // half of 180 deg around Y (sign ambiguous)
    CHECK_NEAR(r.w, std::cos(fuhgl::math::kPi / 4.f), 1e-4);
    anim::sampleChannel(ch, 5.f, t, r, s); // clamped at end
    CHECK_NEAR(t.x, 4.f, 1e-6);
    CHECK_NEAR(s.x, 3.f, 1e-6);
}

void testPlayerLoopModes() {
    anim::AnimationPlayer p;
    p.addClip(makeSpinClip());
    CHECK(p.clip("spin") != nullptr);
    CHECK(p.clip("nope") == nullptr);
    CHECK(!p.play("nope"));
    CHECK(p.play("spin", anim::LoopMode::Once));
    p.update(0.5f);
    CHECK_NEAR(p.time(), 0.5f, 1e-6);
    p.update(0.7f); // 1.2 > duration 1 -> clamped, stopped
    CHECK_NEAR(p.time(), 1.f, 1e-6);
    CHECK(!p.isPlaying());

    CHECK(p.play("spin", anim::LoopMode::Loop));
    p.update(1.25f);
    CHECK_NEAR(p.time(), 0.25f, 1e-5); // wrapped
    CHECK(p.isPlaying());

    CHECK(p.play("spin", anim::LoopMode::PingPong));
    p.update(1.5f); // past the end -> mirrored, direction reversed
    CHECK_NEAR(p.time(), 0.5f, 1e-5);
    CHECK(p.speed() < 0.f);
    p.update(0.75f); // now going backward past 0 -> mirrored again
    CHECK_NEAR(p.time(), 0.25f, 1e-5);
    CHECK(p.speed() > 0.f);
}

void testPlayerApplyToNodes() {
    scene::Node root("root");
    scene::Node& arm = root.createChild("arm");
    anim::AnimationPlayer p;
    p.addClip(makeSpinClip());
    CHECK(p.play("spin", anim::LoopMode::Once));
    p.update(1.f); // full duration -> 90 deg around Z
    std::map<std::string, scene::Node*> nodes{{"arm", &arm}};
    p.applyTo(nodes);
    const fuhgl::math::Quat& r = arm.rotation;
    CHECK_NEAR(r.z, std::sin(fuhgl::math::kPi / 4.f), 1e-4);
    CHECK_NEAR(r.w, std::cos(fuhgl::math::kPi / 4.f), 1e-4);
}

void testSkinMeshCpu() {
    // one-vertex "mesh": pos (1,0,0), normal (0,1,0), color white, uv 0 — 11 floats
    fuhgl::render::MeshData base;
    base.vertices = {1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0};
    base.indices = {0};
    anim::SkinWeights skin;
    skin.joints = {1, 0, 0, 0};
    skin.weights = {1.f, 0.f, 0.f, 0.f};
    // identity palette -> bind pose unchanged
    std::vector<fuhgl::math::Mat4> palette(2);
    fuhgl::render::MeshData out;
    anim::skinMeshCpu(base, skin, palette, out);
    CHECK_NEAR(out.vertices[0], 1.f, 1e-6);
    // joint 1 translated by (0, 5, 0) -> vertex moves up
    palette[1] = fuhgl::math::translate({0.f, 5.f, 0.f});
    anim::skinMeshCpu(base, skin, palette, out);
    CHECK_NEAR(out.vertices[0], 1.f, 1e-5);
    CHECK_NEAR(out.vertices[1], 5.f, 1e-5);
    CHECK_NEAR(out.vertices[2], 0.f, 1e-5);
    // normal stays unit length
    const float nl = std::sqrt(out.vertices[3] * out.vertices[3] + out.vertices[4] * out.vertices[4] +
                               out.vertices[5] * out.vertices[5]);
    CHECK_NEAR(nl, 1.f, 1e-4);
}

} // namespace

void runAnimTests() {
    testSampleChannel();
    testPlayerLoopModes();
    testPlayerApplyToNodes();
    testSkinMeshCpu();
}
