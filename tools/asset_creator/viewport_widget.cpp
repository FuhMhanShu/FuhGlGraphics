#include "viewport_widget.hpp"

#include <fuhglgraphics/scene/component.hpp>

#include <fuhgl/gl/framebuffer.hpp>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_0>
#include <QOpenGLVersionFunctionsFactory>
#include <QVBoxLayout>

namespace assetcreator {

using namespace fuhglgraphics;

ViewportWidget::ViewportWidget(AssetDocument* doc, QWidget* parent)
    : QWidget(parent), doc_(doc) {
    gl_ = new fuhglgraphics::editor::GlViewportWidget(this);
    gl_->setMsaaSamples(msaa_);
    gl_->setRequestedGlVersion(4, 3);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(gl_);

    gl_->setInitializeHandler([this](std::string& err) {
        renderer_ = std::make_unique<render::DeferredRenderer>();
        if (!renderer_->create(devW_ > 0 ? devW_ : 640, devH_ > 0 ? devH_ : 480, msaa_)) {
            err = "DeferredRenderer::create failed (GL >= 3.3 needed)";
            return false;
        }
        renderer_->setRenderScale(renderScale_);
        renderer_->enableCsmShadows(1024, 4);
        renderer_->skyTop = {0.10f, 0.22f, 0.50f};
        renderer_->skyBottom = {0.98f, 0.80f, 0.58f};
        glReady_ = true;
        emit glReadyChanged(true);
        return true;
    });
    gl_->setResizeHandler([this](int w, int h) {
        devW_ = w;
        devH_ = h;
        if (glReady_ && renderer_) renderer_->resize(w, h);
    });
    gl_->setPaintHandler([this]() {
        if (!glReady_ || !renderer_) return;
        applyOrbitToCamera();
        scene::Scene& sc = doc_->scene();
        sc.updateWorldTransforms();
        scene::FrameData frame;
        sc.fillFrame(frame, &camera_, devW_, devH_, true);
        frame.effects.applyTo(*renderer_);
        renderer_->render(frame.view, &frame.materialIds, &frame.castShadow, nullptr, nullptr,
                          &frame.decals, frame.water, &frame.skins);
        // blit the renderer output into the widget's framebuffer (QOpenGLWidget uses an
        // FBO as its default framebuffer, so FBO 0 is not the widget surface here).
        // NOTE: use Qt's QOpenGLFunctions + the plain GL enums — the fuhgl::gl::GL_*
        // tokens clash with the GL macros leaked in by the Qt headers.
        auto* glf = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_0>();
        const GLuint dstFbo = QOpenGLContext::currentContext()->defaultFramebufferObject();
        glf->glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer_->output().id());
        glf->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
        glf->glBlitFramebuffer(0, 0, renderer_->width(), renderer_->height(), 0, 0, devW_, devH_,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
    });
    connect(gl_, &fuhglgraphics::editor::GlViewportWidget::frameRendered, this,
            &ViewportWidget::frameRendered);
    connect(doc_, &AssetDocument::changed, this, [this]() { if (glReady_) gl_->requestRender(); });
}

void ViewportWidget::setRenderScale(float s) {
    renderScale_ = s;
    if (glReady_ && renderer_) renderer_->setRenderScale(s);
    requestRender();
}

void ViewportWidget::applyOrbitToCamera() {
    const float cp = std::cos(orbit_.pitch);
    camera_.position = orbit_.target +
                       fuhgl::math::Vec3{std::sin(orbit_.yaw) * cp, std::sin(orbit_.pitch),
                                         -std::cos(orbit_.yaw) * cp} *
                           orbit_.distance;
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(orbit_.target - camera_.position);
    camera_.yaw = std::atan2(d.x, -d.z);
    camera_.pitch = std::asin(d.y);
    camera_.fovY = fuhgl::math::radians(55.f);
}

} // namespace assetcreator
