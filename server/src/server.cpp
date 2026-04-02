#include "../include/server.hpp"
#include <iostream>
#include <algorithm> // To use std::remove_if

namespace inferno {


Server::Server() : m_port(0), m_running(false) {}

Server::Server(uint16_t port) : m_port(port), m_running(false) {}

Server::~Server() {
    stop();
}

// Core functionalities

bool Server::start() {
    // Bind the listening socket to the specified IP and port
    if (!m_listen_socket.bindNode("0.0.0.0", m_port)) {
        std::cerr << "[Server] bindNode() failed on port " << m_port << "\n";
        return false;
    }

    // Put the listening socket in listening mode
    if (!m_listen_socket.listen()) {
        std::cerr << "[Server] listen() failed\n";
        return false;
    }

    m_running = true;
    std::cout << "[Server] Listening on port " << m_port << "\n";
    return true;
}

void Server::run() {
    if (!m_running) {
        std::cerr << "[Server] Call start() before run()\n";
        return;
    }

    while (m_running) {

        // Build the fd_set
        // fd_set is the set of file descriptors that select() will monitor.
        // We rebuild it at each turn because select() modifies it.

        fd_set read_fds;
        FD_ZERO(&read_fds); // Clear the set

        // Add the listening socket to the set
        FD_SET(m_listen_socket.getFd(), &read_fds);
        int max_fd = m_listen_socket.getFd(); // select() needs the largest fd

        // Add all connected clients to the set
        for (const auto& client : m_clients) {
            FD_SET(client.getFd(), &read_fds);
            if (client.getFd() > max_fd)
                max_fd = client.getFd();
        }

        // Call select()
        // select() blocks until at least one fd is ready to read.
        // Arguments: max_fd + 1, fd_set read, fd_set write, fd_set error, timeout
        // We pass nullptr for write/error and timeout for 1 second to avoid blocking indefinitely.
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = ::select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

        if (activity < 0) {
            std::cerr << "[Server] select() error\n";
            break;
        }

        // For new client connection
        // FD_ISSET checks if a specific fd is "ready" in the fd_set.

        if (FD_ISSET(m_listen_socket.getFd(), &read_fds)) {
            auto new_client = m_listen_socket.acceptNode();
            if (new_client.has_value()) {
                std::cout << "[Server] New client connected: "
                          << new_client->getIp() << ":"
                          << new_client->getPort() << "\n";
                m_clients.push_back(std::move(*new_client));
            }
        }

        // For existing client data
        // We iterate over the clients and check each one.
        // We collect the disconnected fds to remove them after the iteration.

        std::vector<int> to_remove;

        for (auto& client : m_clients) {
            if (FD_ISSET(client.getFd(), &read_fds)) {
                std::vector<uint8_t> buffer;
                ssize_t bytes = client.receiveData(buffer, 4096);

                if (bytes <= 0) {
                    // 0 = clean disconnect, <0 = error
                    std::cout << "[Server] Client " << client.getIp() << " disconnected\n";
                    to_remove.push_back(client.getFd());
                } else {
                    // For now: display what we receive (debug)
                    std::string msg(buffer.begin(), buffer.end());
                    std::cout << "[Server] Received from " << client.getIp()
                              << ": " << msg << "\n";
                }
            }
        }

        // Clean up disconnected clients
        // We remove the sockets whose fd is in to_remove from the vector.
        // The Socket is destroyed → its destructor closes the fd automatically.

        m_clients.erase(
            std::remove_if(m_clients.begin(), m_clients.end(),
                [&to_remove](const Socket& s) {
                    return std::find(to_remove.begin(), to_remove.end(), s.getFd())
                           != to_remove.end();
                }),
            m_clients.end()
        );
    }
}

void Server::stop() {
    m_running = false;
    m_clients.clear(); 
    // m_listen_socket is destroyed by its own destructor
}

// Getters

bool     Server::isRunning() const { return m_running; }
uint16_t Server::getPort()   const { return m_port; }

} // namespace inferno