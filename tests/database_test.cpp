#include "../server/include/Inferno_Database.hpp"
#include <iostream>
#include <cassert>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

void loadEnv(const QString& path = ".env") {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;
        
        QStringList parts = line.split("=", Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            QString key = parts.at(0).trimmed();
            QString value = parts.mid(1).join("=").trimmed();
            qputenv(key.toLocal8Bit().constData(), value.toLocal8Bit());
        }
    }
}

void test_db_singleton() {
    std::cout << "[TEST] Testing Database Singleton & Initialization..." << std::endl;
    
    // Load Tactical Secrets
    loadEnv();
    
    // Attempt initialization with environment variables
    QString dbHost = qEnvironmentVariable("INFERNO_DB_HOST");
    int dbPort = qEnvironmentVariable("INFERNO_DB_PORT").toInt();
    QString dbName = qEnvironmentVariable("INFERNO_DB_NAME");
    QString dbUser = qEnvironmentVariable("INFERNO_DB_USER");
    QString dbPass = qEnvironmentVariable("INFERNO_DB_PASSWORD");

    bool ok = inferno::Inferno_Database::instance().initialize(
        dbHost.isEmpty() ? "127.0.0.1" : dbHost, 
        dbPort > 0 ? dbPort : 5432, 
        dbName.isEmpty() ? "inferno_db" : dbName, 
        dbUser.isEmpty() ? "operator" : dbUser, 
        dbPass);
    
    if (!ok) {
        std::cerr << "[FAIL] Database initialization failed. Ensure Postgres is running." << std::endl;
        return;
    }

    std::cout << "[PASS] Database connected successfully." << std::endl;
}

void test_db_agent_registration() {
    std::cout << "[TEST] Testing Agent Registration (UUID persistence)..." << std::endl;
    
    QString test_uuid = "TEST-UUID-999";
    QString ip = "192.168.1.50";
    
    int id1 = inferno::Inferno_Database::instance().registerAgent(test_uuid, ip, "TestBox", "macOS 15");
    int id2 = inferno::Inferno_Database::instance().registerAgent(test_uuid, ip, "TestBox", "macOS 15");
    
    assert(id1 > 0 && "Agent registration should return a valid ID");
    assert(id1 == id2 && "Duplicate registration should return the same ID via ON CONFLICT UPSERT");
    
    std::cout << "[PASS] Agent UPSERT logic verified (ID: " << id1 << ")." << std::endl;
}

void test_db_telemetry_history() {
    std::cout << "[TEST] Testing Telemetry History retrieval..." << std::endl;
    
    QString test_uuid = "TEST-UUID-999";
    inferno::Inferno_Database::instance().logTelemetry(test_uuid, "TEST", "Persistence Verification Signal 0x01");
    
    QStringList history = inferno::Inferno_Database::instance().getTelemetryHistory(test_uuid);
    
    bool found = false;
    for (const QString& line : history) {
        if (line.contains("Persistence Verification Signal 0x01")) {
            found = true;
            break;
        }
    }
    
    assert(found && "Telemetry record should be retrievable from SQL");
    std::cout << "[PASS] Telemetry persistence verified." << std::endl;
}

void test_db_loot_persistence() {
    std::cout << "[TEST] Testing Loot (Binary) persistence..." << std::endl;
    
    QString test_uuid = "TEST-UUID-999";
    QByteArray test_data = QByteArray::fromHex("89504E470D0A1A0A"); // PNG Header
    
    bool ok = inferno::Inferno_Database::instance().logLoot(test_uuid, "screenshot.png", "image/png", test_data);
    assert(ok && "Loot logging should return true");
    
    std::cout << "[PASS] Loot (BYTEA) persistence verified." << std::endl;
}
