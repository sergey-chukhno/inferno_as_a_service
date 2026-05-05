#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QString>

namespace inferno {

class DataStreamWidget : public QWidget {
    Q_OBJECT
public:
    explicit DataStreamWidget(QWidget* parent = nullptr);

private slots:
    void updateStream();

private:
    QLabel* m_lines[3];
    QTimer* m_timer;
    QString m_data[3];
    const int m_maxChars = 200;
};

} // namespace inferno
