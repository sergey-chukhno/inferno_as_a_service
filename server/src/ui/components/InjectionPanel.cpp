#include "../../../include/ui/components/InjectionPanel.hpp"
#include <QVBoxLayout>
#include <QHeaderView>

namespace inferno {

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
    // Simple text-based report parsing:
    // Format: "target_app|bundle_id|vector|success"
    // Example: "Discord|com.hnc.Discord|DYLD_INSERT_LIBRARIES|1"
    ensureAgentRow(ip);

    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->text() == ip) {
            QStringList parts = report.split('|');
            if (parts.size() >= 4) {
                m_table->item(row, 1)->setText(parts[0]);
                m_table->item(row, 2)->setText(parts[2]);
                m_table->item(row, 3)->setText(
                    parts[3] == "1" ? "✅ Injected" : "⚠️ Fallback Tier 1");
            } else {
                m_table->item(row, 1)->setText(report);
                m_table->item(row, 2)->setText("-");
                m_table->item(row, 3)->setText("❓ Unknown");
            }
            break;
        }
    }
}

} // namespace inferno
