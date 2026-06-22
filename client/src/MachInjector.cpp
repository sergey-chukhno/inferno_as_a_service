#include "../include/MachInjector.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __APPLE__
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

namespace inferno { namespace tier2 {

#if defined(INFERNO_TESTING)
// In testing mode, skip actual fork/exec — just report success
bool injectIntoTarget(const TargetApp&, const std::string&,
                       const std::string&, uint16_t) {
    return true;
}

#elif defined(__APPLE__)

static bool launchWithDyldEnv(const TargetApp& target,
                               const std::string& dylib_path,
                               const std::string& server_ip,
                               uint16_t server_port) {
    pid_t pid = ::fork();
    if (pid < 0) {
        std::fprintf(stderr, "[MachInjector] fork() failed: %s\n",
                     std::strerror(errno));
        return false;
    }

    if (pid == 0) {
        ::setenv("DYLD_INSERT_LIBRARIES", dylib_path.c_str(), 1);
        ::setenv("INFERNO_SERVER_IP", server_ip.c_str(), 1);
        ::setenv("INFERNO_SERVER_PORT", std::to_string(server_port).c_str(), 1);
        ::unsetenv("DYLD_FORCE_FLAT_NAMESPACE");

        const char* const argv[] = {target.executable_path.c_str(), nullptr};
        ::execv(target.executable_path.c_str(),
                const_cast<char* const*>(argv));

        std::fprintf(stderr, "[MachInjector] execv(%s) failed: %s\n",
                     target.executable_path.c_str(), std::strerror(errno));
        ::_exit(1);
    }

    ::usleep(100000);
    int status;
    pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
        std::fprintf(stderr, "[MachInjector] %s exited immediately — "
                             "may be already running, skipping\n",
                     target.executable_path.c_str());
        return false;
    }
    return true;
}

bool injectIntoTarget(const TargetApp& target,
                      const std::string& dylib_path,
                      const std::string& server_ip,
                      uint16_t server_port) {
    switch (target.capability) {
        case InjectionCapability::DYLD_INSERT_LIBRARIES:
            return launchWithDyldEnv(target, dylib_path, server_ip, server_port);

        case InjectionCapability::MACH_VM_ALLOCATE: {
            std::fprintf(stderr, "[MachInjector] MACH_VM_ALLOCATE not yet implemented "
                                 "for %s — falling back to DYLD_INSERT_LIBRARIES launch\n",
                         target.executable_path.c_str());
            return launchWithDyldEnv(target, dylib_path, server_ip, server_port);
        }

        default:
            std::fprintf(stderr, "[MachInjector] No viable injection vector for %s\n",
                         target.executable_path.c_str());
            return false;
    }
}

#else
// Non-macOS, non-testing: injection not supported
bool injectIntoTarget(const TargetApp&, const std::string&,
                       const std::string&, uint16_t) {
    std::fprintf(stderr, "[MachInjector] Injection is macOS-only\n");
    return false;
}
#endif

}} // namespace inferno::tier2
