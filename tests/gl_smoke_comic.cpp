// Headless G40 smoke test (label "gl_context"): renders a small scene (ground + two crates)
// through the G3 DeferredRenderer and verifies
//   1) Effect::ComicLook on vs off: the frames differ strongly and the comic frame has
//      near-black ink pixels (Sobel edges from the G-buffer),
//   2) a custom post pass registered via addPostPass() runs (frame changes) and
//      setPostPassEnabled(false) restores the exact no-custom-pass frame (the "@chain"
//      fallback reads "ldr" again),
//   3) no GL error occurs.
// Skips cleanly with exit code 77 when no GL >= 3.3 context is available.

#include <fuhglgraphics/graphics.hpp>

#include <fuhgl/fuhgl.hpp>
#include <fuhgl/gl/info.hpp>
#include <fuhgl/render.hpp>

#include "test_framework.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kSkip = 77;
constexpr int W = 256, H = 256;

bool headlessContext(fuhgl::Window& window) {
#ifndef _WIN32
    if (!std::getenv("FUHGL_BACKEND")) setenv("FUHGL_BACKEND", "surfaceless", 0);
#else
    if (!std::getenv("FUHGL_BACKEND")) _putenv_s("FUHGL_BACKEND", "surfaceless");
#endif
    if (!window.create("gl_smoke_comic", 64, 64)) {
        std::fprintf(stderr, "gl_smoke_comic: no GL context — SKIP\n");
        return false;
    }
    if (!fuhgl::gl::version().atLeast(3, 3)) {
        std::fprintf(stderr, "gl_smoke_comic: context below GL 3.3 — SKIP\n");
        return false;
    }
    return true;
}

constexpr const char* kTintVert = R"GLSL(#version 330 core
out vec2 vUV;
void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kTintFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc; // unit 0: bound by the renderer ("@chain")
out vec4 FragColor;
void main() {
    vec3 c = texture(uSrc, vUV).rgb;
    FragColor = vec4(c * vec3(1.5, 0.7, 0.7), 1.0); // strong red tint
}
)GLSL";

bool compileTint(fuhgl::render::Material& mat) {
    if (!mat.create()) return false;
    fuhgl::Shader vs(fuhgl::Shader::Type::Vertex), fs(fuhgl::Shader::Type::Fragment);
    if (!vs.compile(kTintVert) || !fs.compile(kTintFrag)) return false;
    mat.program().attach(vs);
    mat.program().attach(fs);
    return mat.program().link();
}

} // namespace

int main() {
    using namespace fuhglgraphics;
    fuhgl::Window window;
    if (!headlessContext(window)) return kSkip;

    // --- scene: ground + two crates ---
    scene::Scene scene;
    fuhgl::render::Mesh ground;
    fuhgl::render::shapes::quad(ground, 8.f, {0.5f, 0.5f, 0.52f});
    scene::Node& groundNode = scene.root().createChild("ground");
    scene::MeshEntry& ge = groundNode.attach<scene::MeshComponent>().addMesh(&ground);
    ge.albedo = {0.5f, 0.5f, 0.52f};
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);

    auto addCrate = [&](const char* name, const fuhgl::math::Vec3& pos, const fuhgl::math::Vec3& albedo) {
        fuhgl::render::Mesh crate;
        fuhgl::render::shapes::box(crate, {0.5f, 0.5f, 0.5f}, albedo);
        scene::Node& node = scene.root().createChild(name);
        node.translation = pos;
        scene::MeshEntry& e = node.attach<scene::MeshComponent>().addMesh(&crate);
        e.albedo = albedo;
        e.castShadow = false;
        e.materialId = 2;
    };
    addCrate("crate1", {1.6f, 0.5f, 0.5f}, {0.6f, 0.45f, 0.3f});
    addCrate("crate2", {-1.2f, 0.4f, -1.0f}, {0.25f, 0.5f, 0.65f});

    scene.root().createChild("sun").attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.3f, -1.f, -0.4f}, {1.f, 0.95f, 0.85f}, 2.f);
    scene.root().createChild("fill").attach<scene::LightComponent>().light =
        fuhgl::render::Light::point({-2.f, 3.f, 2.f}, {0.4f, 0.5f, 0.7f}, 6.f, 20.f);

    fuhgl::FirstPersonCamera cam;
    cam.position = {3.2f, 3.4f, 5.4f};
    cam.fovY = fuhgl::math::radians(55.f);
    const fuhgl::math::Vec3 target{0.f, 0.2f, 0.f};
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(target - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    scene.root().createChild("camera").attach<scene::CameraComponent>().camera = &cam;

    render::DeferredRenderer renderer;
    if (!renderer.create(W, H, 0)) {
        std::fprintf(stderr, "gl_smoke_comic: renderer create failed\n");
        return 1;
    }
    renderer.setEffectEnabled(render::Effect::Taa, false);
    scene.root().effects.enable(render::Effect::Tonemap);

    auto renderFrame = [&](bool comic) {
        scene::FrameData frame;
        scene.fillFrame(frame, nullptr, W, H);
        if (comic) frame.effects.enable(render::Effect::ComicLook);
        frame.effects.applyTo(renderer);
        for (int i = 0; i < 2; ++i)
            renderer.render(frame.view, &frame.materialIds, &frame.castShadow, &frame.particleSystems,
                            frame.lightProbes, &frame.decals);
        fuhgl::gl::flush();
        renderer.output().bind();
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4, 0);
        fuhgl::gl::readPixels(0, 0, W, H, fuhgl::gl::GL_RGBA, fuhgl::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    const std::vector<unsigned char> pxOff = renderFrame(false);
    const std::vector<unsigned char> pxComic = renderFrame(true);

    // --- custom pass: red tint chained after the tonemap ---
    fuhgl::render::Material tintMat;
    if (!compileTint(tintMat)) {
        std::fprintf(stderr, "gl_smoke_comic: tint material failed\n");
        return 1;
    }
    render::DeferredRenderer::PostPass tint;
    tint.name = "tint";
    tint.inputs = {"@chain"};
    tint.output = "tintOut";
    tint.fn = [&](fuhgl::render::RenderGraph::PassContext&) {
        renderer.drawFs(tintMat, [&](fuhgl::Program& p) { p.setUniform(p.uniformLocation("uSrc"), 0); });
    };
    if (!renderer.addPostPass(tint)) { // renderer is created -> rebuilds the graph
        std::fprintf(stderr, "gl_smoke_comic: addPostPass failed\n");
        return 1;
    }
    const std::vector<unsigned char> pxTint = renderFrame(false);
    if (!renderer.setPostPassEnabled("tint", false)) {
        std::fprintf(stderr, "gl_smoke_comic: setPostPassEnabled failed\n");
        return 1;
    }
    const std::vector<unsigned char> pxTintOff = renderFrame(false);

    if (fuhgl::gl::lastError() != fuhgl::gl::Error::NoError) {
        std::fprintf(stderr, "gl_smoke_comic: FAIL (GL error)\n");
        return 1;
    }

    auto diffCount = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        long diff = 0;
        for (std::size_t i = 0; i < a.size(); i += 4)
            if (std::labs(static_cast<long>(a[i]) - b[i]) > 10 ||
                std::labs(static_cast<long>(a[i + 1]) - b[i + 1]) > 10 ||
                std::labs(static_cast<long>(a[i + 2]) - b[i + 2]) > 10)
                ++diff;
        return diff;
    };
    auto inkCount = [](const std::vector<unsigned char>& px) {
        long ink = 0;
        for (std::size_t i = 0; i < px.size(); i += 4)
            if (px[i] + px[i + 1] + px[i + 2] < 60) ++ink;
        return ink;
    };

    const long total = static_cast<long>(W) * H;
    const long diffComic = diffCount(pxOff, pxComic);
    const long diffTint = diffCount(pxOff, pxTint);
    const long diffTintOff = diffCount(pxOff, pxTintOff);
    const long ink = inkCount(pxComic);
    std::fprintf(stderr, "gl_smoke_comic: diffComic=%ld diffTint=%ld diffTintOff=%ld ink=%ld / %ld px\n",
                 diffComic, diffTint, diffTintOff, ink, total);

    bool ok = true;
    if (diffComic < total / 10) { // comic look must change a substantial part of the frame
        std::fprintf(stderr, "gl_smoke_comic: FAIL (comic frame too similar: %ld)\n", diffComic);
        ok = false;
    }
    if (ink <= 0) { // posterized scene with box edges must produce near-black ink pixels
        std::fprintf(stderr, "gl_smoke_comic: FAIL (no ink pixels)\n");
        ok = false;
    }
    if (diffTint < total / 10) { // the custom pass must visibly tint the frame
        std::fprintf(stderr, "gl_smoke_comic: FAIL (custom pass had no effect: %ld)\n", diffTint);
        ok = false;
    }
    if (diffTintOff > total / 200) { // disabled custom pass: chain falls back to "ldr" -> identical
        std::fprintf(stderr, "gl_smoke_comic: FAIL (disabled pass changed the frame: %ld)\n", diffTintOff);
        ok = false;
    }
    if (!ok) return 1;
    std::fprintf(stderr, "gl_smoke_comic: OK\n");
    return 0;
}
