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
    void onInjectResult(const QString& ip, bool success, const QString& targetPath);
    void onTccGrantResult(const QString& ip, const QString& bundleId, bool success);

signals:
    void injectRequested(const QString& ip, const QString& targetPath);
    void tccGrantRequested(const QString& ip, const QString& bundleId);

private:
    void addActionButton(int row, const QString& ip, const QString& fullPath, bool isInjected);
    QString tccIcon(bool hasScreenRecording, bool hasCamera) const;

    QTableWidget* m_table;
};

} // namespace inferno
