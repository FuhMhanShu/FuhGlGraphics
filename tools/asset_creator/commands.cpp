#include "commands.hpp"

#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/node.hpp>
#include <fuhglgraphics/scene/serialize.hpp>

#include <fuhgl/render/light.hpp>
#include <fuhgl/render/mesh.hpp>

#include <QFileInfo>

#include <memory>

namespace assetcreator {

using namespace fuhglgraphics;

std::unique_ptr<QUndoCommand> makeAddNodeCommand(AssetDocument* doc, scene::Node* parent,
                                                 const QString& name) {
    auto created = std::make_shared<scene::Node*>(nullptr);
    return std::make_unique<LambdaCommand>(
        doc, QObject::tr("Add node %1").arg(name),
        [parent, name, created]() { *created = &parent->createChild(name.toStdString()); },
        [parent, created]() {
            if (*created) parent->removeChild(*created);
            *created = nullptr;
        });
}

std::unique_ptr<QUndoCommand> makeRemoveNodeCommand(AssetDocument* doc, scene::Node* node) {
    scene::Node* parent = node ? node->parent() : nullptr;
    auto taken = std::make_shared<std::unique_ptr<scene::Node>>();
    return std::make_unique<LambdaCommand>(
        doc, QObject::tr("Remove node %1").arg(QString::fromStdString(node ? node->name() : "")),
        [parent, node, taken]() {
            if (parent && node) *taken = parent->takeChild(node);
        },
        [parent, taken]() {
            if (parent && *taken) parent->addChild(std::move(*taken));
        });
}

std::unique_ptr<QUndoCommand> makeSetTrsCommand(AssetDocument* doc, scene::Node* node,
                                                const fuhgl::math::Vec3& translation,
                                                const fuhgl::math::Quat& rotation,
                                                const fuhgl::math::Vec3& scale) {
    const auto oldT = node->translation, oldR = node->rotation, oldS = node->scale;
    return std::make_unique<LambdaCommand>(
        doc, QObject::tr("Set TRS %1").arg(QString::fromStdString(node->name())),
        [node, translation, rotation, scale]() {
            node->translation = translation;
            node->rotation = rotation;
            node->scale = scale;
        },
        [node, oldT, oldR, oldS]() {
            node->translation = oldT;
            node->rotation = oldR;
            node->scale = oldS;
        });
}

std::unique_ptr<QUndoCommand> makeSetEffectCommand(AssetDocument* doc, scene::Node* node,
                                                   render::Effect effect, bool on) {
    const bool was = node->effects.enabled(effect);
    return std::make_unique<LambdaCommand>(
        doc, QObject::tr("Effect %1 %2")
                 .arg(QString::fromLatin1(render::effectName(effect)))
                 .arg(on ? "on" : "off"),
        [node, effect, on]() { if (on) node->effects.enable(effect); else node->effects.disable(effect); },
        [node, effect, was]() { if (was) node->effects.enable(effect); else node->effects.disable(effect); });
}

std::unique_ptr<QUndoCommand> makeSetEffectParamCommand(AssetDocument* doc, scene::Node* node,
                                                        const QString& name, const io::Json& value) {
    io::Json oldValue = scene::effectParamToJson(node->effects.params, name.toStdString());
    return std::make_unique<LambdaCommand>(
        doc, QObject::tr("Set %1").arg(name),
        [node, name, value]() {
            std::string err;
            scene::setEffectParam(node->effects.params, name.toStdString(), value, &err);
        },
        [node, name, oldValue]() {
            std::string err;
            scene::setEffectParam(node->effects.params, name.toStdString(), oldValue, &err);
        });
}

std::unique_ptr<QUndoCommand> makeAttachMeshCommand(AssetDocument* doc, scene::Node* parent,
                                                    const QString& name,
                                                    const fuhgl::render::MeshData& md,
                                                    const fuhgl::math::Vec3& translation) {
    auto created = std::make_shared<scene::Node*>(nullptr);
    auto redo = [doc, parent, name, md, translation, created]() {
        *created = &parent->createChild(name.toStdString());
        (*created)->translation = translation;
        doc->meshDataArena().push_back(std::make_unique<fuhgl::render::MeshData>(md));
        auto mesh = std::make_unique<fuhgl::render::Mesh>();
        mesh->setMeshData(md); // current GL context
        fuhgl::render::Mesh* meshPtr = mesh.get();
        doc->meshArena().push_back(std::move(mesh));
        scene::MeshEntry& e = (*created)->attach<scene::MeshComponent>().addMesh(meshPtr);
        e.meshData = doc->meshDataArena().back().get();
    };
    auto undo = [parent, created]() {
        if (*created) parent->removeChild(*created);
        *created = nullptr;
    };
    return std::make_unique<LambdaCommand>(doc, QObject::tr("Attach %1").arg(name), redo, undo);
}

std::unique_ptr<QUndoCommand> makeAddShapeCommand(AssetDocument* doc, scene::Node* parent,
                                                  const QString& shape, const QString& name) {
    auto created = std::make_shared<scene::Node*>(nullptr);
    io::Json spec;
    spec.type = io::Json::Type::Object;
    auto str = [](const std::string& s) { io::Json v; v.type = io::Json::Type::String; v.str = s; return v; };
    spec.obj["shape"] = str(shape.toStdString());
    spec.obj["color"] = io::Json{};
    spec.obj["color"].type = io::Json::Type::Array;
    for (double c : {0.8, 0.8, 0.85}) {
        io::Json n;
        n.type = io::Json::Type::Number;
        n.number = c;
        spec.obj["color"].arr.push_back(n);
    }
    if (shape == "box") {
        io::Json he;
        he.type = io::Json::Type::Array;
        for (double v : {0.5, 0.5, 0.5}) {
            io::Json n;
            n.type = io::Json::Type::Number;
            n.number = v;
            he.arr.push_back(n);
        }
        spec.obj["halfExtents"] = he;
    } else if (shape == "quad") {
        io::Json n;
        n.type = io::Json::Type::Number;
        n.number = 4.0;
        spec.obj["halfSize"] = n;
    } else {
        io::Json n;
        n.type = io::Json::Type::Number;
        n.number = 1.0;
        spec.obj["radius"] = n;
    }
    auto redo = [doc, parent, name, spec, created]() {
        *created = &parent->createChild(name.toStdString());
        auto mesh = scene::defaultShapeResolver(spec);
        if (!mesh && spec.at("shape").asString() == "sphere") {
            mesh = std::make_unique<fuhgl::render::Mesh>(
                fuhgl::render::makeSphere(1.f)->buildMesh(32, {0.8f, 0.8f, 0.85f},
                                                          fuhgl::render::SolidUv::Spherical));
        }
        if (mesh) {
            fuhgl::render::Mesh* p = mesh.get();
            doc->meshArena().push_back(std::move(mesh));
            scene::MeshEntry& e = (*created)->attach<scene::MeshComponent>().addMesh(p);
            e.albedo = {0.8f, 0.8f, 0.85f};
            if (spec.at("shape").asString() == "quad")
                e.localOffset = fuhgl::math::rotateX(-fuhgl::math::kPi * 0.5f);
        }
    };
    auto undo = [parent, created]() {
        if (*created) parent->removeChild(*created);
        *created = nullptr;
    };
    return std::make_unique<LambdaCommand>(doc, QObject::tr("Add %1 %2").arg(shape, name), redo, undo);
}

std::unique_ptr<QUndoCommand> makeAddLightCommand(AssetDocument* doc, scene::Node* parent,
                                                  const QString& type, const QString& name) {
    auto created = std::make_shared<scene::Node*>(nullptr);
    auto redo = [parent, name, type, created]() {
        *created = &parent->createChild(name.toStdString());
        scene::LightComponent& lc = (*created)->attach<scene::LightComponent>();
        if (type == "directional")
            lc.light = fuhgl::render::Light::directional({-0.6f, -0.8f, -0.4f}, {1.f, 0.9f, 0.75f}, 3.f);
        else
            lc.light = fuhgl::render::Light::point({0.f, 2.f, 0.f}, {1.f, 0.85f, 0.6f}, 25.f, 30.f);
    };
    auto undo = [parent, created]() {
        if (*created) parent->removeChild(*created);
        *created = nullptr;
    };
    (void)doc;
    return std::make_unique<LambdaCommand>(doc, QObject::tr("Add light %1").arg(name), redo, undo);
}

std::unique_ptr<QUndoCommand> makeLoadGltfCommand(AssetDocument* doc, scene::Node* parent,
                                                  const QString& path) {
    auto created = std::make_shared<scene::Node*>(nullptr);
    auto gpuBefore = std::make_shared<std::size_t>(0);
    auto redo = [doc, parent, path, created, gpuBefore]() {
        *gpuBefore = doc->gltfResources().size();
        QString err;
        // wrap the gltf in its own node for clean undo
        *created = &parent->createChild(QFileInfo(path).completeBaseName().toStdString());
        if (!doc->loadGltf(path, *created, &err)) {
            parent->removeChild(*created);
            *created = nullptr;
        }
    };
    auto undo = [doc, parent, created, gpuBefore]() {
        if (*created) parent->removeChild(*created);
        *created = nullptr;
        if (doc->gltfResources().size() > *gpuBefore) doc->gltfResources().resize(*gpuBefore);
    };
    return std::make_unique<LambdaCommand>(doc, QObject::tr("Load glTF %1").arg(path), redo, undo);
}

std::unique_ptr<QUndoCommand> makeAddWaterCommand(AssetDocument* doc, scene::Node* parent,
                                                  const QString& name, int resolution, float size) {
    auto created = std::make_shared<scene::Node*>(nullptr);
    auto redo = [doc, parent, name, resolution, size, created]() {
        *created = &parent->createChild(name.toStdString());
        scene::WaterComponent& wc = (*created)->attach<scene::WaterComponent>();
        if (wc.surface.create(resolution, size)) { // current GL context
            wc.surface.setDefaultWaves();
            doc->setActiveWater(&wc.surface);
            (*created)->effects.enable(render::Effect::Water);
        }
    };
    auto undo = [parent, created]() {
        if (*created) parent->removeChild(*created);
        *created = nullptr;
    };
    return std::make_unique<LambdaCommand>(doc, QObject::tr("Add water %1").arg(name), redo, undo);
}

} // namespace assetcreator
