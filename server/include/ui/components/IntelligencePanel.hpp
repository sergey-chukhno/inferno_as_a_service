#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

namespace inferno {

class IntelligencePanel : public QWidget {
    Q_OBJECT

public:
    explicit IntelligencePanel(QWidget* parent = nullptr);
    ~IntelligencePanel() override = default;

    void loadIntelligenceList(const QString& uuid);

signals:
    void forceScanRequested(const QString& uuid);
    void statusMessage(const QString& msg);

private slots:
    void onFilterOrSearchChanged();
    void copySelectedIntel();
    void clearIntelFindings();
    void onForceScanClicked();

private:
    void setupUI();

    QTableWidget*   m_intelTable;
    QComboBox*      m_intelTypeFilter;
    QLineEdit*      m_intelSearchBox;
    
    QString         m_activeUuid;
};

} // namespace inferno
