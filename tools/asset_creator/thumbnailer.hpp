#pragma once

/// @file thumbnailer.hpp
/// @brief G12 asset creator P5 — glTF thumbnails for the asset browser: renders a small
/// offscreen preview of a .gltf/.glb with its own DeferredRenderer and caches it as PNG in
/// <project>/.cache/thumbs/<key>.png (inside the G11 asset-cache folder of the project).
/// ensureThumbnail()/generateMissing() need a current GL context.

#include <QString>

namespace assetcreator::thumbnailer {

/// @brief Thumbnail cache dir of the project (<projectDir>/.cache/thumbs).
QString cacheDirFor(const QString& projectDir);

/// @brief Cache file path of `assetPath` inside the project's thumbnail cache. Keyed by
/// path + size + mtime (cheap; avoids re-reading the file on every icon request).
QString thumbnailPathFor(const QString& assetPath, const QString& projectDir);

/// @brief Renders `assetPath` into its cached thumbnail when missing. Needs a current GL
/// context. Returns true when a thumbnail is available afterwards.
bool ensureThumbnail(const QString& assetPath, const QString& projectDir, int size = 128);

/// @brief Generates all missing thumbnails for *.gltf/*.glb below `projectDir` (recursive).
/// Needs a current GL context. Returns the number of newly rendered thumbnails.
int generateMissing(const QString& projectDir, int size = 128);

} // namespace assetcreator::thumbnailer
