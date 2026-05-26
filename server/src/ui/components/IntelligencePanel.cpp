#include "../../../include/ui/components/IntelligencePanel.hpp"
#include "../../../include/ui/StyleSheets.hpp"
#include "../../../include/database/Inferno_Database.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGuiApplication>
#include <QClipboard>
#include <QHeaderView>

namespace inferno {

IntelligencePanel::IntelligencePanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void IntelligencePanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout();
    header->addWidget(new QLabel("CLASSIFIED FORENSIC INTELLIGENCE"));
    header->addStretch();

    auto* btnScanHistory = new QPushButton("Force Scan History");
    btnScanHistory->setStyleSheet(
        "QPushButton { background: #0c0c0c; border: 1px solid #00ff41; color: #00ff41; padding: 6px 12px; font-weight: bold; border-radius: 3px; }"
        "QPushButton:hover { background: #00ff41; color: #000; }"
    );
    connect(btnScanHistory, &QPushButton::clicked, this, &IntelligencePanel::onForceScanClicked);
    header->addWidget(btnScanHistory);

    auto* btnCopyIntel = new QPushButton("Copy Finding");
    btnCopyIntel->setStyleSheet(
        "QPushButton { background: #0c0c0c; border: 1px solid #00ff41; color: #00ff41; padding: 6px 12px; font-weight: bold; border-radius: 3px; }"
        "QPushButton:hover { background: #00ff41; color: #000; }"
    );
    connect(btnCopyIntel, &QPushButton::clicked, this, &IntelligencePanel::copySelectedIntel);
    header->addWidget(btnCopyIntel);

    auto* btnClearIntel = new QPushButton("Clear Findings");
    btnClearIntel->setStyleSheet(
        "QPushButton { background: #0c0c0c; border: 1px solid #ff0000; color: #ff0000; padding: 6px 12px; font-weight: bold; border-radius: 3px; }"
        "QPushButton:hover { background: #ff0000; color: #000; }"
    );
    connect(btnClearIntel, &QPushButton::clicked, this, &IntelligencePanel::clearIntelFindings);
    header->addWidget(btnClearIntel);

    layout->addLayout(header);

    auto* filtersRow = new QHBoxLayout();
    m_intelSearchBox = new QLineEdit();
    m_intelSearchBox->setPlaceholderText("Filter findings...");
    m_intelSearchBox->setStyleSheet(ui::style::INPUT_BOX);
    connect(m_intelSearchBox, &QLineEdit::textChanged, this, &IntelligencePanel::onFilterOrSearchChanged);
    filtersRow->addWidget(m_intelSearchBox);

    m_intelTypeFilter = new QComboBox();
    m_intelTypeFilter->addItem("All Findings", "ALL");
    m_intelTypeFilter->addItem("Emails", "EMAIL");
    m_intelTypeFilter->addItem("Phone Numbers", "PHONE");
    m_intelTypeFilter->addItem("Credit Cards", "CREDIT_CARD");
    m_intelTypeFilter->addItem("Passwords", "PASSWORD");
    m_intelTypeFilter->setStyleSheet(ui::style::COMBO_BOX);
    connect(m_intelTypeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &IntelligencePanel::onFilterOrSearchChanged);
    filtersRow->addWidget(m_intelTypeFilter);

    layout->addLayout(filtersRow);

    m_intelTable = new QTableWidget(0, 4, this);
    m_intelTable->setHorizontalHeaderLabels({"TYPE", "VALUE", "CONTEXT", "TIMESTAMP"});
    m_intelTable->setStyleSheet(ui::style::INTEL_TABLE);
    m_intelTable->horizontalHeader()->setStretchLastSection(true);
    m_intelTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_intelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_intelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_intelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_intelTable);
}

void IntelligencePanel::loadIntelligenceList(const QString& uuid) {
    m_activeUuid = uuid;
    m_intelTable->setRowCount(0);
    if (uuid.isEmpty()) return;

    QString filterType = m_intelTypeFilter->currentData().toString();
    QString searchVal = m_intelSearchBox->text().trimmed();

    QList<IntelEntry> list = Inferno_Database::instance().getIntelligence(uuid, filterType);
    
    int row = 0;
    for (const auto& entry : list) {
        if (!searchVal.isEmpty() && 
            !entry.value.contains(searchVal, Qt::CaseInsensitive) && 
            !entry.context.contains(searchVal, Qt::CaseInsensitive)) {
            continue;
        }

        m_intelTable->insertRow(row);
        
        auto* itemType = new QTableWidgetItem(entry.dataType);
        auto* itemValue = new QTableWidgetItem(entry.value);
        auto* itemContext = new QTableWidgetItem(entry.context);
        auto* itemTime = new QTableWidgetItem(entry.timestamp);

        itemType->setForeground(QColor(0, 255, 65));
        itemValue->setForeground(Qt::white);
        itemContext->setForeground(QColor(170, 170, 170));
        itemTime->setForeground(QColor(120, 120, 120));

        m_intelTable->setItem(row, 0, itemType);
        m_intelTable->setItem(row, 1, itemValue);
        m_intelTable->setItem(row, 2, itemContext);
        m_intelTable->setItem(row, 3, itemTime);
        
        row++;
    }
}

void IntelligencePanel::onFilterOrSearchChanged() {
    if (!m_activeUuid.isEmpty()) {
        loadIntelligenceList(m_activeUuid);
    }
}

void IntelligencePanel::copySelectedIntel() {
    int row = m_intelTable->currentRow();
    if (row < 0) return;
    
    QTableWidgetItem* valItem = m_intelTable->item(row, 1);
    if (!valItem) return;
    
    QGuiApplication::clipboard()->setText(valItem->text());
    emit statusMessage("Copied to clipboard: " + valItem->text());
}

void IntelligencePanel::clearIntelFindings() {
    if (m_activeUuid.isEmpty()) return;

    if (Inferno_Database::instance().clearIntelligence(m_activeUuid)) {
        loadIntelligenceList(m_activeUuid);
        emit statusMessage("Cleared intelligence findings");
    }
}

void IntelligencePanel::onForceScanClicked() {
    if (!m_activeUuid.isEmpty()) {
        emit forceScanRequested(m_activeUuid);
    } else {
        emit statusMessage("No agent selected to perform scan");
    }
}

} // namespace inferno
