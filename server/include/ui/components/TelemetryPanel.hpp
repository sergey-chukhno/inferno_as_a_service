#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QStringList>
#include <QTimer>

namespace inferno {

class TelemetryPanel : public QWidget {
    Q_OBJECT

public:
    explicit TelemetryPanel(QWidget* parent = nullptr);
    ~TelemetryPanel() override = default;

    void appendText(const QString& text);
    void clearConsole();
    void loadHistory(const QString& uuid);

signals:
    void shellRequested();
    void processesRequested();
    void statusMessage(const QString& msg);

private slots:
    void onSearchTextChanged();
    void onFilterChanged();
    void highlightSearchMatches();

private:
    void setupUI();
    void applyVisualHighlighting();

    QPlainTextEdit* m_console;
    QLineEdit*      m_searchBox;
    QComboBox*      m_typeFilter;
    QTimer*         m_searchDebounceTimer;

    QStringList     m_history;
    QString         m_activeUuid;
};

} // namespace inferno
