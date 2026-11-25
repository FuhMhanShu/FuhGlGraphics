#include "mcp_client.hpp"

#include <fuhglgraphics/mcp/mcp_server.hpp>

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace assetcreator {

using namespace fuhglgraphics;

McpClient::McpClient(QObject* parent) : QObject(parent) {}

McpClient::~McpClient() { disconnectFromServer(); }

void McpClient::setServerCommand(const QString& command) { serverCommand_ = command.trimmed(); }

void McpClient::setBuiltinHostFactory(std::function<mcp::ToolHost()> factory) {
    builtinHostFactory_ = std::move(factory);
}

bool McpClient::isConnected() const {
    return builtinConnected_ || (process_ && process_->state() == QProcess::Running);
}

void McpClient::connectToServer() {
    disconnectFromServer();
    if (isBuiltin()) {
        if (!builtinHostFactory_) {
            emit connectionError(tr("builtin MCP needs a tool-host factory (set by MainWindow)"));
            return;
        }
        builtinConnected_ = true;
        initialized_ = true;
        emit connected();
        requestToolsList();
        return;
    }
    process_ = new QProcess(this);
    connect(process_, &QProcess::readyReadStandardOutput, this, &McpClient::onReadyRead);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit connectionError(tr("failed to start MCP server: %1").arg(serverCommand_));
        else
            emit connectionError(tr("MCP server process error"));
    });
    connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        initialized_ = false;
        emit disconnected();
    });
    // clean, explicit environment: no credentials in the project file — API keys come
    // from the environment (docs/ASSET_CREATOR.md §7 Security)
    process_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    const QStringList parts = serverCommand_.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        emit connectionError(tr("empty MCP server command (settings dialog)"));
        return;
    }
    process_->start(parts.first(), parts.mid(1));
    if (!process_->waitForStarted(5000)) {
        emit connectionError(tr("MCP server did not start: %1").arg(parts.first()));
        return;
    }
    initialized_ = false;
    sendLine(mcp::makeRequest(mcp::jsonNumber(nextId_++), "initialize",
                              mcp::jsonObject({{"protocolVersion", mcp::jsonString(mcp::kProtocolVersion)}})));
}

void McpClient::disconnectFromServer() {
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
        process_->deleteLater();
        process_ = nullptr;
    }
    builtinConnected_ = false;
    initialized_ = false;
}

void McpClient::sendLine(const io::Json& msg) {
    if (isBuiltin()) return; // builtin answers synchronously in callTool/requestToolsList
    if (!process_) return;
    process_->write(mcp::serializeMessage(msg).c_str());
    process_->write("\n");
    process_->flush();
}

void McpClient::requestToolsList() {
    if (isBuiltin()) {
        if (!builtinHostFactory_) return;
        mcp::ToolHost host = builtinHostFactory_();
        const std::string resp = mcp::handleLine(
            mcp::serializeMessage(mcp::makeRequest(mcp::jsonNumber(nextId_++), "tools/list",
                                                   mcp::jsonObject({}))),
            host);
        mcp::Message m;
        if (mcp::parseMessage(resp, m)) emit toolsList(m.result);
        return;
    }
    sendLine(mcp::makeRequest(mcp::jsonNumber(nextId_++), "tools/list", mcp::jsonObject({})));
}

void McpClient::callTool(const QString& name, const io::Json& arguments) {
    const int id = nextId_++;
    pendingToolNames_[id] = name;
    io::Json params = mcp::jsonObject({{"name", mcp::jsonString(name.toStdString())},
                                       {"arguments", arguments}});
    if (isBuiltin()) {
        if (!builtinHostFactory_) {
            emit connectionError(tr("builtin MCP needs a tool-host factory"));
            return;
        }
        mcp::ToolHost host = builtinHostFactory_(); // caller ensures a current GL context
        const std::string resp = mcp::handleLine(
            mcp::serializeMessage(mcp::makeRequest(mcp::jsonNumber(id), "tools/call", params)), host);
        mcp::Message m;
        if (mcp::parseMessage(resp, m)) emit toolResult(id, m.result);
        return;
    }
    sendLine(mcp::makeRequest(mcp::jsonNumber(id), "tools/call", params));
}

void McpClient::onReadyRead() {
    if (!process_) return;
    readBuffer_ += process_->readAllStandardOutput().toStdString();
    std::string line;
    while (mcp::extractLine(readBuffer_, line)) handleIncomingLine(line);
}

void McpClient::onProcessError() {}

void McpClient::handleIncomingLine(const std::string& line) {
    mcp::Message m;
    std::string err;
    if (!mcp::parseMessage(line, m, &err)) return;
    if (m.kind == mcp::Message::Kind::Response) {
        if (m.errorCode != 0) {
            emit connectionError(QString::fromStdString(m.errorMessage));
            return;
        }
        // initialize response -> send notifications/initialized + tools/list
        if (!initialized_ && m.result.isObject() && m.result.contains("protocolVersion")) {
            initialized_ = true;
            sendLine(mcp::makeNotification("notifications/initialized", mcp::jsonObject({})));
            emit connected();
            requestToolsList();
            return;
        }
        const int id = m.id.isNumber() ? static_cast<int>(m.id.number) : -1;
        if (m.result.isObject() && m.result.contains("tools")) {
            emit toolsList(m.result);
        } else if (m.result.isObject() && m.result.contains("content")) {
            emit toolResult(id, m.result);
        }
    }
}

} // namespace assetcreator
