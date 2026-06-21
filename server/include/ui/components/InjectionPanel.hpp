#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QString>

namespace inferno {

class InjectionPanel : public QWidget {
    Q_OBJECT
public:
    explicit InjectionPanel(QWidget* parent = nullptr);

public slots:
    void onScanResult(const QString& ip, const QString& report);

private:
    QTableWidget* m_table;
    void ensureAgentRow(const QString& ip);
};

} // namespace inferno
