#pragma once

#include <QObject>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QString>
#include <QDateTime>
#include <QDebug>

#include <QStringList>
#include <QDateTime>

namespace inferno {

struct AgentProfile {
    QString uuid;
    QString ip;
    QString hostname;
    QString osInfo;
    QDateTime firstSeen;
    QDateTime lastSeen;
    bool isOnline = false;
};

struct LootEntry {
    int id = 0;
    QString agentUuid;
    QString filename;
    QString fileType;
    QByteArray content;
    QString timestamp;
};

class Inferno_Database : public QObject {
    Q_OBJECT

public:
    static Inferno_Database& instance();
    
    bool initialize(const QString& host, int port, const QString& dbName, const QString& user, const QString& password);
    void close();

    // Core Intelligence Methods
    int registerAgent(const QString& uuid, const QString& ip, const QString& hostname, const QString& osInfo);
    bool logTelemetry(const QString& uuid, const QString& type, const QString& data);
    bool logKeylog(const QString& uuid, const QString& data);
    bool logLoot(const QString& uuid, const QString& filename, const QString& fileType, const QByteArray& content);
    
    // Retrieval

    // Liveness & Profiling (Circle 5 Phase II)
    void setAgentOnlineStatus(const QString& uuid, bool online);
    AgentProfile getAgentProfile(const QString& uuid);

    // Retrieval for Auditing
    QStringList getTelemetryHistory(const QString& uuid, const QString& type = "ALL", int limit = 1000);
    QStringList getKeylogHistory(const QString& uuid, int limit = 1000);

private:
    explicit Inferno_Database(QObject* parent = nullptr);
    ~Inferno_Database();
    
    Inferno_Database(const Inferno_Database&) = delete;
    Inferno_Database& operator=(const Inferno_Database&) = delete;

    bool createTables();
    int getAgentInternalId(const QString& uuid);

    QSqlDatabase m_db;
};

} // namespace inferno
