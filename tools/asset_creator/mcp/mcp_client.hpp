#pragma once

/// @file mcp_client.hpp
/// @brief G12 asset creator — McpClient (controller): talks MCP (JSON-RPC 2.0,
/// line-delimited) to a configurable external server command over stdio (QProcess), OR
/// in-process to the built-in FuhGlGraphics catalog ("builtin" command) via
/// mcp::handleLine + a host factory (the document's makeToolHost — call with a current
/// GL context for attach operations). Framing/parsing via the Qt-free mcp::jsonrpc helpers.

#include <fuhglgraphics/mcp/jsonrpc.hpp>
#include <fuhglgraphics/mcp/tool_catalog.hpp>

#include <QObject>

#include <functional>
#include <map>

class QProcess;

namespace assetcreator {

class McpClient : public QObject {
    Q_OBJECT
public:
    explicit McpClient(QObject* parent = nullptr);
    ~McpClient() override;

    /// @brief Server command from the settings dialog; empty or "builtin" selects the
    /// in-process catalog server.
    void setServerCommand(const QString& command);
    QString serverCommand() const { return serverCommand_; }
    bool isBuiltin() const { return serverCommand_.isEmpty() || serverCommand_ == "builtin"; }

    /// @brief Host factory for builtin mode (document's makeToolHost).
    void setBuiltinHostFactory(std::function<fuhglgraphics::mcp::ToolHost()> factory);

    bool isConnected() const;
    void connectToServer();
    void disconnectFromServer();

    void requestToolsList();
    /// @brief Async tools/call (external) or synchronous (builtin); result via toolResult().
    void callTool(const QString& name, const fuhglgraphics::io::Json& arguments);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& message);
    void toolsList(const fuhglgraphics::io::Json& tools);
    /// @brief Full JSON-RPC result of a tools/call ({"content": [...], "isError": bool}).
    void toolResult(int requestId, const fuhglgraphics::io::Json& result);

private slots:
    void onReadyRead();
    void onProcessError();

private:
    void sendLine(const fuhglgraphics::io::Json& msg);
    void handleIncomingLine(const std::string& line);

    QString serverCommand_;
    QProcess* process_ = nullptr;
    std::string readBuffer_;
    int nextId_ = 1;
    bool initialized_ = false;
    bool builtinConnected_ = false;
    std::function<fuhglgraphics::mcp::ToolHost()> builtinHostFactory_;
    std::map<int, QString> pendingToolNames_; ///< request id -> tool name (for display)
};

} // namespace assetcreator
