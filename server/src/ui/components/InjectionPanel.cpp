#include "../../../include/ui/components/InjectionPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileInfo>
#include <QPushButton>
#include <QLabel>

namespace inferno {

enum Col {
    COL_AGENT, COL_APP, COL_VECTOR, COL_TCC, COL_STATUS, COL_ACTION,
    COL_COUNT
};

static QString capabilityName(int cap) {
    switch (cap) {
        case 0: return "NONE";
        case 1: return "DYLD_INSERT_LIBRARIES";
        case 2: return "MACH_VM_ALLOCATE";
        case 3: return "DYLIB_PROXYING";
        default: return QString::number(cap);
    }
}

QString InjectionPanel::tccIcon(bool hasScreenRecording, bool hasCamera) const {
    if (hasScreenRecording && hasCamera) return QString::fromUtf8("🎥📷");
    if (hasScreenRecording) return QString::fromUtf8("🎥");
    if (hasCamera) return QString::fromUtf8("📷");
    return QString::fromUtf8("❌");
}

InjectionPanel::InjectionPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(0, COL_COUNT, this);
    m_table->setHorizontalHeaderLabels({
        "Agent", "Target App", "Vector", "TCC", "Status", "Action"
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ACTION, QHeaderView::Fixed);
    m_table->setColumnWidth(COL_ACTION, 190);
    m_table->setColumnWidth(COL_TCC, 60);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setStyleSheet(ui::style::INTEL_TABLE);
    m_table->verticalHeader()->setDefaultSectionSize(50);
    layout->addWidget(m_table);
}

void InjectionPanel::addActionButton(int row, const QString& ip,
                                      const QString& fullPath, bool isInjected) {
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);

    auto* injectBtn = new QPushButton(isInjected ? "✅" : "Inject");
    injectBtn->setFixedSize(60, 30);
    if (isInjected) {
        injectBtn->setStyleSheet(
            "QPushButton { background: #333; color: #666; "
            "border: 1px solid #444; font-size: 12px; border-radius: 4px; padding: 2px 4px; }");
    } else {
        injectBtn->setStyleSheet(
            "QPushButton { background: #004d00; color: #00ff41; "
            "border: 2px solid #00ff41; font-size: 12px; border-radius: 4px; "
            "font-weight: bold; padding: 2px 4px; }"
            "QPushButton:hover { background: #00ff41; color: #000; }");
        injectBtn->setEnabled(true);
    }
    connect(injectBtn, &QPushButton::clicked, this, [this, ip, fullPath]() {
        emit injectRequested(ip, fullPath);
    });
    layout->addWidget(injectBtn);

    // Grant TCC button (only on macOS, when not injected yet)
    QPushButton* tccBtn = nullptr;
    QTableWidgetItem* tccItem = m_table->item(row, COL_TCC);
    if (tccItem && tccItem->text() == QString::fromUtf8("❌") && !isInjected) {
        tccBtn = new QPushButton("Grant TCC");
        tccBtn->setFixedSize(80, 30);
        tccBtn->setStyleSheet(
            "QPushButton { background: #1a1a4e; color: #4488ff; "
            "border: 1px solid #4488ff; font-size: 11px; border-radius: 4px; padding: 2px 4px; }"
            "QPushButton:hover { background: #2a2a5e; }");
        tccBtn->setEnabled(true);
        QString bundleId = tccItem->data(Qt::UserRole).toString();
        connect(tccBtn, &QPushButton::clicked, this, [this, ip, bundleId]() {
            emit tccGrantRequested(ip, bundleId);
        });
        layout->addWidget(tccBtn);
    }

    m_table->setCellWidget(row, COL_ACTION, container);
}

void InjectionPanel::onScanResult(const QString& ip, const QString& report) {
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        if (m_table->item(row, COL_AGENT)->text() == ip) {
            m_table->removeRow(row);
        }
    }

    QStringList lines = report.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty() || (lines.size() == 1 && lines[0].startsWith("none"))) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, COL_AGENT, new QTableWidgetItem(ip));
        m_table->setItem(row, COL_APP, new QTableWidgetItem("(none)"));
        m_table->setItem(row, COL_VECTOR, new QTableWidgetItem("-"));
        m_table->setItem(row, COL_TCC, new QTableWidgetItem("-"));
        m_table->setItem(row, COL_STATUS, new QTableWidgetItem("No injectable apps"));
        return;
    }

    for (const QString& line : lines) {
        QStringList parts = line.split('|');
        // Format: path|bundle_id|capability|is_host|has_sr|has_camera
        if (parts.size() < 4) continue;

        QString fullPath = parts[0];
        QString bundleId = parts.size() >= 2 ? parts[1] : "";
        QFileInfo fi(fullPath);
        QString appName = fi.baseName();
        int cap = parts[2].toInt();
        bool isInjected = parts[3] == "1";
        bool hasSR = parts.size() >= 5 ? parts[4] == "1" : false;
        bool hasCam = parts.size() >= 6 ? parts[5] == "1" : false;

        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, COL_AGENT, new QTableWidgetItem(ip));

        auto* nameItem = new QTableWidgetItem(appName);
        nameItem->setData(Qt::UserRole, fullPath);
        m_table->setItem(row, COL_APP, nameItem);

        m_table->setItem(row, COL_VECTOR, new QTableWidgetItem(capabilityName(cap)));

        auto* tccItem = new QTableWidgetItem(tccIcon(hasSR, hasCam));
        tccItem->setData(Qt::UserRole, bundleId);
        tccItem->setToolTip(
            hasSR && hasCam ? "Screen Recording + Camera"
            : hasSR ? "Screen Recording only"
            : hasCam ? "Camera only"
            : "No TCC permissions");
        m_table->setItem(row, COL_TCC, tccItem);

        m_table->setItem(row, COL_STATUS, new QTableWidgetItem(
            isInjected ? "✅ Injected" : "Ready"));

        addActionButton(row, ip, fullPath, isInjected);
    }
}

void InjectionPanel::onInjectResult(const QString& ip, bool success,
                                     const QString& targetPath) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QString rowIp = m_table->item(row, COL_AGENT)->text();
        QString rowPath = m_table->item(row, COL_APP)->data(Qt::UserRole).toString();
        if (rowIp == ip && rowPath == targetPath) {
            m_table->item(row, COL_STATUS)->setText(
                success ? "✅ Injected" : "❌ Failed");
            m_table->removeCellWidget(row, COL_ACTION);
            addActionButton(row, ip, targetPath, true);
            break;
        }
    }
}

void InjectionPanel::onTccGrantResult(const QString& ip,
                                       const QString& /*bundleId*/,
                                       bool success) {
    if (!success) return;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, COL_AGENT)->text() == ip) {
            auto* tccItem = m_table->item(row, COL_TCC);
            if (tccItem) {
                tccItem->setText(tccIcon(true, true));
                tccItem->setToolTip("Screen Recording + Camera (granted)");
            }
            QString fullPath = m_table->item(row, COL_APP)->data(Qt::UserRole).toString();
            bool isInjected = m_table->item(row, COL_STATUS)->text().contains("Injected");
            m_table->removeCellWidget(row, COL_ACTION);
            addActionButton(row, ip, fullPath, isInjected);
            break;
        }
    }
}

} // namespace inferno
