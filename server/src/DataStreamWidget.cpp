#include "../include/DataStreamWidget.hpp"
#include <QHBoxLayout>
#include <QRandomGenerator>

namespace inferno {

DataStreamWidget::DataStreamWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(0);
    setFixedHeight(60);

    for(int i=0; i < 3; ++i) {
        m_lines[i] = new QLabel(this);
        m_lines[i]->setStyleSheet("color: rgba(0, 255, 65, 80); font-family: 'Courier New'; font-size: 9px;");
        layout->addWidget(m_lines[i]);
        
        // Initial random data
        for(int j=0; j < m_maxChars; ++j) {
            m_data[i].append(QRandomGenerator::global()->bounded(2) ? "1" : "0");
        }
        m_lines[i]->setText(m_data[i]);
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DataStreamWidget::updateStream);
    m_timer->start(100); 
}

void DataStreamWidget::updateStream() {
    for(int i=0; i < 3; ++i) {
        // Different shift speeds/amounts
        int shift = (i == 1) ? 2 : 4;
        m_data[i].remove(0, shift);
        
        for(int j=0; j < shift; ++j) {
            int r = QRandomGenerator::global()->bounded(20);
            if (r < 15) {
                m_data[i].append(QRandomGenerator::global()->bounded(2) ? "1" : "0");
            } else if (r < 18) {
                m_data[i].append(" ");
            } else {
                m_data[i].append(QString("0x%1").arg(QRandomGenerator::global()->bounded(256), 2, 16, QChar('0')).toUpper());
            }
        }

        if (m_data[i].length() > m_maxChars) {
            m_data[i] = m_data[i].right(m_maxChars);
        }
        m_lines[i]->setText(m_data[i]);
    }
}

} // namespace inferno
