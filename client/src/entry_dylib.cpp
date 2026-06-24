#include "../include/Agent.hpp"
#include "../include/entry_dylib.hpp"
#include "../../common/include/CryptoContext.hpp"
#include <cerrno>
#include <cstdlib>
#include <thread>
#include <atomic>

namespace inferno { namespace agent {

#ifdef INFERNO_TESTING
static bool& constructorRan() {
    static bool ran = false;
    return ran;
}
bool didAgentConstructorRun() { return constructorRan(); }
#endif

namespace {
    std::thread g_agent_thread;
    std::atomic<bool> g_shutdown{false};
}

bool isDylibShuttingDown() {
    return g_shutdown.load();
}

}} // namespace inferno::agent

#if defined(INFERNO_DYLIB) || defined(INFERNO_TESTING)

extern "C" int inferno_agent_entry_ran = 0;

__attribute__((destructor))
static void agent_exit() {
    inferno::agent::g_shutdown.store(true);
    if (inferno::agent::g_agent_thread.joinable()) {
        inferno::agent::g_agent_thread.join();
    }
}

__attribute__((constructor))
static void agent_entry() {
    inferno_agent_entry_ran = 1;
#ifdef INFERNO_TESTING
    inferno::agent::constructorRan() = true;
    return;
#else
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

    inferno::agent::g_agent_thread = std::thread([ip, port]() {
        inferno::Agent agent(ip, port);
        agent.run();
    });
#endif
}

#endif
