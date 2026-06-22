#include "../../../include/ui/components/InjectionPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileInfo>

namespace inferno {

static QString capabilityName(int cap) {
    switch (cap) {
        case 0: return "NONE";
        case 1: return "DYLD_INSERT_LIBRARIES";
        case 2: return "MACH_VM_ALLOCATE";
        case 3: return "DYLIB_PROXYING";
        default: return QString::number(cap);
    }
}

InjectionPanel::InjectionPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({
        "Agent", "Target App", "Vector", "Status"
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setStyleSheet(ui::style::INTEL_TABLE);
    layout->addWidget(m_table);

    // Inject button row
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_injectBtn = new QPushButton("Inject");
    m_injectBtn->setEnabled(false);
    m_injectBtn->setToolTip("Select a Ready target and click to inject the agent");
    btnLayout->addWidget(m_injectBtn);
    layout->addLayout(btnLayout);

    // Enable/disable button based on selection
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &InjectionPanel::updateInjectButtonState);

    // Emit injectRequested on button click
    connect(m_injectBtn, &QPushButton::clicked, this, [this]() {
        int row = m_table->currentRow();
        if (row < 0) return;
        QString ip = m_table->item(row, 0)->text();
        // Reconstruct full path from the stored display data — we only have baseName
        // Store the full path as item data in column 1
        QString fullPath = m_table->item(row, 1)->data(Qt::UserRole).toString();
        if (!fullPath.isEmpty()) {
            emit injectRequested(ip, fullPath);
        }
    });
}

void InjectionPanel::updateInjectButtonState() {
    int row = m_table->currentRow();
    if (row < 0) {
        m_injectBtn->setEnabled(false);
        return;
    }
    QTableWidgetItem* statusItem = m_table->item(row, 3);
    bool isReady = statusItem && statusItem->text() == "Ready";
    m_injectBtn->setEnabled(isReady);
}

void InjectionPanel::onScanResult(const QString& ip, const QString& report) {
    // Remove existing rows for this agent
    for (int row = m_table->rowCount() - 1; row >= 0; --row) {
        if (m_table->item(row, 0)->text() == ip) {
            m_table->removeRow(row);
        }
    }

    QStringList lines = report.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty() || (lines.size() == 1 && lines[0].startsWith("none"))) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(ip));
        m_table->setItem(row, 1, new QTableWidgetItem("(none)"));
        m_table->setItem(row, 2, new QTableWidgetItem("-"));
        m_table->setItem(row, 3, new QTableWidgetItem("No injectable apps"));
        updateInjectButtonState();
        return;
    }

    for (const QString& line : lines) {
        QStringList parts = line.split('|');
        if (parts.size() < 4) continue;

        QString fullPath = parts[0];
        QFileInfo fi(fullPath);
        QString appName = fi.baseName();
        int cap = parts[2].toInt();
        bool isInjected = parts[3] == "1";

        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(ip));

        auto* nameItem = new QTableWidgetItem(appName);
        nameItem->setData(Qt::UserRole, fullPath);
        m_table->setItem(row, 1, nameItem);

        m_table->setItem(row, 2, new QTableWidgetItem(capabilityName(cap)));
        m_table->setItem(row, 3, new QTableWidgetItem(
            isInjected ? "✅ Injected" : "Ready"));
    }
    updateInjectButtonState();
}

void InjectionPanel::onInjectResult(const QString& ip, bool success, const QString& targetPath) {
    // Find the row for this agent+target and update its status
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QString rowIp = m_table->item(row, 0)->text();
        QString rowPath = m_table->item(row, 1)->data(Qt::UserRole).toString();
        if (rowIp == ip && rowPath == targetPath) {
            m_table->item(row, 3)->setText(success ? "✅ Injected" : "❌ Failed");
            break;
        }
    }
    updateInjectButtonState();
}

} // namespace inferno
