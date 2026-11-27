// Unit tests for the G40 custom-post-pass registry of the G3 DeferredRenderer and the
// Effect::ComicLook registry bits (no GL context needed: addPostPass() before create()
// only stores the descriptor, so the registry is pure CPU here).

#include "test_framework.hpp"
#include "tests.hpp"

#include <fuhglgraphics/render/deferred_renderer.hpp>
#include <fuhglgraphics/render/effects.hpp>

#include <string>

using namespace fuhglgraphics::render;

void runPostPassTests() {
    // --- Effect::ComicLook registry ---
    CHECK(kEffectCount == 31);
    CHECK(std::string(effectName(Effect::ComicLook)) == "comic look");
    Effect back = Effect::Taa;
    CHECK(effectFromName("comic look", back));
    CHECK(back == Effect::ComicLook);
    CHECK(effectSupported(Effect::ComicLook, 3, 3)); // plain GLSL 330 fullscreen pass

    // --- PostPass registry on a not-yet-created renderer (pure CPU) ---
    DeferredRenderer renderer;
    CHECK(renderer.postPassNames().empty());
    CHECK(renderer.postPass("tint") == nullptr);

    DeferredRenderer::PostPass tint;
    tint.name = "tint";
    tint.inputs = {"@chain"};
    tint.output = "tintOut";
    tint.fn = [](fuhgl::render::RenderGraph::PassContext&) {};
    CHECK(renderer.addPostPass(tint));
    CHECK(renderer.postPassNames().size() == 1);
    CHECK(renderer.postPassNames()[0] == "tint");
    CHECK(renderer.postPass("tint") != nullptr);
    CHECK(renderer.postPass("tint")->output == "tintOut");
    CHECK(renderer.postPass("tint")->inputs.size() == 1);
    CHECK(renderer.postPass("tint")->enabled);

    // duplicates (name or output resource) and invalid descriptors are rejected
    CHECK(!renderer.addPostPass(tint)); // same name + output
    DeferredRenderer::PostPass dupOut;
    dupOut.name = "other";
    dupOut.output = "tintOut";
    dupOut.fn = tint.fn;
    CHECK(!renderer.addPostPass(dupOut)); // output resource taken
    DeferredRenderer::PostPass invalid; // no name/output/fn
    CHECK(!renderer.addPostPass(invalid));
    CHECK(renderer.postPassNames().size() == 1);

    // a second pass chains after the first
    DeferredRenderer::PostPass second;
    second.name = "second";
    second.inputs = {"@chain", "gNormal"};
    second.output = "secondOut";
    second.fn = tint.fn;
    CHECK(renderer.addPostPass(second));
    CHECK(renderer.postPassNames().size() == 2);
    CHECK(renderer.postPassNames()[1] == "second");

    // runtime toggle (no rebuild while not created)
    CHECK(renderer.setPostPassEnabled("tint", false));
    CHECK(renderer.postPass("tint")->enabled == false);
    CHECK(renderer.setPostPassEnabled("tint", true));
    CHECK(!renderer.setPostPassEnabled("missing", true));

    // removal
    CHECK(renderer.removePostPass("tint"));
    CHECK(renderer.postPass("tint") == nullptr);
    CHECK(renderer.postPassNames().size() == 1);
    CHECK(!renderer.removePostPass("tint"));
    CHECK(renderer.removePostPass("second"));
    CHECK(renderer.postPassNames().empty());
}
