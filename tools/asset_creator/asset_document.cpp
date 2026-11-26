#include "asset_document.hpp"

#include <QFile>
#include <QFileInfo>

#include <fuhgl/render/mesh.hpp>

#include <functional>
#include <vector>

using namespace fuhglgraphics;

namespace {

/// "primitive:box[:hx,hy,hz]" / "primitive:quad[:halfSize]" -> parsed params.
struct PrimitiveRef {
    std::string kind;              ///< "box" / "quad"
    fuhgl::math::Vec3 param{0.5f}; ///< box half extents / quad half size
};

bool parsePrimitiveRef(const std::string& ref, PrimitiveRef& out) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : ref) {
        if (c == ':') { parts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    parts.push_back(cur);
    if (parts.size() < 2 || parts[0] != "primitive") return false;
    out.kind = parts[1];
    if (out.kind != "box" && out.kind != "quad") return false;
    if (parts.size() >= 5) {
        out.param = {std::stof(parts[2]), std::stof(parts[3]), std::stof(parts[4])};
    } else if (parts.size() == 3) {
        out.param = fuhgl::math::Vec3{std::stof(parts[2])};
    } else if (out.kind == "quad") {
        out.param = fuhgl::math::Vec3{out.param.x}; // quad: uniform half size
    }
    return true;
}

} // namespace

QString AssetDocument::displayName() const {
    return hasPath() ? QFileInfo(path_).fileName() : QStringLiteral("untitled");
}

void AssetDocument::newDocument() {
    // demo scene: ground quad + box + sun (meshes referenced by assetRef, created lazily)
    scene::Node& ground = scene_.root().createChild("ground");
    scene::MeshEntry& ge = ground.attach<scene::MeshComponent>().addMesh(nullptr);
    ge.assetRef = "primitive:quad:8";
    ge.albedo = {0.32f, 0.34f, 0.36f};
    ge.roughness = 0.95f;
    ge.castShadow = false;
    ge.materialId = 1;
    ge.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f); // quad XY -> XZ

    scene::Node& box = scene_.root().createChild("box");
    box.translation = {0.f, 1.f, 0.f};
    scene::MeshEntry& be = box.attach<scene::MeshComponent>().addMesh(nullptr);
    be.assetRef = "primitive:box";
    be.albedo = {0.83f, 0.66f, 0.24f};
    be.metallic = 0.2f;
    be.roughness = 0.4f;
    be.materialId = 2;

    scene::Node& sun = scene_.root().createChild("sun");
    sun.attach<scene::LightComponent>().light =
        fuhgl::render::Light::directional({-0.85f, -0.55f, -0.45f}, {1.0f, 0.82f, 0.58f}, 3.4f);

    scene_.root().effects.enable(render::Effect::Tonemap);
    scene_.root().effects.enable(render::Effect::CsmShadows);

    path_.clear();
    dirty_ = true;
    gpuDone_ = false; // meshes pending until a GL context is current
}

bool AssetDocument::open(const QString& path, std::string* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "cannot open file: " + path.toStdString();
        return false;
    }
    const std::string text = QString::fromUtf8(f.readAll()).toStdString();
    io::Json doc;
    if (!io::parseJson(text, doc, err)) return false;
    if (doc.at("format").asString() != "fuhglgraphics-asset") {
        if (err) *err = "not a fuhglgraphics-asset file (format field mismatch)";
        return false;
    }
    if (!scene::deserializeScene(scene_, doc.at("scene"), err)) return false;
    path_ = path;
    dirty_ = false;
    gpuDone_ = false;
    return true;
}

bool AssetDocument::save(const QString& path, std::string* err) {
    io::Json doc;
    doc.type = io::Json::Type::Object;
    doc.obj["format"] = [] { io::Json j; j.type = io::Json::Type::String; j.str = "fuhglgraphics-asset"; return j; }();
    doc.obj["version"] = [] { io::Json j; j.type = io::Json::Type::Number; j.number = 1; return j; }();
    doc.obj["name"] = [] { io::Json j; j.type = io::Json::Type::String; j.str = QFileInfo(path).baseName().toStdString(); return j; }();

    // collect referenced file assets (assetRef "file:<path>") for the assets list
    io::Json assets;
    assets.type = io::Json::Type::Array;
    std::function<void(const scene::Node&)> walk = [&](const scene::Node& n) {
        if (const auto* mc = n.find<scene::MeshComponent>())
            for (const scene::MeshEntry& e : mc->meshes)
                if (e.assetRef.rfind("file:", 0) == 0) {
                    io::Json a;
                    a.type = io::Json::Type::Object;
                    a.obj["path"] = [] { io::Json j; j.type = io::Json::Type::String; j.str = e.assetRef.substr(5); return j; }();
                    assets.arr.push_back(a);
                }
        for (const auto& c : n.children()) walk(*c);
    };
    walk(scene_.root());
    doc.obj["assets"] = assets;

    io::Json sceneJson;
    if (!scene::serializeScene(scene_, sceneJson, err)) return false;
    doc.obj["scene"] = sceneJson;

    std::string text;
    if (!io::writeJson(doc, text, true, err)) return false;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = "cannot write file: " + path.toStdString();
        return false;
    }
    f.write(text.data(), static_cast<qint64>(text.size()));
    path_ = path;
    dirty_ = false;
    return true;
}

bool AssetDocument::instantiateGpuResources(std::string* err) {
    if (gpuDone_) return true;
    std::function<void(scene::Node&)> walk = [&](scene::Node& n) {
        if (auto* mc = n.find<scene::MeshComponent>()) {
            for (scene::MeshEntry& e : mc->meshes) {
                if (e.mesh || e.assetRef.empty()) continue;
                auto it = meshes_.find(e.assetRef);
                if (it != meshes_.end()) {
                    e.mesh = it->second.get();
                    continue;
                }
                PrimitiveRef ref;
                if (!parsePrimitiveRef(e.assetRef, ref)) {
                    if (err) *err += "unsupported assetRef (P1 supports primitive:*): " + e.assetRef + "\n";
                    continue; // "file:" glTF refs land in P3
                }
                auto mesh = std::make_unique<fuhgl::render::Mesh>();
                if (ref.kind == "box")
                    fuhgl::render::shapes::box(*mesh, ref.param, e.albedo);
                else
                    fuhgl::render::shapes::quad(*mesh, ref.param.x, e.albedo);
                e.mesh = mesh.get();
                meshes_[e.assetRef] = std::move(mesh);
            }
        }
        for (const auto& c : n.children()) walk(*c);
    };
    walk(scene_.root());
    gpuDone_ = true;
    return err == nullptr || err->empty();
}
