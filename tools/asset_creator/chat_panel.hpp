#pragma once

/// @file chat_panel.hpp
/// @brief G12 asset creator — ChatPanel (view): chat history (user/assistant/tool/system
/// roles), input line, send + connect buttons, status label, and the MCP settings are
/// edited in MainWindow's dialog; the panel only emits user intents.

#include <QWidget>

class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;

namespace assetcreator {

class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void addMessage(const QString& role, const QString& text);
    void setStatus(const QString& text);
    void setConnected(bool connected);
    QStringList history() const;

signals:
    void sendMessage(const QString& text);
    void connectRequested();

private:
    QListWidget* list_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QLabel* status_ = nullptr;
    QStringList history_;
};

} // namespace assetcreator
