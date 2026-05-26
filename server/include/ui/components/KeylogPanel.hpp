#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>

namespace inferno {

class KeylogPanel : public QWidget {
    Q_OBJECT

public:
    explicit KeylogPanel(QWidget* parent = nullptr);
    ~KeylogPanel() override = default;

    void appendText(const QString& text);
    void loadHistory(const QString& uuid);
    
    void setKeylogButtonChecked(bool checked);
    void setKeylogButtonEnabled(bool enabled);

signals:
    void keylogToggled(bool active);
    void statusMessage(const QString& msg);

private slots:
    void filterKeylogStream(const QString& text);

private:
    void setupUI();

    QPlainTextEdit* m_keylogStream;
    QLineEdit*      m_searchBox;
    QPushButton*    m_btnKeylog;

    QStringList     m_history;
    QString         m_activeUuid;
};

} // namespace inferno
