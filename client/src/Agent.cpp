#include "../include/Agent.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <arpa/inet.h>
#include <algorithm>

namespace inferno {

Agent::Agent() : m_server_ip("127.0.0.1"), m_server_port(8080), m_state(AgentState::INIT), m_running(false) {}

Agent::Agent(const std::string& server_ip, uint16_t server_port)
    : m_server_ip(server_ip), m_server_port(server_port), m_state(AgentState::INIT), m_running(false) {}

Agent::~Agent() {
    stop();
}

void Agent::run() {
    m_running = true;
    while (m_running) {
        switch (m_state) {
            case AgentState::INIT:
                handleInit();
                break;
            case AgentState::CONNECTING:
                handleConnecting();
                break;
            case AgentState::CONNECTED:
                handleConnected();
                break;
            case AgentState::LISTENING:
                handleListening();
                break;
            case AgentState::DISPATCHING:
                // Handled internally in handleListening
                break;
            case AgentState::ERROR:
                std::cerr << "[Agent] Terminal Error State reached. Restarting INIT.\n";
                m_state = AgentState::INIT;
                break;
        }
    }
}

void Agent::stop() {
    m_running = false;
    // Socket will be closed by destructor or on reconnection
}

void Agent::handleInit() {
    std::cout << "[Agent] Initializing...\n";
    // Future: Setup persistence, hide console, etc.
    m_state = AgentState::CONNECTING;
}

void Agent::handleConnecting() {
    std::cout << "[Agent] Attempting to connect to " << m_server_ip << ":" << m_server_port << "..." << std::endl;
    if (m_socket.connectTo(m_server_ip, m_server_port)) {
        std::cout << "[Agent] Connection established." << std::endl;
        m_state = AgentState::CONNECTED;
    } else {
        std::cerr << "[Agent] Connection failed. Retrying in 5 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void Agent::handleConnected() {
    // Transition to LISTENING. The Server will request info via SYS_REQ_INFO.
    m_state = AgentState::LISTENING;
}

void Agent::handleListening() {
    std::vector<uint8_t> buffer;
    ssize_t bytes = m_socket.receiveData(buffer, 4096);

    if (bytes <= 0) {
        std::cerr << "[Agent] Connection lost.\n";
        m_state = AgentState::CONNECTING;
        return;
    }

    // Accumulate in member buffer
    m_receive_buffer.insert(m_receive_buffer.end(), buffer.begin(), buffer.end());

    // Try to deserialize
    auto packet_opt = ::inferno::Packet::deserialize(m_receive_buffer);
    if (packet_opt.has_value()) {
        handleDispatching(std::move(*packet_opt));
        // Simple buffer cleanup (only works for 1 packet at a time right now)
        // Future: Improve to handle multiple packets/partial packets properly
        m_receive_buffer.clear(); 
    }
}

void Agent::handleDispatching(Packet&& packet) {
    uint16_t opcode = packet.getOpcode();
    std::cout << "[Agent] Dispatching Opcode: " << opcode << std::endl;

    if (opcode == static_cast<uint16_t>(Opcode::SYS_REQ_INFO)) {
        std::string info = getSystemInfo();
        Packet res(static_cast<uint16_t>(Opcode::SYS_RES_INFO), info);
        std::vector<uint8_t> data = res.serialize();
        m_socket.sendData(data);
    } else if (opcode == static_cast<uint16_t>(Opcode::PROC_LIST_REQ)) {
        handleProcessDiscovery();
    } else if (opcode == static_cast<uint16_t>(Opcode::PING)) {
        Packet pong(static_cast<uint16_t>(Opcode::PONG), "");
        std::vector<uint8_t> data = pong.serialize();
        m_socket.sendData(data);
    }
}

void Agent::handleProcessDiscovery() {
    auto list = m_profiler.getSnapshot();
    
    // Chunking parameters
    const size_t entries_per_page = 50;
    size_t total_pages = (list.size() + entries_per_page - 1) / entries_per_page;

    for (size_t p = 0; p < total_pages; p++) {
        std::vector<uint8_t> payload;
        
        // 1. Header of the page
        uint16_t page_index = htons(static_cast<uint16_t>(p));
        uint8_t is_last = (p == total_pages - 1) ? 1 : 0;
        
        size_t start = p * entries_per_page;
        size_t end = std::min(start + entries_per_page, list.size());
        uint16_t entries_in_page = htons(static_cast<uint16_t>(end - start));

        // Push Page Index (2 bytes)
        payload.push_back(page_index & 0xFF);
        payload.push_back((page_index >> 8) & 0xFF);
        
        // Push Is Last flag (1 byte)
        payload.push_back(is_last);
        
        // Push Entries count (2 bytes)
        payload.push_back(entries_in_page & 0xFF);
        payload.push_back((entries_in_page >> 8) & 0xFF);

        // 2. Add entries
        for (size_t i = start; i < end; i++) {
            uint32_t pid = htonl(list[i].pid);
            uint16_t name_len = htons(static_cast<uint16_t>(list[i].name.length()));

            // PID
            uint8_t* pid_ptr = reinterpret_cast<uint8_t*>(&pid);
            payload.insert(payload.end(), pid_ptr, pid_ptr + 4);

            // Name Length
            uint8_t* len_ptr = reinterpret_cast<uint8_t*>(&name_len);
            payload.insert(payload.end(), len_ptr, len_ptr + 2);

            // Name
            payload.insert(payload.end(), list[i].name.begin(), list[i].name.end());
        }

        Packet res(static_cast<uint16_t>(Opcode::PROC_LIST_RES), 
                  std::string(payload.begin(), payload.end()));
        
        m_socket.sendData(res.serialize());
    }
}

std::string Agent::getSystemInfo() {
    std::stringstream ss;
    ss << "Host: " << getHostname() << " | ";
    ss << "User: " << getUsername() << " | ";
    ss << "OS: " << getOSVersion();
    return ss.str();
}

std::string Agent::getHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "Unknown";
}

std::string Agent::getUsername() {
    char username[256];
    if (getlogin_r(username, sizeof(username)) == 0) {
        return std::string(username);
    }
    // Fallback
    struct passwd* pw = getpwuid(getuid());
    if (pw) {
        return std::string(pw->pw_name);
    }
    return "Unknown";
}

std::string Agent::getOSVersion() {
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        std::stringstream ss;
        ss << buffer.sysname << " " << buffer.release << " (" << buffer.machine << ")";
        return ss.str();
    }
    return "Linux/Generic";
}

} // namespace inferno
