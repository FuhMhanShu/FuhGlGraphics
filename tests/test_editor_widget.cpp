// Unit test for editor::GlViewportWidget (WITH_QT only, label "unit"): exercises the
// Qt-object logic that needs no GL context — limiter/format config, dirty flag, handler
// wiring — on the offscreen platform. Actual GL rendering is covered by the gl_smoke
// tests through the core renderer; here only the widget's own state machine is verified.

#include "test_framework.hpp"

#include <fuhglgraphics/editor/gl_viewport_widget.hpp>

#include <QApplication>
#include <QSurfaceFormat>

#include <cstdio>
#include <string>

using fuhglgraphics::editor::GlViewportWidget;

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen"); // headless: no windowing system needed
    QApplication app(argc, argv);

    GlViewportWidget widget;
    widget.resize(320, 240);

    // defaults
    CHECK(widget.maxFps() == 0); // unlimited = render on demand only
    CHECK(widget.isVSyncEnabled());
    CHECK(widget.msaaSamples() == 0);
    CHECK(!widget.isDirty());
    CHECK(widget.lastFps() == 0.0);
    CHECK(widget.lastFrameMs() == 0.0);

    // frame-rate limiter incl. clamping
    widget.setMaxFps(30);
    CHECK(widget.maxFps() == 30);
    widget.setMaxFps(-5);
    CHECK(widget.maxFps() == 0);

    // surface-format config (applied to the widget's requested format before first show)
    widget.setVSyncEnabled(false);
    CHECK(!widget.isVSyncEnabled());
    CHECK(widget.format().swapInterval() == 0);
    widget.setVSyncEnabled(true);
    CHECK(widget.format().swapInterval() == 1);
    widget.setMsaaSamples(4);
    CHECK(widget.msaaSamples() == 4);
    CHECK(widget.format().samples() == 4);
    widget.setRequestedGlVersion(4, 3);
    CHECK(widget.format().majorVersion() == 4);
    CHECK(widget.format().minorVersion() == 3);
    CHECK(widget.format().profile() == QSurfaceFormat::CoreProfile);
    CHECK(widget.format().depthBufferSize() == 24);
    CHECK(widget.format().stencilBufferSize() == 8);

    // on-demand rendering: requestRender() marks dirty (no paint while not shown)
    widget.requestRender();
    CHECK(widget.isDirty());

    // handlers are stored; invocation happens in initializeGL/paintGL and needs a GL context
    bool initializeCalled = false;
    widget.setInitializeHandler(
        [&initializeCalled](std::string& /*errorMessage*/) { initializeCalled = true; return true; });
    widget.setResizeHandler([](int /*deviceWidth*/, int /*deviceHeight*/) {});
    widget.setPaintHandler([]() {});
    CHECK(!initializeCalled);

    if (fuhglgraphicstest::failures() == 0)
        std::printf("test_editor_widget: %d checks OK\n", fuhglgraphicstest::checks());
    return fuhglgraphicstest::failures() == 0 ? 0 : 1;
}
