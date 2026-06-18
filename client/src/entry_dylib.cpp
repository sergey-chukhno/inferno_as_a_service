#include "../include/Agent.hpp"
#include "../include/entry_dylib.hpp"
#include <cstdlib>
#include <thread>

namespace inferno { namespace agent {

#ifdef INFERNO_TESTING
static bool& constructorRan() {
    static bool ran = false;
    return ran;
}
bool didAgentConstructorRun() { return constructorRan(); }
#endif

}} // namespace inferno::agent

__attribute__((constructor))
static void agent_entry() {
#ifdef INFERNO_TESTING
    inferno::agent::constructorRan() = true;
    return;
#endif

    const char* env_ip = ::getenv("INFERNO_SERVER_IP");
    const char* env_port = ::getenv("INFERNO_SERVER_PORT");
    std::string ip = env_ip ? env_ip : "127.0.0.1";
    uint16_t port = env_port
        ? static_cast<uint16_t>(std::atoi(env_port))
        : 8080;

    std::thread agent_thread([ip, port]() {
        inferno::Agent agent(ip, port);
        agent.run();
    });
    agent_thread.detach();
}
