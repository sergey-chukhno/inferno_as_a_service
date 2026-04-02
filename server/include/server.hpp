#pragma once

#include "../../common/include/Socket.hpp"
#include <vector>
#include <cstdint>

namespace inferno {

class Server {
private:
    Socket              m_listen_socket; // Socket d'écoute principal
    std::vector<Socket> m_clients;       // Clients connectés
    uint16_t            m_port;
    bool                m_running;

public:
    // Coplien
    Server();
    explicit Server(uint16_t port);
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Core
    bool start();  // bind + listen
    void run();    // boucle select()
    void stop();

    // Getters
    [[nodiscard]] bool     isRunning() const;
    [[nodiscard]] uint16_t getPort()   const;
};

} // namespace inferno