#include "../../../include/ui/components/InjectionPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include <QVBoxLayout>
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
        // No injectable apps
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(ip));
        m_table->setItem(row, 1, new QTableWidgetItem("(none)"));
        m_table->setItem(row, 2, new QTableWidgetItem("-"));
        m_table->setItem(row, 3, new QTableWidgetItem("No injectable apps"));
        return;
    }

    for (const QString& line : lines) {
        QStringList parts = line.split('|');
        if (parts.size() < 4) continue;

        QFileInfo fi(parts[0]);
        QString appName = fi.baseName();
        int cap = parts[2].toInt();
        bool isInjected = parts[3] == "1";

        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(ip));
        m_table->setItem(row, 1, new QTableWidgetItem(appName));
        m_table->setItem(row, 2, new QTableWidgetItem(capabilityName(cap)));
        m_table->setItem(row, 3, new QTableWidgetItem(
            isInjected ? "✅ Injected" : "Ready"));
    }
}

} // namespace inferno
