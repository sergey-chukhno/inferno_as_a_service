#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QByteArray>

namespace inferno {

class MediaPanel : public QWidget {
    Q_OBJECT

public:
    explicit MediaPanel(QWidget* parent = nullptr);
    ~MediaPanel() override = default;

    void displayScreenshot(const QString& ip, const QByteArray& jpeg,
                           int width, int height, bool success);
    void setAgentList(const QStringList& ips);

signals:
    void screenshotRequested(const QString& ip);
    void saveToLootRequested(const QString& ip, const QByteArray& jpeg,
                             int width, int height);
    void statusMessage(const QString& msg);

private:
    void setupUI();

    QComboBox*      m_agentSelector;
    QPushButton*    m_btnCapture;
    QLabel*         m_previewLabel;
    QPushButton*    m_btnSave;
    QByteArray      m_lastJpeg;
    int             m_lastWidth = 0;
    int             m_lastHeight = 0;
    QString         m_lastAgentIp;
};

} // namespace inferno
