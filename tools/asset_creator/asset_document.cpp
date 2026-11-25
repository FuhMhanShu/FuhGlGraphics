#include "asset_document.hpp"

#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/node.hpp>
#include <fuhglgraphics/scene/serialize.hpp>

#include <fuhgl/gl/info.hpp>
#include <fuhgl/gl/loader.hpp>
#include <fuhgl/render/mesh.hpp>

#include <QFileInfo>

#include <fstream>
#include <sstream>

namespace assetcreator {

using namespace fuhglgraphics;

namespace {

/// @brief Shape-spec resolver for the MCP scene_add_shape tool: box/quad via the built-in
/// scene::defaultShapeResolver, sphere via fuhgl::render::makeSphere. Needs a current GL
/// context (creates real GL meshes).
std::unique_ptr<fuhgl::render::Mesh> shapeResolver(const io::Json& spec) {
    if (spec.at("shape").asString() == "sphere") {
        const float radius = static_cast<float>(spec.at("radius").asNumber(1.0));
        const io::Json& c = spec.at("color");
        const fuhgl::math::Vec3 color{static_cast<float>(c.arr.size() > 0 ? c.arr[0].asNumber(0.8) : 0.8),
                                      static_cast<float>(c.arr.size() > 1 ? c.arr[1].asNumber(0.8) : 0.8),
                                      static_cast<float>(c.arr.size() > 2 ? c.arr[2].asNumber(0.85) : 0.85)};
        return std::make_unique<fuhgl::render::Mesh>(
            fuhgl::render::makeSphere(radius)->buildMesh(32, color, fuhgl::render::SolidUv::Spherical));
    }
    return scene::defaultShapeResolver(spec);
}

} // namespace

AssetDocument::AssetDocument(QObject* parent) : QObject(parent) {
    undoStack_.setUndoLimit(100);
}

void AssetDocument::setDirty(bool d) {
    dirty_ = d;
    emit changed();
}

QString AssetDocument::projectDir() const {
    if (projectPath_.isEmpty()) return QStringLiteral(".");
    return QFileInfo(projectPath_).absolutePath();
}

bool AssetDocument::newDocument() {
    scene_.root().clearChildren();
    meshArena_.clear();
    meshDataArena_.clear();
    gltfResources_.clear();
    activeWater_ = nullptr;
    undoStack_.clear();
    projectPath_.clear();
    projectName_ = QStringLiteral("untitled");
    dirty_ = false;
    buildDemoScene();
    emit documentReset();
    emit changed();
    return true;
}

void AssetDocument::buildDemoScene() {
    using namespace fuhglgraphics;
    scene::Node& root = scene_.root();
    root.effects.enable(render::Effect::Taa);
    root.effects.enable(render::Effect::Tonemap);
    root.effects.enable(render::Effect::Ibl);
    root.effects.enable(render::Effect::CsmShadows);
    root.effects.params.shadowCenter = {0.f, 2.f, 0.f};
    root.effects.params.shadowRadius = 30.f;

    // ground quad + hero box + sun light (GL meshes — call with a current context)
    auto groundMesh = std::make_unique<fuhgl::render::Mesh>();
    fuhgl::render::shapes::quad(*groundMesh, 20.f, {0.32f, 0.34f, 0.36f});
    fuhgl::render::Mesh* groundPtr = groundMesh.get();
    meshArena_.push_back(std::move(groundMesh));
    scene::Node& ground = root.createChild("ground");
    scene::MeshEntry& ge = ground.attach<scene::MeshComponent>().addMesh(groundPtr);
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);
    ge.castShadow = false;
    ge.materialId = 1;

    auto boxMesh = std::make_unique<fuhgl::render::Mesh>();
    fuhgl::render::shapes::box(*boxMesh, {0.6f, 0.6f, 0.6f}, {0.85f, 0.55f, 0.25f});
    fuhgl::render::Mesh* boxPtr = boxMesh.get();
    meshArena_.push_back(std::move(boxMesh));
    scene::Node& hero = root.createChild("hero");
    hero.translation = {0.f, 0.6f, 0.f};
    scene::MeshEntry& he = hero.attach<scene::MeshComponent>().addMesh(boxPtr);
    he.materialId = 2;

    scene::Node& sun = root.createChild("sun");
    sun.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.6f, -0.8f, -0.4f}, {1.f, 0.9f, 0.75f}, 3.f);
}

bool AssetDocument::open(const QString& path, QString* err) {
    std::ifstream f(path.toStdString());
    if (!f) {
        if (err) *err = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    io::Json j;
    std::string perr;
    if (!io::parseJson(ss.str(), j, &perr)) {
        if (err) *err = QStringLiteral("JSON parse error: %1").arg(QString::fromStdString(perr));
        return false;
    }
    const io::Json* sceneJson = j.find("scene");
    if (!sceneJson || !sceneJson->isObject()) {
        if (err) *err = QStringLiteral("not a .fasset file (missing \"scene\")");
        return false;
    }
    scene_.root().clearChildren();
    meshArena_.clear();
    meshDataArena_.clear();
    gltfResources_.clear();
    activeWater_ = nullptr;
    std::string serr;
    if (!scene::sceneFromJson(scene_, *sceneJson, &shapeResolver, meshArena_, &serr)) {
        if (err) *err = QStringLiteral("scene load failed: %1").arg(QString::fromStdString(serr));
        return false;
    }
    projectPath_ = path;
    projectName_ = QFileInfo(path).completeBaseName();
    dirty_ = false;
    undoStack_.clear();
    // asset cache for the project folder
    std::string cerr;
    assetCache_.open(projectDir().toStdString() + "/.cache", &cerr);
    emit documentReset();
    emit changed();
    return true;
}

bool AssetDocument::save(const QString& path, QString* err) {
    io::Json j;
    j.type = io::Json::Type::Object;
    auto str = [](const std::string& s) { io::Json v; v.type = io::Json::Type::String; v.str = s; return v; };
    auto num = [](double d) { io::Json v; v.type = io::Json::Type::Number; v.number = d; return v; };
    j.obj["format"] = str("fuhglgraphics-asset");
    j.obj["version"] = num(1);
    j.obj["name"] = str(projectName_.toStdString());
    j.obj["scene"] = scene::sceneToJson(scene_);
    std::string werr;
    if (!io::writeJsonFile(path.toStdString(), j, &werr)) {
        if (err) *err = QString::fromStdString(werr);
        return false;
    }
    projectPath_ = path;
    projectName_ = QFileInfo(path).completeBaseName();
    dirty_ = false;
    std::string cerr;
    assetCache_.open(projectDir().toStdString() + "/.cache", &cerr);
    emit changed();
    return true;
}

bool AssetDocument::loadGltf(const QString& path, scene::Node* parent, QString* err) {
    io::GltfAsset asset;
    std::string lerr;
    if (!io::loadGltf(path.toStdString(), asset, &lerr)) {
        if (err) *err = QStringLiteral("glTF load failed: %1").arg(QString::fromStdString(lerr));
        return false;
    }
    auto gpu = std::make_unique<io::GltfGpuResources>();
    io::GltfGpuResources* gpuPtr = gpu.get();
    std::string ierr;
    if (!io::instantiateGltf(asset, scene_, *gpuPtr, parent ? parent : &scene_.root(), &ierr)) {
        if (err) *err = QStringLiteral("glTF instantiate failed: %1").arg(QString::fromStdString(ierr));
        return false;
    }
    // cache the source file by content hash (G11)
    std::string cacheErr, cached;
    if (assetCache_.isOpen()) assetCache_.put(path.toStdString(), cached, &cacheErr);
    gltfResources_.push_back(std::move(gpu));
    setDirty(true);
    return true;
}

mcp::ToolHost AssetDocument::makeToolHost() {
    mcp::ToolHost host;
    host.scene = &scene_;
    host.effects = &scene_.root().effects;
    host.water = activeWater_;
    host.meshArena = &meshArena_;
    host.meshDataArena = &meshDataArena_;
    host.meshResolver = &shapeResolver;
    host.meshDataResolver = [](const fuhgl::render::MeshData& md) {
        auto m = std::make_unique<fuhgl::render::Mesh>();
        m->setMeshData(md); // needs a current GL context
        return m;
    };
    host.waterCreate = [](water::WaterSurface& s, int resolution, float size) {
        return s.create(resolution, size); // needs a current GL context
    };
    const auto v = fuhgl::gl::version();
    host.glMajor = v.major;
    host.glMinor = v.minor;
    return host;
}

} // namespace assetcreator
