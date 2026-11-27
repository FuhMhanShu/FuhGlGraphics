#include "viewport_widget.hpp"

#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/node.hpp>

#include <fuhgl/gl/framebuffer.hpp>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_0>
#include <QOpenGLVersionFunctionsFactory>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace assetcreator {

using namespace fuhglgraphics;

namespace {

void collectAnimNodes(scene::Node& n, std::vector<scene::Node*>& out) {
    if (n.find<scene::AnimationComponent>()) out.push_back(&n);
    for (const auto& ch : n.children()) collectAnimNodes(*ch, out);
}

void snapshotTrs(scene::Node& n, std::map<scene::Node*, ViewportWidget::Trs>& snap) {
    snap[&n] = ViewportWidget::Trs{n.translation, n.rotation, n.scale};
    for (const auto& ch : n.children()) snapshotTrs(*ch, snap);
}

} // namespace

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
        gizmo_.create(); // P5 gizmo part meshes (current GL context here)
        glReady_ = true;
        emit glReadyChanged(true);
        return true;
    });
    connect(gl_, &fuhglgraphics::editor::GlViewportWidget::glAboutToBeDestroyed, this, [this]() {
        if (gl_->context() && gl_->windowHandle()) {
            gl_->context()->makeCurrent(gl_->windowHandle());
            gizmo_.release();
        }
    });
    gl_->setResizeHandler([this](int w, int h) {
        devW_ = w;
        devH_ = h;
        if (glReady_ && renderer_) renderer_->resize(w, h);
    });
    gl_->setPaintHandler([this]() {
        if (!glReady_ || !renderer_) return;
        if (animPlaying_) {
            const float dt = animClock_.isValid() ? std::min(0.1f, animClock_.restart() / 1000.f)
                                                  : 0.016f;
            stepAnimation(dt);
        }
        applyOrbitToCamera();
        scene::Scene& sc = doc_->scene();
        sc.updateWorldTransforms();
        scene::FrameData frame;
        sc.fillFrame(frame, &camera_, devW_, devH_, true);
        if (gizmo_.created() && gizmo_.target()) { // P5 gizmo overlay items (own G-buffer depth)
            gizmo_.updateForTarget(camera_);
            gizmo_.appendToFrame(frame, camera_, devW_, devH_);
        }
        frame.effects.applyTo(*renderer_);
        renderer_->render(frame.view, &frame.materialIds, &frame.castShadow, nullptr, nullptr,
                          &frame.decals, frame.water, &frame.skins);
        // blit the renderer output into the widget's framebuffer (QOpenGLWidget uses an
        // FBO as its default framebuffer, so FBO 0 is not the widget surface here).
        // NOTE: use Qt's QOpenGLFunctions + the plain GL enums — the fuhgl::gl::GL_*
        // tokens clash with the GL macros leaked in by the Qt headers.
        auto* glf = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_0>(
            QOpenGLContext::currentContext());
        if (glf) {
            const GLuint dstFbo = QOpenGLContext::currentContext()->defaultFramebufferObject();
            glf->glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer_->output().id());
            glf->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
            glf->glBlitFramebuffer(0, 0, renderer_->width(), renderer_->height(), 0, 0, devW_, devH_,
                                   GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        if (animPlaying_) requestRender(); // continuous playback
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

void ViewportWidget::setAnimationPlaying(bool on) {
    if (on == animPlaying_) return;
    animPlaying_ = on;
    if (on) {
        animSnapshot_.clear();
        snapshotTrs(doc_->scene().root(), animSnapshot_);
        animClock_.start();
    } else {
        for (const auto& [n, trs] : animSnapshot_) {
            n->translation = trs.t;
            n->rotation = trs.r;
            n->scale = trs.s;
        }
        animSnapshot_.clear();
    }
    requestRender();
}

int ViewportWidget::animationClipCount() const {
    int count = 0;
    std::vector<scene::Node*> nodes;
    collectAnimNodes(const_cast<scene::Node&>(doc_->scene().root()), nodes);
    for (const scene::Node* n : nodes)
        count += static_cast<int>(n->find<scene::AnimationComponent>()->player.clips().size());
    return count;
}

void ViewportWidget::stepAnimation(float dt) {
    std::vector<scene::Node*> nodes;
    collectAnimNodes(doc_->scene().root(), nodes);
    if (nodes.empty()) {
        animPlaying_ = false;
        return;
    }
    for (scene::Node* n : nodes) {
        scene::AnimationComponent* ac = n->find<scene::AnimationComponent>();
        ac->player.update(dt);
        ac->player.applyTo(doc_->scene());
    }
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
