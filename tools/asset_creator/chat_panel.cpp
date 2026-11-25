#include "chat_panel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace assetcreator {

ChatPanel::ChatPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    v->addWidget(list_);
    status_ = new QLabel(tr("MCP: not connected"), this);
    v->addWidget(status_);
    auto* h = new QHBoxLayout();
    input_ = new QLineEdit(this);
    input_->setPlaceholderText(tr("Message the LLM, or /tool_name {json} for a direct MCP tool call..."));
    sendButton_ = new QPushButton(tr("Send"), this);
    connectButton_ = new QPushButton(tr("Connect MCP"), this);
    h->addWidget(input_, 1);
    h->addWidget(sendButton_);
    h->addWidget(connectButton_);
    v->addLayout(h);

    connect(sendButton_, &QPushButton::clicked, this, [this]() {
        const QString text = input_->text().trimmed();
        if (text.isEmpty()) return;
        input_->clear();
        addMessage("user", text);
        emit sendMessage(text);
    });
    connect(input_, &QLineEdit::returnPressed, this, [this]() {
        const QString text = input_->text().trimmed();
        if (text.isEmpty()) return;
        input_->clear();
        addMessage("user", text);
        emit sendMessage(text);
    });
    connect(connectButton_, &QPushButton::clicked, this, [this]() { emit connectRequested(); });
}

void ChatPanel::addMessage(const QString& role, const QString& text) {
    history_.append(role + ": " + text);
    if (history_.size() > 200) history_.removeFirst();
    auto* item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(role, text), list_);
    list_->addItem(item);
    list_->scrollToBottom();
}

void ChatPanel::setStatus(const QString& text) { status_->setText(text); }

void ChatPanel::setConnected(bool connected) {
    connectButton_->setText(connected ? tr("Disconnect") : tr("Connect MCP"));
    if (connected) status_->setText(tr("MCP: connected"));
}

QStringList ChatPanel::history() const { return history_; }

} // namespace assetcreator
