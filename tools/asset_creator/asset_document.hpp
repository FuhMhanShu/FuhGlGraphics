#pragma once

/// @file asset_document.hpp
/// @brief G12 asset creator — model (MVC): one `.fasset` project document.
/// Owns the scene::Scene, the QUndoStack, the GL mesh cache and the dirty flag.
/// P1 scope: new/open/save of .fasset (scene::serialize + io::writeJson) and lazy
/// GL mesh creation from the stored asset references ("primitive:box" etc.).

#include <fuhglgraphics/graphics.hpp>

#include <QUndoStack>
#include <QString>

#include <map>
#include <memory>
#include <string>

class AssetDocument {
public:
    AssetDocument() = default;

    /// @brief Fills the (fresh) document with a small demo scene: ground quad, box, sun.
    /// Meshes are only referenced (assetRef) — GL objects are created lazily.
    void newDocument();

    /// @brief Loads a .fasset project. The document must be fresh (default-constructed).
    /// Mesh entries come back with mesh = nullptr; call instantiateGpuResources() once
    /// a GL context is current.
    bool open(const QString& path, std::string* err = nullptr);

    /// @brief Writes the .fasset project (format "fuhglgraphics-asset", version 1).
    bool save(const QString& path, std::string* err = nullptr);
    bool save(std::string* err = nullptr) { return hasPath() ? save(path_, err) : false; }

    bool hasPath() const { return !path_.isEmpty(); }
    const QString& path() const { return path_; }
    QString displayName() const;

    bool dirty() const { return dirty_; }
    void setDirty(bool d) { dirty_ = d; }

    fuhglgraphics::scene::Scene& scene() { return scene_; }
    QUndoStack& undoStack() { return undoStack_; }

    /// @brief Creates the GL meshes for all mesh entries that carry an assetRef
    /// ("primitive:box[:hx,hy,hz]", "primitive:quad[:halfSize]"). Needs a current GL
    /// context. Idempotent — safe to call every frame until gpuResourcesPending() is false.
    bool instantiateGpuResources(std::string* err = nullptr);
    bool gpuResourcesPending() const { return !gpuDone_; }

private:
    fuhglgraphics::scene::Scene scene_;
    QUndoStack undoStack_;
    QString path_;
    bool dirty_ = false;
    bool gpuDone_ = false;
    std::map<std::string, std::unique_ptr<fuhgl::render::Mesh>> meshes_; ///< assetRef -> GL mesh
};
