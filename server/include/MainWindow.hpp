#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QListWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QToolBar>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
#include "server.hpp"

namespace inferno {

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
    void highlightSearchMatches(const QString& text);
    void applyVisualHighlighting();
    void filterKeylogStream(const QString& text);
    void loadTelemetryHistory();
    void loadKeylogHistory();

    // Circle 6 - Intelligence Analysis slots
    void forceScanHistory();
    void loadIntelligenceList();
    void copySelectedIntel();
    void clearIntelFindings();

private:
    void setupUI();
    void loadStyleSheet();
    void appendToTelemetry(const QString& text);
    void appendToKeylog(const QString& text);

    Server* m_server;

    // UI Components
    QTabWidget*     m_tabWidget;
    QListWidget*    m_agentList;
    QPlainTextEdit* m_telemetryConsole;
    QPlainTextEdit* m_keylogStream;
    QLabel*         m_statusLabel;
    QLineEdit*      m_searchBox;
    QComboBox*      m_typeFilter;
    QLineEdit*      m_keylogSearchBox;

    // Circle 6 - Intelligence Analysis Components
    QTableWidget*   m_intelTable;
    QComboBox*      m_intelTypeFilter;
    QLineEdit*      m_intelSearchBox;
    
    // History Buffers
    QMap<QString, QString> m_agentIpToUuid; // Maps IP to persistent UUID
    QMap<QString, QString> m_agentRawKeylogs; // Maps agent UUID to accumulated raw keylogs in-memory
    QStringList     m_telemetryHistory;
    QStringList     m_keylogHistory;
    
    // Animation State
    QPushButton*    m_btnScan;
    QPushButton*    m_btnKeylog;
    QTimer*         m_radarTimer;
    QTimer*         m_keylogPollTimer;
    QTimer*         m_searchDebounceTimer;
    int             m_radarAngle;
};

} // namespace inferno
