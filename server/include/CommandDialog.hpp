#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

namespace inferno {

class CommandDialog : public QDialog {
    Q_OBJECT

public:
    explicit CommandDialog(const QString& agentIp, QWidget* parent = nullptr);
    QString getCommand() const;

private slots:
    void onCopy();
    void onPaste();
    void onClear();

private:
    QPlainTextEdit* m_commandInput;
    QString m_agentIp;
};

} // namespace inferno
