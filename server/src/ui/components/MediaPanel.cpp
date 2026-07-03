#include "../../../include/ui/components/MediaPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include "../../../include/database/Inferno_Database.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QImage>
#include <QMessageBox>
#include <QLabel>

namespace inferno {

MediaPanel::MediaPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void MediaPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);

    // Header
    auto* header = new QHBoxLayout();
    header->addWidget(new QLabel("MEDIA CAPTURE"));
    header->addStretch();
    auto* headerDesc = new QLabel("SCREENSHOT | CAMERA");
    headerDesc->setStyleSheet("color: #888; font-size: 11px;");
    header->addWidget(headerDesc);
    layout->addLayout(header);

    // Agent selector + capture button
    auto* controlRow = new QHBoxLayout();
    m_agentSelector = new QComboBox();
    m_agentSelector->setPlaceholderText("Select agent...");
    m_agentSelector->setMinimumWidth(200);
    m_agentSelector->setStyleSheet(ui::style::INPUT_BOX);
    controlRow->addWidget(m_agentSelector);
    controlRow->addStretch();

    m_btnCapture = new QPushButton("📷 Capture Screenshot");
    m_btnCapture->setStyleSheet(
        "QPushButton { background-color: #004d00; color: #00ff41; "
        "border: 1px solid #00ff41; padding: 8px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #006600; }"
        "QPushButton:disabled { background-color: #333; color: #666; border-color: #555; }");
    m_btnCapture->setEnabled(false);
    controlRow->addWidget(m_btnCapture);
    layout->addLayout(controlRow);

    connect(m_agentSelector, &QComboBox::currentTextChanged, this, [this](const QString&) {
        m_btnCapture->setEnabled(!m_agentSelector->currentText().isEmpty());
    });
    connect(m_btnCapture, &QPushButton::clicked, this, [this]() {
        QString ip = m_agentSelector->currentText();
        if (!ip.isEmpty()) emit screenshotRequested(ip);
    });

    // Preview area
    m_previewLabel = new QLabel();
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(
        "QLabel { background-color: #0a0a0a; border: 1px solid #333; "
        "min-height: 360px; color: #555; }");
    m_previewLabel->setText("No screenshot captured yet.\nSelect an agent and click Capture.");
    layout->addWidget(m_previewLabel, 1);

    // Save button
    m_btnSave = new QPushButton("Save to Loot");
    m_btnSave->setStyleSheet(
        "QPushButton { background-color: #1a1a2e; color: #888; "
        "border: 1px solid #555; padding: 6px 14px; }"
        "QPushButton:hover { background-color: #2a2a3e; color: #ccc; }"
        "QPushButton:disabled { color: #444; border-color: #333; }");
    m_btnSave->setEnabled(false);
    layout->addWidget(m_btnSave);

    connect(m_btnSave, &QPushButton::clicked, this, [this]() {
        if (!m_lastJpeg.isEmpty()) {
            emit saveToLootRequested(m_lastAgentIp, m_lastJpeg,
                                     m_lastWidth, m_lastHeight);
        }
    });
}

void MediaPanel::displayScreenshot(const QString& ip, const QByteArray& jpeg,
                                    int width, int height, bool success) {
    if (!success || jpeg.isEmpty()) {
        m_previewLabel->setText(
            QString("Screenshot capture failed for %1.").arg(ip));
        m_btnSave->setEnabled(false);
        return;
    }

    m_lastJpeg = jpeg;
    m_lastWidth = width;
    m_lastHeight = height;
    m_lastAgentIp = ip;

    QPixmap pixmap;
    if (pixmap.loadFromData(jpeg, "JPEG")) {
        // Scale to fit preview while maintaining aspect ratio
        QPixmap scaled = pixmap.scaled(800, 600, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
        m_previewLabel->setPixmap(scaled);
        m_previewLabel->setText(QString());
    } else {
        m_previewLabel->setText("Failed to decode JPEG preview.");
    }

    m_btnSave->setEnabled(true);
    emit statusMessage(QString("Screenshot received from %1 (%2x%3, %4 KB)")
                           .arg(ip)
                           .arg(width)
                           .arg(height)
                           .arg(jpeg.size() / 1024));
}

void MediaPanel::setAgentList(const QStringList& ips) {
    QString current = m_agentSelector->currentText();
    m_agentSelector->clear();
    m_agentSelector->addItems(ips);
    int idx = m_agentSelector->findText(current);
    if (idx >= 0) m_agentSelector->setCurrentIndex(idx);
    m_btnCapture->setEnabled(m_agentSelector->count() > 0);
}

} // namespace inferno
