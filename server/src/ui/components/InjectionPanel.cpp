#include "../../../include/ui/components/InjectionPanel.hpp"
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
    layout->addWidget(m_table);
}

void InjectionPanel::ensureAgentRow(const QString& ip) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->text() == ip) {
            return;
        }
    }
    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(ip));
    m_table->setItem(row, 1, new QTableWidgetItem("-"));
    m_table->setItem(row, 2, new QTableWidgetItem("-"));
    m_table->setItem(row, 3, new QTableWidgetItem("-"));
}

void InjectionPanel::onScanResult(const QString& ip, const QString& report) {
    ensureAgentRow(ip);

    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->text() == ip) {
            QStringList parts = report.split('|');
            if (parts.size() >= 4 && parts[0] != "none") {
                QFileInfo fi(parts[0]);
                QString appName = fi.baseName();
                int cap = parts[2].toInt();
                m_table->item(row, 1)->setText(appName);
                m_table->item(row, 2)->setText(capabilityName(cap));
                m_table->item(row, 3)->setText(
                    parts[3] == "1" ? "✅ Injected" : "⚠️ Fallback Tier 1");
            } else {
                m_table->item(row, 1)->setText("(none)");
                m_table->item(row, 2)->setText("-");
                m_table->item(row, 3)->setText("No injectable apps");
            }
            break;
        }
    }
}

} // namespace inferno
