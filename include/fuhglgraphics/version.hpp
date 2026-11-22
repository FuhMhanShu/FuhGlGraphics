#pragma once

// FuhGlGraphics version constants and runtime version string.

namespace fuhglgraphics {

constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 1;
constexpr int kVersionPatch = 0;

/// Runtime version string, e.g. "0.1.0" (compile-time macro
/// FUHGLGRAPHICS_VERSION_STRING, set by CMake/qmake from the project version).
const char* versionString();

} // namespace fuhglgraphics
