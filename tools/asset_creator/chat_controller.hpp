#pragma once

/// @file chat_controller.hpp
/// @brief G12 asset creator — ChatController (MVC controller): ChatPanel <-> McpClient.
/// User messages go to the configured chat tool (tools/call {message, history}) or, when
/// the message starts with '/', directly as a tool call ("/procedural_wfc {...}").
/// Tool results carrying an `assetAction` object are mapped onto QUndoCommands
/// (LLM -> editor bridge, docs/ASSET_CREATOR.md §7); with the builtin catalog the host
/// mutates the document directly, so the controller just marks it dirty + repaints.

#include "asset_document.hpp"
#include "chat_panel.hpp"
#include "mcp/mcp_client.hpp"
#include "viewport_widget.hpp"

#include <QObject>

#include <functional>
#include <memory>

namespace assetcreator {

class ChatController : public QObject {
    Q_OBJECT
public:
    ChatController(AssetDocument* doc, ViewportWidget* viewport, ChatPanel* panel,
                   McpClient* client, QObject* parent = nullptr);

    /// @brief Set by MainWindow: push a QUndoCommand (also refreshes panels/viewport).
    std::function<void(std::unique_ptr<QUndoCommand>)> pushCommand;

    void setChatToolName(const QString& name) { chatToolName_ = name; }

private slots:
    void onSendMessage(const QString& text);
    void onConnectRequested();
    void onConnected();
    void onConnectionError(const QString& msg);
    void onToolsList(const fuhglgraphics::io::Json& tools);
    void onToolResult(int requestId, const fuhglgraphics::io::Json& result);

private:
    /// @brief Makes the viewport GL context current (for builtin attach/glTF/water) and
    /// returns the document's MCP tool host.
    fuhglgraphics::mcp::ToolHost makeHostWithCurrentGL();
    void applyAssetAction(const fuhglgraphics::io::Json& action);

    AssetDocument* doc_;
    ViewportWidget* vp_;
    ChatPanel* panel_;
    McpClient* client_;
    QString chatToolName_ = QStringLiteral("chat");
    fuhglgraphics::io::Json lastTools_;
};

} // namespace assetcreator
