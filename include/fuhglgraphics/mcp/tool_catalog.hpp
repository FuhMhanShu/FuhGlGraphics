#pragma once

/// @file tool_catalog.hpp
/// @brief G12 MCP tool catalog (Qt-free, unit-testable): the tools the FuhGlGraphics
/// MCP server (mcp/mcp_server.hpp) and the asset creator expose over `tools/list` /
/// `tools/call`. Besides the scene-editing ops, the catalog makes the **procedural
/// functions** (G24–G31: noise, marching cubes, L-systems, WFC, voronoi, SDF/fractals,
/// creatures, world generator) and the **deferred renderer settings** (render::Effect
/// toggles + scene::EffectParams, incl. the G13 water parameters) accessible to an LLM.
///
/// `callTool()` executes one `tools/call` against a ToolHost: pure CPU for scene ops,
/// render settings, water params and procedural *data*; attaching generated geometry to
/// the scene needs the host's GL resolvers (meshResolver / meshDataResolver), otherwise
/// the tools return the generated data only ("attached": false).
///
/// Conventions: bool + error string, never exceptions; namespace fuhglgraphics::mcp.

#include <fuhglgraphics/io/json.hpp>
#include <fuhglgraphics/scene/effects.hpp>
#include <fuhglgraphics/scene/scene.hpp>
#include <fuhglgraphics/scene/serialize.hpp>
#include <fuhglgraphics/water/water_surface.hpp>

#include <fuhgl/render/mesh.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fuhglgraphics::mcp {

/// @brief Everything a tools/call may touch (bound by the host: the Qt asset creator
/// binds its document + GL; the standalone mcp_server binds a plain CPU scene).
struct ToolHost {
    scene::Scene* scene = nullptr;          ///< target scene graph (scene_* tools)
    scene::Effects* effects = nullptr;      ///< render-settings target (render_* tools, e.g. &root().effects)
    water::WaterSurface* water = nullptr;   ///< G13 water surface (water_* tools)
    /// @brief Shape-spec -> GL mesh (like scene::defaultShapeResolver); empty = data-only.
    scene::MeshResolver meshResolver;
    /// @brief MeshData -> GL mesh (procedural attach); empty = data-only.
    std::function<std::unique_ptr<fuhgl::render::Mesh>(const fuhgl::render::MeshData&)> meshDataResolver;
    /// @brief Creates the water grid mesh (needs a current GL context); empty = unsupported.
    std::function<bool(water::WaterSurface&, int resolution, float size)> waterCreate;
    /// @brief Owns resolved GL meshes (host lifetime).
    std::vector<std::unique_ptr<fuhgl::render::Mesh>>* meshArena = nullptr;
    /// @brief Owns CPU MeshData copies referenced by MeshEntry::meshData (picking).
    std::vector<std::unique_ptr<fuhgl::render::MeshData>>* meshDataArena = nullptr;
    int glMajor = 3, glMinor = 3;           ///< for capability reporting (render_list_effects)
};

/// @brief MCP `tools/list` result: {"tools": [{name, description, inputSchema}, ...]}.
io::Json toolsListJson();

/// @brief True when `name` is a catalog tool.
bool toolExists(const std::string& name);

/// @brief Executes one `tools/call`: returns the tool's payload Json (the server wraps it
/// into the MCP result content). Returns Null + err on unknown tool / invalid arguments.
io::Json callTool(const std::string& name, const io::Json& arguments, ToolHost& host,
                  std::string* err = nullptr);

/// @brief Wraps a payload into an MCP tools/call result:
/// {"content": [{"type": "text", "text": <payload as JSON string>}], "isError": isError}.
io::Json makeToolResult(const io::Json& payload, bool isError = false);

} // namespace fuhglgraphics::mcp
