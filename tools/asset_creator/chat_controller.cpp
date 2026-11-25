#include "chat_controller.hpp"

#include "commands.hpp"

#include <fuhglgraphics/scene/node.hpp>

#include <QOpenGLContext>
#include <QRegularExpression>

namespace assetcreator {

using namespace fuhglgraphics;

ChatController::ChatController(AssetDocument* doc, ViewportWidget* viewport, ChatPanel* panel,
                               McpClient* client, QObject* parent)
    : QObject(parent), doc_(doc), vp_(viewport), panel_(panel), client_(client) {
    client_->setBuiltinHostFactory([this]() { return makeHostWithCurrentGL(); });
    connect(panel_, &ChatPanel::sendMessage, this, &ChatController::onSendMessage);
    connect(panel_, &ChatPanel::connectRequested, this, &ChatController::onConnectRequested);
    connect(client_, &McpClient::connected, this, &ChatController::onConnected);
    connect(client_, &McpClient::disconnected, panel_, [this]() {
        panel_->setConnected(false);
        panel_->setStatus(tr("MCP: not connected"));
    });
    connect(client_, &McpClient::connectionError, this, &ChatController::onConnectionError);
    connect(client_, &McpClient::toolsList, this, &ChatController::onToolsList);
    connect(client_, &McpClient::toolResult, this, &ChatController::onToolResult);
}

mcp::ToolHost ChatController::makeHostWithCurrentGL() {
    // attach/glTF/water tools need a current GL context — use the viewport's
    if (vp_ && vp_->glWidget() && vp_->glWidget()->context()) {
        vp_->glWidget()->context()->makeCurrent(vp_->glWidget());
    }
    return doc_->makeToolHost();
}

void ChatController::onConnectRequested() {
    if (client_->isConnected()) {
        client_->disconnectFromServer();
        panel_->setConnected(false);
        panel_->setStatus(tr("MCP: not connected"));
    } else {
        client_->connectToServer();
    }
}

void ChatController::onConnected() {
    panel_->setConnected(true);
    panel_->addMessage("system", tr("MCP connected. Tools: %1")
                                       .arg(QString::fromStdString(
                                           io::writeJson(lastTools_, 0))));
}

void ChatController::onConnectionError(const QString& msg) {
    panel_->addMessage("system", tr("MCP error: %1").arg(msg));
}

void ChatController::onToolsList(const io::Json& tools) {
    lastTools_ = tools;
    if (client_->isConnected()) {
        QStringList names;
        const io::Json* arr = tools.find("tools");
        if (arr)
            for (const io::Json& t : arr->arr) names << QString::fromStdString(t.at("name").asString());
        panel_->addMessage("system", tr("MCP tools (%1): %2").arg(names.size()).arg(names.join(", ")));
    }
}

void ChatController::onSendMessage(const QString& text) {
    if (!client_->isConnected()) {
        panel_->addMessage("system", tr("not connected — press \"Connect MCP\" (settings: server "
                                       "command, or \"builtin\" for the in-repo catalog)"));
        return;
    }
    if (text.startsWith('/')) {
        // direct tool call: "/tool_name {json args}"
        const QString body = text.mid(1).trimmed();
        const int sp = body.indexOf(' ');
        const QString name = sp < 0 ? body : body.left(sp);
        io::Json args;
        if (sp >= 0) {
            std::string err;
            if (!io::parseJson(body.mid(sp + 1).toStdString(), args, &err)) {
                panel_->addMessage("system", tr("bad JSON args: %1").arg(QString::fromStdString(err)));
                return;
            }
        } else {
            args.type = io::Json::Type::Object;
        }
        client_->callTool(name, args);
        return;
    }
    // chat flow: send the message to the configured chat tool
    io::Json args;
    args.type = io::Json::Type::Object;
    auto str = [](const std::string& s) { io::Json v; v.type = io::Json::Type::String; v.str = s; return v; };
    auto arr = [](const QStringList& l) {
        io::Json v;
        v.type = io::Json::Type::Array;
        for (const QString& s : l) v.arr.push_back(str(s.toStdString()));
        return v;
    };
    args.obj["message"] = str(text.toStdString());
    args.obj["history"] = arr(panel_->history());
    client_->callTool(chatToolName_, args);
}

void ChatController::onToolResult(int /*requestId*/, const io::Json& result) {
    const io::Json* content = result.find("content");
    if (!content || !content->isArray() || content->arr.empty()) {
        panel_->addMessage("tool", QString::fromStdString(io::writeJson(result, 2)));
        return;
    }
    const io::Json& textPart = content->arr[0];
    const QString text = QString::fromStdString(textPart.at("text").asString());
    panel_->addMessage("tool", text);
    io::Json payload;
    std::string perr;
    if (io::parseJson(text.toStdString(), payload, &perr) && payload.isObject()) {
        const io::Json* action = payload.find("assetAction");
        if (action && action->isObject()) applyAssetAction(*action);
        // builtin catalog mutates the document in place -> mark dirty + repaint
        if (client_->isBuiltin()) doc_->setDirty(true);
    }
}

void ChatController::applyAssetAction(const io::Json& action) {
    if (!pushCommand) return;
    const std::string op = action.at("op").asString();
    scene::Node* parent = &doc_->scene().root();
    auto vec3 = [](const io::Json& j, const fuhgl::math::Vec3& def) {
        if (j.isArray() && j.arr.size() >= 3)
            return fuhgl::math::Vec3{static_cast<float>(j.arr[0].asNumber(def.x)),
                                     static_cast<float>(j.arr[1].asNumber(def.y)),
                                     static_cast<float>(j.arr[2].asNumber(def.z))};
        return def;
    };
    auto str = [](const io::Json& j) { return j.asString(); };
    const QString name = QString::fromStdString(str(action.at("name")));
    if (op == "addNode") {
        pushCommand(makeAddNodeCommand(doc_, parent, name.isEmpty() ? "node" : name));
    } else if (op == "removeNode") {
        // find by name
        std::vector<scene::Node*> stack{&doc_->scene().root()};
        while (!stack.empty()) {
            scene::Node* n = stack.back();
            stack.pop_back();
            if (n->name() == name.toStdString()) { pushCommand(makeRemoveNodeCommand(doc_, n)); return; }
            for (const auto& c : n->children()) stack.push_back(c.get());
        }
    } else if (op == "setTrs") {
        std::vector<scene::Node*> stack{&doc_->scene().root()};
        while (!stack.empty()) {
            scene::Node* n = stack.back();
            stack.pop_back();
            if (n->name() == name.toStdString()) {
                pushCommand(makeSetTrsCommand(doc_, n, vec3(action.at("translation"), n->translation),
                                              n->rotation, vec3(action.at("scale"), n->scale)));
                return;
            }
            for (const auto& c : n->children()) stack.push_back(c.get());
        }
    } else if (op == "addShape") {
        const QString shape = QString::fromStdString(str(action.at("shape")));
        pushCommand(makeAddShapeCommand(doc_, parent, shape.isEmpty() ? "box" : shape,
                                        name.isEmpty() ? shape + "1" : name));
    } else if (op == "setEffect") {
        render::Effect e;
        if (render::effectFromName(str(action.at("effect")), e))
            pushCommand(makeSetEffectCommand(doc_, &doc_->scene().root(), e,
                                             action.at("enabled").asBool(true)));
    } else if (op == "setParam") {
        io::Json v = action.contains("value") ? action.at("value") : io::Json{};
        pushCommand(makeSetEffectParamCommand(doc_, &doc_->scene().root(),
                                              QString::fromStdString(str(action.at("name"))), v));
    } else if (op == "addWater") {
        pushCommand(makeAddWaterCommand(doc_, parent, name.isEmpty() ? "water" : name, 96, 120.f));
    } else {
        panel_->addMessage("system", tr("unknown assetAction op: %1").arg(QString::fromStdString(op)));
    }
}

} // namespace assetcreator
