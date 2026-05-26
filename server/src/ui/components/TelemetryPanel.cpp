#include "../../../include/ui/components/TelemetryPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include "../../../include/database/Inferno_Database.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>

namespace inferno {

TelemetryPanel::TelemetryPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void TelemetryPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* header = new QHBoxLayout();
    header->addWidget(new QLabel("TELEMETRY CONSOLE"));
    header->addStretch();
    
    auto* btnShell = new QPushButton();
    btnShell->setObjectName("iconButton");
    btnShell->setFixedSize(40, 40);
    btnShell->setIcon(QIcon(":/icon_shell.png"));
    btnShell->setIconSize(QSize(32, 32));
    btnShell->setToolTip("Execute Shell");
    connect(btnShell, &QPushButton::clicked, this, &TelemetryPanel::shellRequested);
    
    auto* btnProcs = new QPushButton();
    btnProcs->setObjectName("iconButton");
    btnProcs->setFixedSize(40, 40);
    btnProcs->setIcon(QIcon(":/icon_refresh.png"));
    btnProcs->setIconSize(QSize(32, 32));
    btnProcs->setToolTip("Refresh Process List");
    connect(btnProcs, &QPushButton::clicked, this, &TelemetryPanel::processesRequested);

    auto* btnClearConsole = new QPushButton();
    btnClearConsole->setObjectName("iconButton");
    btnClearConsole->setFixedSize(40, 40);
    btnClearConsole->setIcon(QIcon(":/icon_clear.png"));
    btnClearConsole->setIconSize(QSize(32, 32));
    btnClearConsole->setToolTip("Clear Console");
    connect(btnClearConsole, &QPushButton::clicked, this, &TelemetryPanel::clearConsole);

    auto* btnHistory = new QPushButton();
    btnHistory->setObjectName("iconButton");
    btnHistory->setFixedSize(40, 40);
    btnHistory->setIcon(QIcon(":/icon_history.png"));
    btnHistory->setIconSize(QSize(32, 32));
    btnHistory->setToolTip("Load Telemetry History");
    connect(btnHistory, &QPushButton::clicked, this, [this](){
        if (!m_activeUuid.isEmpty()) loadHistory(m_activeUuid);
    });

    m_typeFilter = new QComboBox();
    m_typeFilter->addItem("All History", "ALL");
    m_typeFilter->addItem("Shell only", "SHELL");
    m_typeFilter->addItem("Processes only", "PROC");
    m_typeFilter->setStyleSheet(ui::style::COMBO_BOX);
    connect(m_typeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TelemetryPanel::onFilterChanged);

    header->addWidget(btnShell);
    header->addWidget(btnProcs);
    header->addWidget(btnHistory);
    header->addWidget(m_typeFilter);
    header->addWidget(btnClearConsole);
    layout->addLayout(header);
    
    // Console Search Bar
    auto* searchLayout = new QHBoxLayout();
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search telemetry...");
    m_searchBox->setStyleSheet(ui::style::INPUT_BOX);
    
    auto* btnSearch = new QPushButton();
    btnSearch->setFixedSize(40, 40);
    btnSearch->setIcon(QIcon(":/icon_search.png"));
    btnSearch->setIconSize(QSize(32, 32));
    btnSearch->setObjectName("iconButton");
    connect(btnSearch, &QPushButton::clicked, this, &TelemetryPanel::highlightSearchMatches);
    
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(200);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, &TelemetryPanel::highlightSearchMatches);
    connect(m_searchBox, &QLineEdit::textChanged, m_searchDebounceTimer, QOverload<>::of(&QTimer::start));
    
    searchLayout->addWidget(m_searchBox);
    searchLayout->addWidget(btnSearch);
    layout->addLayout(searchLayout);
    
    m_console = new QPlainTextEdit();
    m_console->setReadOnly(true);
    m_console->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_console, &QPlainTextEdit::customContextMenuRequested, this, [this](const QPoint& pos){
        QMenu menu(this);
        menu.addAction("Copy Selected", m_console, &QPlainTextEdit::copy);
        menu.exec(m_console->mapToGlobal(pos));
    });
    layout->addWidget(m_console);
}

void TelemetryPanel::clearConsole() {
    m_console->clear();
    m_history.clear();
    emit statusMessage("Telemetry buffer cleared");
}

void TelemetryPanel::appendText(const QString& text) {
    m_history.append(text);
    constexpr int kMaxHistoryLines = 50000;
    if (m_history.size() > kMaxHistoryLines) {
        m_history.removeFirst();
    }
    // Only append to UI if it matches current filter or filter is empty
    if (m_searchBox->text().isEmpty() || text.contains(m_searchBox->text(), Qt::CaseInsensitive)) {
        m_console->appendPlainText(text);
        if (!m_searchBox->text().isEmpty()) {
            applyVisualHighlighting();
        }
    }
}

void TelemetryPanel::loadHistory(const QString& uuid) {
    m_activeUuid = uuid;
    m_console->clear();
    QString filter = m_searchBox->text();
    QString type = m_typeFilter->currentData().toString();
    
    QStringList dbHistory = Inferno_Database::instance().getTelemetryHistory(uuid, type);
    for (const QString& line : dbHistory) {
        if (filter.isEmpty() || line.contains(filter, Qt::CaseInsensitive)) {
            m_console->appendPlainText(line);
        }
    }
    m_console->appendPlainText("[SYSTEM] Database history dump complete.");
    emit statusMessage("Telemetry history reloaded from SQL");
}

void TelemetryPanel::onSearchTextChanged() {
    highlightSearchMatches();
}

void TelemetryPanel::onFilterChanged() {
    if (!m_activeUuid.isEmpty()) {
        loadHistory(m_activeUuid);
    }
}

void TelemetryPanel::highlightSearchMatches() {
    m_console->clear();
    for (const QString& line : m_history) {
        if (m_searchBox->text().isEmpty() || line.contains(m_searchBox->text(), Qt::CaseInsensitive)) {
            m_console->appendPlainText(line);
        }
    }
    applyVisualHighlighting();
}

void TelemetryPanel::applyVisualHighlighting() {
    QString text = m_searchBox->text();
    if (text.isEmpty()) {
        m_console->setExtraSelections({});
        return;
    }

    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextDocument* doc = m_console->document();
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
    m_console->setExtraSelections(extraSelections);
}

} // namespace inferno
