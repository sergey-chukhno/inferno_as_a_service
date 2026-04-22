#include "../include/server.hpp"
#include <iostream>
#include <algorithm> // To use std::remove_if
#include "../../common/include/Packet.hpp"

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
    std::cout << "[Server] Listening on port " << m_port << std::endl;
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
            FD_SET(client.socket.getFd(), &read_fds);
            if (client.socket.getFd() > max_fd)
                max_fd = client.socket.getFd();
        }

        // Call select()
        struct timeval timeout;
        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;

        int activity = ::select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

        if (activity < 0) {
            std::cerr << "[Server] select() error\n";
            break;
        }

        // New client connection
        if (FD_ISSET(m_listen_socket.getFd(), &read_fds)) {
            auto new_socket = m_listen_socket.acceptNode();
            if (new_socket.has_value()) {
                std::cout << "[Server] New agent connected: "
                          << new_socket->getIp() << ":"
                          << new_socket->getPort() << std::endl;
                
                // Immediately request system info (Phase 3 requirement)
                Packet req(static_cast<uint16_t>(Opcode::SYS_REQ_INFO), "");
                std::vector<uint8_t> data = req.serialize();
                new_socket->sendData(data);

                m_clients.push_back({std::move(*new_socket), {}});
            }
        }

        // Existing client data
        std::vector<int> to_remove;
        for (auto& client : m_clients) {
            if (FD_ISSET(client.socket.getFd(), &read_fds)) {
                std::vector<uint8_t> raw_chunk;
                ssize_t bytes = client.socket.receiveData(raw_chunk, 4096);

                if (bytes <= 0) {
                    std::cout << "[Server] Agent " << client.socket.getIp() << " disconnected" << std::endl;
                    to_remove.push_back(client.socket.getFd());
                } else {
                    // Accumulate data
                    client.buffer.insert(client.buffer.end(), raw_chunk.begin(), raw_chunk.end());
                    
                    // Option B: Offload processing to a dedicated method
                    processPacketBuffer(client);
                }
            }
        }

        // Clean up disconnected clients
        m_clients.erase(
            std::remove_if(m_clients.begin(), m_clients.end(),
                [&to_remove](const ClientContext& ctx) {
                    return std::find(to_remove.begin(), to_remove.end(), ctx.socket.getFd())
                           != to_remove.end();
                }),
            m_clients.end()
        );
    }
}

void Server::stop() {
    m_running = false;
    m_clients.clear(); 
}

// Getters

bool     Server::isRunning() const { return m_running; }
uint16_t Server::getPort()   const { return m_port; }

// Static Protocol Helper (Option B)
void Server::processPacketBuffer(ClientContext& client) {
    while (true) {
        auto packet_opt = ::inferno::Packet::deserialize(client.buffer);
        if (!packet_opt.has_value()) {
            break; // Not enough data for a full packet yet
        }

        // Dispatch Packet
        uint16_t opcode = packet_opt->getOpcode();
        const auto& payload = packet_opt->getPayload();
        std::string payload_str(payload.begin(), payload.end());

        if (opcode == static_cast<uint16_t>(Opcode::SYS_RES_INFO)) {
            std::cout << "[Server] [INFO] Agent " << client.socket.getIp() 
                      << " Specs: " << payload_str << std::endl;
        } else {
            std::cout << "[Server] Received Opcode " << opcode 
                      << " from " << client.socket.getIp() << std::endl;
        }

        // Remove processed bytes from the buffer
        size_t packet_size = sizeof(PacketHeader) + payload.size();
        client.buffer.erase(client.buffer.begin(), client.buffer.begin() + packet_size);
    }
}

} // namespace inferno