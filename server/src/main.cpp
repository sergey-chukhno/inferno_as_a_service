#include <QApplication>
#include <QThread>
#include <QDebug>
#include "../include/server.hpp"
#include "../include/MainWindow.hpp"
#include "../include/Inferno_Database.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Initialize Database Persistence (Circle 5)
    if (!inferno::Inferno_Database::instance().initialize("127.0.0.1", 5432, "inferno_db", "operator", "inferno_password_2026")) {
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