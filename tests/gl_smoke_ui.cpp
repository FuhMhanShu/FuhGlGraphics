// Headless G41 smoke test (label "gl_context"): exercises the 2D UI layer end to end —
// WorldSurface (UI rendered into an offscreen texture, pixel checks), the overlay post
// pass on the G3 deferred renderer (UI pixels land in the tonemapped output) and
// WorldSurface::pickUv (a scene::pick ray hit maps to the quad UV). Skips with exit
// code 77 when no GL >= 3.3 context is available.

#include "test_framework.hpp"

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_ui", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_ui: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_ui: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

// Builds a small widget tree (panel + label + button + checkbox + line edit + list).
void buildUi(fuhglgraphics::ui::UiRoot& ui, int w, int h) {
    using namespace fuhglgraphics::ui;
    ui.setSize(w, h);
    auto& panel = ui.root.add<Panel>();
    panel.rect = {8.f, 8.f, static_cast<float>(w) - 16.f, static_cast<float>(h) - 16.f};
    panel.title = "G41 UI";
    panel.setLayout(std::make_unique<VBoxLayout>());
    auto& label = panel.add<Label>();
    label.text = "Hello FuhGL UI";
    auto& btn = panel.add<Button>();
    btn.text = "Click me";
    auto& cb = panel.add<Checkbox>();
    cb.label = "enabled";
    auto& le = panel.add<LineEdit>();
    le.text = "edit me";
    auto& lb = panel.add<ListBox>();
    lb.items = {"one", "two", "three"};
}

std::vector<unsigned char> readTex(const fuhgl::Texture& tex, int w, int h) {
    std::vector<unsigned char> buf(static_cast<std::size_t>(w) * h * 4, 0);
    fuhgl::gl::activeTexture(fuhgl::gl::GL_TEXTURE0);
    fuhgl::gl::functions().glBindTexture(fuhgl::gl::GL_TEXTURE_2D, tex.id());
    fuhgl::gl::getTexImage(fuhgl::gl::GL_TEXTURE_2D, 0, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, buf.data());
    return buf;
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- 1) WorldSurface: render the UI offscreen and verify pixels ---
    const int SW = 256, SH = 192;
    ui::UiRoot ui;
    if (!ui.create()) {
        std::fprintf(stderr, "gl_smoke_ui: UiRoot::create failed\n");
        return 1;
    }
    buildUi(ui, SW, SH);
    ui::WorldSurface surf;
    if (!surf.create(SW, SH)) {
        std::fprintf(stderr, "gl_smoke_ui: WorldSurface::create failed\n");
        return 1;
    }
    // simulate a click on the checkbox (3rd row of the panel): name the widgets, then click
    ui.layout();
    std::function<ui::Widget*(ui::Container&, const std::string&)> findBy =
        [&findBy](ui::Container& c, const std::string& n) -> ui::Widget* {
        for (auto& ch : c.children) {
            if (ch->name == n) return ch.get();
            if (auto* cc = dynamic_cast<ui::Container*>(ch.get()))
                if (ui::Widget* w = findBy(*cc, n)) return w;
        }
        return nullptr;
    };
    // name the widgets for the test
    auto* panel = dynamic_cast<ui::Container*>(ui.root.children[0].get());
    panel->children[0]->name = "label";
    panel->children[1]->name = "btn";
    panel->children[2]->name = "cb";
    auto* cb = dynamic_cast<ui::Checkbox*>(findBy(ui.root, "cb"));
    if (!cb) {
        std::fprintf(stderr, "gl_smoke_ui: checkbox not found\n");
        return 1;
    }
    ui::Event down;
    down.type = ui::EventType::MouseButton;
    down.button = ui::MouseButton::Left;
    down.down = true;
    down.x = cb->abs.center().x;
    down.y = cb->abs.center().y;
    ui::Event up = down;
    up.down = false;
    ui.dispatch(down);
    ui.dispatch(up);
    if (!cb->checked.get()) {
        std::fprintf(stderr, "gl_smoke_ui: checkbox click did not toggle\n");
        return 1;
    }

    surf.render(ui);
    fuhgl::gl::flush();
    const std::vector<unsigned char> px = readTex(surf.texture(), SW, SH);
    // getTexImage returns the bottom row first: flip y so (x, y) is "y from the top"
    const auto at = [&](int x, int y) -> const unsigned char* {
        return px.data() + (static_cast<std::size_t>(SH - 1 - y) * SW + x) * 4;
    };
    // panel background in the title bar (theme.panelBg ~ (41,43,51), alpha 0.98 over cleared black)
    const unsigned char* bg = at(SW / 2, 14);
    std::fprintf(stderr, "gl_smoke_ui: panel bg=(%d,%d,%d,%d)\n", bg[0], bg[1], bg[2], bg[3]);
    if (bg[3] < 240 || bg[0] < 25 || bg[0] > 70) {
        std::fprintf(stderr, "gl_smoke_ui: FAIL panel bg pixel\n");
        return 1;
    }
    // text pixels inside the label row: some pixel much brighter than the bg
    bool foundText = false;
    for (int y = 30; y < 48 && !foundText; ++y)
        for (int x = 16; x < SW - 16 && !foundText; ++x) {
            const unsigned char* p = at(x, y);
            if (p[3] == 255 && p[0] > 180) foundText = true;
        }
    if (!foundText) {
        std::fprintf(stderr, "gl_smoke_ui: FAIL no text pixels in the label row\n");
        return 1;
    }

    // --- 2) overlay post pass on the G3 deferred renderer ---
    const int W = 128, H = 128;
    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_ui: DeferredRenderer::create failed\n");
        return 1;
    }
    ui::UiRoot hud;
    if (!hud.create()) {
        std::fprintf(stderr, "gl_smoke_ui: hud UiRoot create failed\n");
        return 1;
    }
    buildUi(hud, W, H);
    if (!renderer.addPostPass(ui::makeOverlayPass(hud))) {
        std::fprintf(stderr, "gl_smoke_ui: addPostPass(ui) failed\n");
        return 1;
    }
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 8.f, {0.35f, 0.4f, 0.38f});
    fuhgl::render::Material groundMat = fuhgl::render::materials::pbr({0.4f, 0.45f, 0.4f}, 0.f, 0.9f);
    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 2.2f, 6.f};
    cam.pitch = -0.22f;
    fuhgl::render::SceneView scene;
    scene.camera = &cam;
    scene.width = W;
    scene.height = H;
    scene.aspect = 1.f;
    scene.items.push_back({&ground, &groundMat, fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f)});
    scene.lights.push_back(fuhgl::render::Light::directional({-0.4f, -1.f, -0.3f}, {1.f, 0.9f, 0.75f}, 3.f));
    for (int i = 0; i < 4; ++i) renderer.render(scene);
    fuhgl::gl::flush();
    renderer.output().bind();
    std::vector<unsigned char> out(static_cast<std::size_t>(W) * H * 4, 0);
    fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, out.data());
    // the overlay panel covers (8,8)..(W-8,H-8) in screen space; readPixels is bottom-up:
    const auto oat = [&](int x, int yTop) -> const unsigned char* {
        return out.data() + (static_cast<std::size_t>(H - 1 - yTop) * W + x) * 4;
    };
    // the overlay panel covers (8,8)..(W-8,H-8): scan the region for panel-bg pixels
    // (theme.panelBg ~ r=41, opaque after the separate-alpha blend) and bright text pixels
    int panelPx = 0, textPx = 0;
    for (int y = 8; y < H - 8; ++y)
        for (int x = 8; x < W - 8; ++x) {
            const unsigned char* p = oat(x, y);
            if (p[3] == 255 && p[0] >= 30 && p[0] <= 60) ++panelPx;
            if (p[0] > 180) ++textPx;
        }
    std::fprintf(stderr, "gl_smoke_ui: overlay panelPx=%d textPx=%d\n", panelPx, textPx);
    if (panelPx < 2000 || textPx < 20) {
        std::fprintf(stderr, "gl_smoke_ui: FAIL overlay pass did not draw the panel/text\n");
        return 1;
    }

    // --- 3) WorldSurface::pickUv: center pick on a quad -> uv ~ (0.5, 0.5) ---
    fuhgl::render::Mesh screenMesh;
    screenMesh.create();
    const fuhgl::render::MeshData md = fuhgl::render::shapes::quadData(1.f, {1.f, 1.f, 1.f});
    screenMesh.setMeshData(md);
    scene::Scene sc;
    auto& node = sc.root().createChild("screen");
    auto& entry = node.attach<scene::MeshComponent>().addMesh(&screenMesh);
    entry.meshData = &md;
    fuhgl::FirstPersonCamera pickCam;
    pickCam.position = {0.f, 0.f, 6.f}; // yaw 0, pitch 0 -> looks down -Z at the XY quad
    // fov chosen so the frustum at the quad (dist 6) is 4 units wide, the quad (half 1) covers
    // ndc |x| <= 0.5: center -> u = 0.5, viewport x = 80 (ndc 0.25) -> u = 0.75, corner misses
    pickCam.fovY = 2.f * std::atan(1.f / 3.f);
    float u = 0.f, v = 0.f;
    if (!ui::WorldSurface::pickUv(sc, pickCam, 128, 128, 64.f, 64.f, &node, u, v)) {
        std::fprintf(stderr, "gl_smoke_ui: pickUv missed the quad center\n");
        return 1;
    }
    std::fprintf(stderr, "gl_smoke_ui: pickUv center -> (%f, %f)\n", u, v);
    CHECK_NEAR(u, 0.5, 0.03);
    CHECK_NEAR(v, 0.5, 0.03);
    float u2 = 0.f, v2 = 0.f;
    if (!ui::WorldSurface::pickUv(sc, pickCam, 128, 128, 80.f, 64.f, &node, u2, v2)) {
        std::fprintf(stderr, "gl_smoke_ui: pickUv missed the right quarter\n");
        return 1;
    }
    CHECK_NEAR(u2, 0.75, 0.03);
    // a pick near the frame corner is outside the quad -> must miss
    float u3 = 0.f, v3 = 0.f;
    CHECK(!ui::WorldSurface::pickUv(sc, pickCam, 128, 128, 2.f, 2.f, &node, u3, v3));

    std::fprintf(stderr, "gl_smoke_ui: OK\n");
    return fuhglgraphicstest::failures() > 0 ? 1 : 0;
}
