#include "../../../include/ui/components/InjectionPanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileInfo>
#include <QPushButton>

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

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({
        "Agent", "Target App", "Vector", "Status", "Action"
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->setColumnWidth(4, 110);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setStyleSheet(ui::style::INTEL_TABLE);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    layout->addWidget(m_table);
}

void InjectionPanel::addActionButton(int row, const QString& ip, const QString& fullPath, bool isInjected) {
    auto* btn = new QPushButton(isInjected ? "Injected" : "Inject");
    btn->setEnabled(!isInjected);

    if (isInjected) {
        btn->setFixedSize(95, 34);
        btn->setStyleSheet(
            "QPushButton { background: #333; color: #666; border: 1px solid #444; "
            "font-size: 13px; border-radius: 4px; }");
    } else {
        btn->setFixedSize(95, 34);
        btn->setStyleSheet(
            "QPushButton { background: #004d00; color: #00ff41; border: 2px solid #00ff41; "
            "font-size: 13px; border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background: #00ff41; color: #000; }");
    }

    connect(btn, &QPushButton::clicked, this, [this, ip, fullPath]() {
        emit injectRequested(ip, fullPath);
    });

    m_table->setCellWidget(row, 4, btn);
}

void InjectionPanel::onScanResult(const QString& ip, const QString& report) {
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

        addActionButton(row, ip, fullPath, isInjected);
    }
}

void InjectionPanel::onInjectResult(const QString& ip, bool success, const QString& targetPath) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QString rowIp = m_table->item(row, 0)->text();
        QString rowPath = m_table->item(row, 1)->data(Qt::UserRole).toString();
        if (rowIp == ip && rowPath == targetPath) {
            m_table->item(row, 3)->setText(success ? "✅ Injected" : "❌ Failed");
            // Replace action button with disabled one
            m_table->removeCellWidget(row, 4);
            addActionButton(row, ip, targetPath, true);
            break;
        }
    }
}

} // namespace inferno
