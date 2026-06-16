#include "../../../include/ui/components/PropagationPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QDateTime>

namespace inferno {

PropagationPanel::PropagationPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void PropagationPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Header
    auto* header = new QHBoxLayout();
    header->addWidget(new QLabel("NETWORK PROPAGATION"));
    header->addStretch();

    auto* headerDesc = new QLabel("SCAN | BRUTE | DEPLOY");
    headerDesc->setStyleSheet("color: #888; font-size: 11px;");
    header->addWidget(headerDesc);
    layout->addLayout(header);

    // Target input row
    auto* targetLayout = new QHBoxLayout();
    m_targetInput = new QLineEdit();
    m_targetInput->setPlaceholderText("Target IP or subnet (e.g. 172.17.0.0/16)");
    m_targetInput->setStyleSheet(ui::style::INPUT_BOX);
    m_targetInput->setText("172.17.0.0/16");
    targetLayout->addWidget(m_targetInput, 1);
    layout->addLayout(targetLayout);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();

    m_btnScan = new QPushButton("SCAN");
    m_btnScan->setToolTip("Scan subnet for live hosts");
    connect(m_btnScan, &QPushButton::clicked, this, [this]() {
        emit scanRequested(m_targetInput->text());
    });
    buttonLayout->addWidget(m_btnScan);

    m_btnBrute = new QPushButton("BRUTE");
    m_btnBrute->setToolTip("Brute-force SSH/SMB on target");
    connect(m_btnBrute, &QPushButton::clicked, this, [this]() {
        emit bruteRequested(m_targetInput->text());
    });
    buttonLayout->addWidget(m_btnBrute);

    m_btnDeploy = new QPushButton("DEPLOY");
    m_btnDeploy->setToolTip("Upload and execute agent on target");
    connect(m_btnDeploy, &QPushButton::clicked, this, [this]() {
        emit deployRequested(m_targetInput->text());
    });
    buttonLayout->addWidget(m_btnDeploy);

    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    // Log output
    m_logOutput = new QPlainTextEdit();
    m_logOutput->setReadOnly(true);
    m_logOutput->setPlaceholderText("Propagation results will appear here...");
    layout->addWidget(m_logOutput, 1);
}

void PropagationPanel::appendResult(const QString& ip, const QString& result) {
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString line = QString("[%1] [%2] %3").arg(timestamp, ip, result);
    m_history.append(line);
    constexpr int kMaxHistoryLines = 10000;
    if (m_history.size() > kMaxHistoryLines) {
        m_history.removeFirst();
    }
    m_logOutput->appendPlainText(line);
}

} // namespace inferno
