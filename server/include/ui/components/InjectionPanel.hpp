#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QString>
#include <QPushButton>

namespace inferno {

class InjectionPanel : public QWidget {
    Q_OBJECT
public:
    explicit InjectionPanel(QWidget* parent = nullptr);

public slots:
    void onScanResult(const QString& ip, const QString& report);
    void onInjectResult(const QString& ip, bool success, const QString& targetPath);

signals:
    void injectRequested(const QString& ip, const QString& targetPath);

private:
    void updateInjectButtonState();

    QTableWidget* m_table;
    QPushButton*  m_injectBtn;
};

} // namespace inferno
