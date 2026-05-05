#include <QApplication>
#include <QThread>
#include "../include/server.hpp"
#include "../include/MainWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

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