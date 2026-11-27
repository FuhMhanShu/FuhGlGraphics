#include "thumbnailer.hpp"

#include <fuhglgraphics/io/gltf.hpp>
#include <fuhglgraphics/render/deferred_renderer.hpp>
#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/node.hpp>
#include <fuhglgraphics/scene/scene.hpp>

#include <fuhgl/cam/first_person.hpp>
#include <fuhgl/math.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_0>
#include <QOpenGLVersionFunctionsFactory>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace assetcreator::thumbnailer {

using namespace fuhglgraphics;

QString cacheDirFor(const QString& projectDir) { return projectDir + QStringLiteral("/.cache/thumbs"); }

QString thumbnailPathFor(const QString& assetPath, const QString& projectDir) {
    const QFileInfo fi(assetPath);
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(fi.absoluteFilePath())
                            .arg(fi.size())
                            .arg(fi.lastModified().toMSecsSinceEpoch());
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return cacheDirFor(projectDir) + QStringLiteral("/") + QString::fromLatin1(hash) +
           QStringLiteral(".png");
}

bool ensureThumbnail(const QString& assetPath, const QString& projectDir, int size) {
    const QString out = thumbnailPathFor(assetPath, projectDir);
    if (QFileInfo::exists(out)) return true;

    io::GltfAsset asset;
    std::string lerr;
    if (!io::loadGltf(assetPath.toStdString(), asset, &lerr)) return false;
    scene::Scene sc;
    io::GltfGpuResources gpu;
    std::string ierr;
    if (!io::instantiateGltf(asset, sc, gpu, &sc.root(), &ierr)) return false;
    sc.updateWorldTransforms();

    // fit a 3/4 camera onto the asset AABB
    fuhgl::math::Vec3 mn{0.f, 0.f, 0.f}, mx{0.f, 0.f, 0.f};
    sc.root().worldAabb(mn, mx);
    const fuhgl::math::Vec3 center = (mn + mx) * 0.5f;
    const float radius = std::max(0.5f, fuhgl::math::length(mx - mn) * 0.5f);
    fuhgl::FirstPersonCamera cam;
    const fuhgl::math::Vec3 dir = fuhgl::math::normalize(fuhgl::math::Vec3{0.8f, -0.55f, 1.f});
    cam.position = center - dir * (radius * 2.4f + 1.f);
    const fuhgl::math::Vec3 d = fuhgl::math::normalize(center - cam.position);
    cam.yaw = std::atan2(d.x, -d.z);
    cam.pitch = std::asin(d.y);
    cam.fovY = fuhgl::math::radians(45.f);
    cam.nearPlane = std::max(0.01f, radius * 0.02f);
    cam.farPlane = radius * 20.f + 10.f;

    render::DeferredRenderer r;
    if (!r.create(size, size, 0)) return false; // needs a current GL context
    sc.root().effects.enable(render::Effect::Tonemap); // LDR output for the readback
    scene::FrameData frame;
    sc.fillFrame(frame, &cam, size, size, false);
    frame.effects.applyTo(r);
    r.render(frame.view, &frame.materialIds, &frame.castShadow, nullptr, nullptr, &frame.decals,
             frame.water, &frame.skins);

    // read back the tonemapped LDR output (GL rows are bottom-up -> flip into the QImage)
    auto* glf = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_0>(
        QOpenGLContext::currentContext());
    if (!glf) return false;
    std::vector<unsigned char> px(static_cast<std::size_t>(size) * size * 4);
    glf->glBindFramebuffer(GL_READ_FRAMEBUFFER, r.output().id());
    glf->glReadBuffer(GL_COLOR_ATTACHMENT0);
    glf->glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glf->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    QImage img(size, size, QImage::Format_RGBA8888);
    for (int y = 0; y < size; ++y)
        std::memcpy(img.scanLine(y), px.data() + static_cast<std::size_t>(size - 1 - y) * size * 4,
                    static_cast<std::size_t>(size) * 4);

    QDir().mkpath(cacheDirFor(projectDir));
    return img.save(out, "PNG");
}

int generateMissing(const QString& projectDir, int size) {
    int made = 0;
    QDirIterator it(projectDir, {QStringLiteral("*.gltf"), QStringLiteral("*.glb")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (ensureThumbnail(path, projectDir, size)) ++made;
    }
    return made;
}

} // namespace assetcreator::thumbnailer
