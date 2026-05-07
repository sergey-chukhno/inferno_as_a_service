#include <QApplication>
#include <QThread>
#include <QDebug>
#include "../include/server.hpp"
#include "../include/MainWindow.hpp"
#include "../include/Inferno_Database.hpp"

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

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    
    // Load Tactical Secrets (Circle 5)
    loadEnv();

    // Initialize Database Persistence (Circle 5)
    QString dbHost = qEnvironmentVariable("INFERNO_DB_HOST");
    int dbPort = qEnvironmentVariable("INFERNO_DB_PORT").toInt();
    QString dbName = qEnvironmentVariable("INFERNO_DB_NAME");
    QString dbUser = qEnvironmentVariable("INFERNO_DB_USER");
    QString dbPass = qEnvironmentVariable("INFERNO_DB_PASSWORD");

    // Strict OPSEC Check: Require database credentials
    if (dbPass.isEmpty()) {
        qDebug() << "[Database] CRITICAL ERROR: Database credentials NOT found in environment or .env file!";
        qDebug() << "[Database] Please copy .env.example to .env and configure your secrets.";
    }

    if (!inferno::Inferno_Database::instance().initialize(
            dbHost.isEmpty() ? "127.0.0.1" : dbHost, 
            dbPort > 0 ? dbPort : 5432, 
            dbName.isEmpty() ? "inferno_db" : dbName, 
            dbUser.isEmpty() ? "operator" : dbUser, 
            dbPass)) {
        qDebug() << "[Server] WARNING: Continuing WITHOUT persistence. Database is offline.";
    }

    uint16_t port = 4242;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    // 1. Create Server and UI
    inferno::Server server(port);
    inferno::MainWindow window(&server);

    // 2. Move Server to a Worker Thread
    QThread workerThread;
    server.moveToThread(&workerThread);

    // 3. Connect Thread Lifecycle
    QObject::connect(&workerThread, &QThread::started, &server, &inferno::Server::run);
    QObject::connect(&app, &QApplication::aboutToQuit, &server, &inferno::Server::stop);
    QObject::connect(&server, &inferno::Server::statusMessage, [](const QString& msg){
        (void)msg; // Ready for more complex logging if needed
    });

    // 4. Start Server
    if (!server.start()) {
        return 1;
    }
    
    workerThread.start();
    window.show();

    int result = app.exec();

    // Cleanup
    workerThread.quit();
    workerThread.wait();

    return result;
}