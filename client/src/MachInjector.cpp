#include "../include/MachInjector.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

namespace inferno { namespace tier2 {

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
        // Child: set env and exec the target app
        ::setenv("DYLD_INSERT_LIBRARIES", dylib_path.c_str(), 1);
        ::setenv("INFERNO_SERVER_IP", server_ip.c_str(), 1);
        ::setenv("INFERNO_SERVER_PORT", std::to_string(server_port).c_str(), 1);

        // Unset macOS sneakiness — don't inherit our own libs
        ::unsetenv("DYLD_FORCE_FLAT_NAMESPACE");

        const char* const argv[] = {target.executable_path.c_str(), nullptr};
        ::execv(target.executable_path.c_str(),
                const_cast<char* const*>(argv));

        // execv failed
        std::fprintf(stderr, "[MachInjector] execv(%s) failed: %s\n",
                     target.executable_path.c_str(), std::strerror(errno));
        ::_exit(1);
    }

    // Parent: wait briefly for the app to launch
    int status;
    ::waitpid(pid, &status, WNOHANG);
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

}} // namespace inferno::tier2
