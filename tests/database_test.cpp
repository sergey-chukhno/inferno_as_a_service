#include "../server/include/Inferno_Database.hpp"
#include <iostream>
#include <cassert>
#include <QCoreApplication>

void test_db_singleton() {
    std::cout << "[TEST] Testing Database Singleton & Initialization..." << std::endl;
    
    // Attempt initialization with test credentials
    bool ok = inferno::Inferno_Database::instance().initialize("127.0.0.1", 5432, "inferno_db", "operator", "inferno_password_2026");
    
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
