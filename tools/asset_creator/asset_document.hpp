#pragma once

/// @file asset_document.hpp
/// @brief G12 asset creator — Model (MVC): owns the scene::Scene, the GL mesh arenas, the
/// QUndoStack, the dirty flag, the .fasset project file (scene::serialize + io::writeJson)
/// and the G11 asset cache; binds the MCP ToolHost (scene + render settings + G13 water +
/// GL resolvers) for the chat controller / built-in MCP server.

#include <fuhglgraphics/io/asset_cache.hpp>
#include <fuhglgraphics/io/gltf.hpp>
#include <fuhglgraphics/mcp/tool_catalog.hpp>
#include <fuhglgraphics/scene/scene.hpp>

#include <QObject>
#include <QUndoStack>
#include <QString>

#include <memory>
#include <vector>

namespace fuhglgraphics::water {
class WaterSurface;
}

namespace assetcreator {

class AssetDocument : public QObject {
    Q_OBJECT
public:
    explicit AssetDocument(QObject* parent = nullptr);

    fuhglgraphics::scene::Scene& scene() { return scene_; }
    const fuhglgraphics::scene::Scene& scene() const { return scene_; }
    QUndoStack& undoStack() { return undoStack_; }

    bool isDirty() const { return dirty_; }
    void setDirty(bool d);

    QString projectPath() const { return projectPath_; }
    QString projectName() const { return projectName_; }
    QString projectDir() const;

    bool newDocument();                          ///< demo scene, untitled
    bool open(const QString& path, QString* err); ///< .fasset (needs a current GL context for meshes)
    bool save(const QString& path, QString* err); ///< writes .fasset JSON
    bool save(QString* err) { return projectPath_.isEmpty() ? false : save(projectPath_, err); }

    /// @brief Loads a .gltf/.glb into `parent` (needs a current GL context). The GPU resources
    /// stay owned by the document.
    bool loadGltf(const QString& path, fuhglgraphics::scene::Node* parent, QString* err);

    /// @brief MCP tool host bound to this document (call with a current GL context for
    /// mesh attach / water create / glTF; render + scene + water-param tools are GL-free).
    fuhglgraphics::mcp::ToolHost makeToolHost();

    fuhglgraphics::water::WaterSurface* activeWater() const { return activeWater_; }
    void setActiveWater(fuhglgraphics::water::WaterSurface* w) { activeWater_ = w; }

    fuhglgraphics::io::AssetCache& assetCache() { return assetCache_; }

    /// @brief Small demo scene for a new document (needs a current GL context).
    void buildDemoScene();

    /// @brief Mesh arenas (GL meshes + CPU MeshData for picking) — public for commands.
    std::vector<std::unique_ptr<fuhgl::render::Mesh>>& meshArena() { return meshArena_; }
    std::vector<std::unique_ptr<fuhgl::render::MeshData>>& meshDataArena() { return meshDataArena_; }
    std::vector<std::unique_ptr<fuhglgraphics::io::GltfGpuResources>>& gltfResources() { return gltfResources_; }

signals:
    void changed();        ///< model edited — panels refresh, viewport repaints
    void documentReset();  ///< new/open — full refresh

private:
    fuhglgraphics::scene::Scene scene_;
    std::vector<std::unique_ptr<fuhgl::render::Mesh>> meshArena_;
    std::vector<std::unique_ptr<fuhgl::render::MeshData>> meshDataArena_;
    std::vector<std::unique_ptr<fuhglgraphics::io::GltfGpuResources>> gltfResources_;
    QUndoStack undoStack_;
    bool dirty_ = false;
    QString projectPath_;
    QString projectName_ = QStringLiteral("untitled");
    fuhglgraphics::io::AssetCache assetCache_;
    fuhglgraphics::water::WaterSurface* activeWater_ = nullptr; ///< non-owning (owned by a WaterComponent)
};

} // namespace assetcreator
