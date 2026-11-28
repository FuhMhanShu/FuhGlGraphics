// G41 demo: the 2D UI layer integrated into the G3 deferred renderer —
//  1) a menu rendered into a WorldSurface texture that is bound as the albedo of a 3D
//     "monitor" quad in the scene (world-space UI; clicks reach it via pickUv), and
//  2) a screen-space HUD drawn as a G40 custom post pass (makeOverlayPass) after tonemap.
// A slider in the world menu is bound to the renderer exposure (backend <-> GUI demo).
// Headless to PNG for docs/images/.
//
// Usage: fuhglgraphics_ui_screenshot --out shot.png [--width 1280] [--height 720]
//          [--frames 16] [--msaa 0]
//
// Exit code 2 when no headless GL context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/render.hpp>
#include <fuhgl/skybox.hpp>

#include "../screenshot/png_writer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out = "ui.png";
    int width = 1280, height = 720, frames = 16, msaa = 0;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) a.out = argv[++i];
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) a.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msaa") == 0 && i + 1 < argc) a.msaa = std::atoi(argv[++i]);
    }
    return a;
}

} // namespace

int main(int argc, char** argv) {
    using namespace fuhglgraphics;
    const Args args = parseArgs(argc, argv);
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif

    fuhgl::Window window;
    if (!window.create("fuhglgraphics_ui_screenshot", 64, 64)) {
        std::fprintf(stderr, "ui_screenshot: no GL context (headless without EGL?)\n");
        return 2;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "ui_screenshot: context below GL 3.3\n");
        return 2;
    }

    // --- stage: floor, monitor stand + screen quad carrying the WorldSurface texture ---
    scene::Scene scene;
    std::vector<fuhgl::render::Mesh> meshes;
    meshes.reserve(8); // nodes hold pointers into this vector — no reallocation allowed
    auto addBox = [&](const char* name, const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& half,
                      const fuhgl::math::Vec3& albedo) {
        meshes.emplace_back();
        fuhgl::render::Mesh& m = meshes.back();
        m.create();
        m.setMeshData(fuhgl::render::shapes::boxData(half, albedo));
        auto& node = scene.root().createChild(name);
        node.translation = pos;
        auto& e = node.attach<scene::MeshComponent>().addMesh(&m);
        e.albedo = albedo;
        e.roughness = 0.8f;
        e.castShadow = true;
    };
    addBox("floor", {0.f, -0.1f, 0.f}, {14.f, 0.1f, 14.f}, {0.55f, 0.53f, 0.48f});
    addBox("stand", {0.f, 0.45f, -2.6f}, {0.12f, 0.45f, 0.12f}, {0.12f, 0.12f, 0.14f});
    addBox("frame", {0.f, 1.6f, -2.62f}, {1.75f, 1.05f, 0.06f}, {0.08f, 0.08f, 0.10f});

    const fuhgl::math::Vec3 sunDir = fuhgl::math::normalize(fuhgl::math::Vec3{0.5f, -0.8f, 0.35f});
    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional(sunDir, {1.f, 0.85f, 0.62f}, 3.0f);
    scene.root().createChild("fill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({0.f, 3.f, 2.f}, {0.5f, 0.6f, 0.9f}, 25.f);

    // --- world-space UI: menu rendered into the surface texture of the screen quad ---
    const int SW = 512, SH = 300;
    ui::UiRoot menu;
    if (!menu.create()) {
        std::fprintf(stderr, "ui_screenshot: menu UiRoot create failed\n");
        return 1;
    }
    menu.setSize(SW, SH);
    auto& panel = menu.root.add<ui::Panel>();
    panel.rect = {12.f, 12.f, static_cast<float>(SW) - 24.f, static_cast<float>(SH) - 24.f};
    panel.title = "Main Menu";
    panel.setLayout(std::make_unique<ui::VBoxLayout>());
    auto& title = panel.add<ui::Label>();
    title.text = "G41 2D UI on a 3D screen";
    auto& cb = panel.add<ui::Checkbox>();
    cb.name = "cb";
    cb.label = "Fullscreen";
    auto& sl = panel.add<ui::Slider>();
    sl.name = "exposure";
    sl.value = 0.55f;
    auto& slLabel = panel.add<ui::Label>();
    slLabel.text = "Exposure (bound to the renderer)";
    auto& le = panel.add<ui::LineEdit>();
    le.name = "player";
    le.text = "Player 1";
    auto& lb = panel.add<ui::ListBox>();
    lb.name = "levels";
    lb.items = {"level_01", "level_02", "level_03"};
    lb.sizeHint.prefH = 3 * lb.itemHeight + 4;

    // backend <-> GUI binding: the slider drives the renderer exposure
    sl.value.onChanged().connect([&] { /* applied per frame below */ });
    ui::Property<float> exposure{1.0f};
    ui::bindTwoWay(exposure, sl.value);
    exposure.set(0.55f);

    // show some interaction state in the shot: toggle the checkbox, select a list entry
    menu.layout();
    std::function<ui::Widget*(ui::Container&, const std::string&)> findBy =
        [&findBy](ui::Container& c, const std::string& n) -> ui::Widget* {
        for (auto& ch : c.children) {
            if (ch->name == n) return ch.get();
            if (auto* cc = dynamic_cast<ui::Container*>(ch.get()))
                if (ui::Widget* w = findBy(*cc, n)) return w;
        }
        return nullptr;
    };
    if (auto* cbw = dynamic_cast<ui::Checkbox*>(findBy(menu.root, "cb"))) {
        ui::Event down;
        down.type = ui::EventType::MouseButton;
        down.button = ui::MouseButton::Left;
        down.down = true;
        down.x = cbw->abs.center().x;
        down.y = cbw->abs.center().y;
        ui::Event up = down;
        up.down = false;
        menu.dispatch(down);
        menu.dispatch(up);
    }
    if (auto* lbw = dynamic_cast<ui::ListBox*>(findBy(menu.root, "levels")))
        lbw->selected.set(1);

    ui::WorldSurface surface;
    if (!surface.create(SW, SH)) {
        std::fprintf(stderr, "ui_screenshot: WorldSurface create failed\n");
        return 1;
    }

    meshes.emplace_back();
    fuhgl::render::Mesh& screenMesh = meshes.back();
    screenMesh.create();
    const fuhgl::render::MeshData screenData = fuhgl::render::shapes::quadData(1.f, {1.f, 1.f, 1.f});
    screenMesh.setMeshData(screenData);
    auto& screenNode = scene.root().createChild("screen");
    screenNode.translation = {0.f, 1.6f, -2.5f};
    screenNode.scale = {1.6f, 1.0f, 1.f};
    auto& se = screenNode.attach<scene::MeshComponent>().addMesh(&screenMesh);
    se.albedoTexture = &surface.texture(); // the UI texture on the 3D screen model
    se.meshData = &screenData;               // enables WorldSurface::pickUv on this node
    se.roughness = 0.15f;
    se.emissive = {0.10f, 0.10f, 0.12f}; // slight self-glow so the screen reads as "on"
    se.castShadow = false;

    fuhgl::FirstPersonCamera cam;
    cam.position = {0.f, 1.7f, 3.2f};
    cam.fovY = fuhgl::math::radians(50.f);
    const fuhgl::math::Vec3 target{0.f, 1.5f, -2.5f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    scene.root().effects.enable(render::Effect::Taa);
    scene.root().effects.enable(render::Effect::Tonemap);
    scene.root().effects.enable(render::Effect::CsmShadows);
    scene.root().effects.params.shadowCenter = {0.f, 1.f, -2.f};
    scene.root().effects.params.shadowRadius = 10.f;

    const fuhgl::math::Vec3 skyTop{0.16f, 0.32f, 0.66f}, skyHorizon{0.92f, 0.84f, 0.66f};
    fuhgl::Skybox skybox;
    if (!skybox.create()) std::fprintf(stderr, "ui_screenshot: skybox create failed\n");
    skybox.makeGradient(skyTop, skyHorizon, 256);

    render::DeferredRenderer renderer;
    if (!renderer.create(args.width, args.height, args.msaa)) {
        std::fprintf(stderr, "ui_screenshot: renderer create failed\n");
        return 1;
    }
    renderer.enableCsmShadows(2048, 4);
    renderer.setShadowSceneBounds({0.f, 1.f, -2.f}, 10.f);
    if (skybox.texture().id() != 0) renderer.setSkyCubemap(&skybox.texture());
    renderer.skyTop = skyTop;
    renderer.skyBottom = skyHorizon;

    // --- screen-space HUD overlay as a G40 custom post pass (after tonemap) ---
    ui::UiRoot hud;
    if (!hud.create()) {
        std::fprintf(stderr, "ui_screenshot: hud UiRoot create failed\n");
        return 1;
    }
    hud.setSize(args.width, args.height);
    auto& hudPanel = hud.root.add<ui::Panel>();
    hudPanel.rect = {10.f, 10.f, 300.f, 64.f};
    hudPanel.title = "G41 overlay";
    hudPanel.setLayout(std::make_unique<ui::VBoxLayout>());
    auto& hudLabel = hudPanel.add<ui::Label>();
    hudLabel.text = "screen-space post-pass UI";
    auto& bar = hudPanel.add<ui::ProgressBar>();
    bar.value = 0.65f;
    if (!renderer.addPostPass(ui::makeOverlayPass(hud))) {
        std::fprintf(stderr, "ui_screenshot: addPostPass(ui) failed\n");
        return 1;
    }

    for (int i = 0; i < args.frames; ++i) {
        surface.render(menu); // world-space UI into the screen texture
        renderer.exposure = 0.25f + exposure.get() * 2.25f; // menu slider -> backend
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, args.width, args.height);
        frame.effects.applyTo(renderer);
        frame.view.fogColor = skyHorizon;
        frame.view.fogDensity = 0.0012f;
        renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                        frame.lightProbes, &frame.decals);
        hud.update(1.f / 60.f);
    }
    fuhgl::gl::flush();

    renderer.output().bind();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(args.width) * args.height * 4, 0);
    fuhgl::gl::readPixels(0, 0, args.width, args.height, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE,
                          pixels.data());
    if (!fuhglgraphics::screenshot::writePng(args.out.c_str(), pixels.data(), args.width, args.height)) {
        std::fprintf(stderr, "ui_screenshot: failed to write %s\n", args.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "ui_screenshot: wrote %s (%dx%d, %d frames, GL %d.%d %s)\n", args.out.c_str(),
                 args.width, args.height, args.frames, fuhgl::gl::version().major, fuhgl::gl::version().minor,
                 fuhgl::gl::queryDeviceInfo().renderer.c_str());
    return 0;
}
