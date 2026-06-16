#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>

namespace inferno {

class PropagationPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropagationPanel(QWidget* parent = nullptr);
    ~PropagationPanel() override = default;

    void appendResult(const QString& ip, const QString& result);

signals:
    void scanRequested(const QString& target);
    void bruteRequested(const QString& target);
    void deployRequested(const QString& target);
    void statusMessage(const QString& msg);

private:
    void setupUI();

    QLineEdit*      m_targetInput;
    QPushButton*    m_btnScan;
    QPushButton*    m_btnBrute;
    QPushButton*    m_btnDeploy;
    QPlainTextEdit* m_logOutput;
    QStringList     m_history;
};

} // namespace inferno
