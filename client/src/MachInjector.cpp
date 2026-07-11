#include "../include/MachInjector.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <sys/time.h>

#ifdef __APPLE__
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctime>
#endif

namespace inferno { namespace tier2 {

#if defined(INFERNO_TESTING)
// In testing mode, skip actual fork/exec — just report success
bool injectIntoTarget(const TargetApp&, const std::string&,
                       const std::string&, uint16_t) {
    return true;
}

#elif defined(__APPLE__)

// Debug logging — writes to /tmp/inject_debug.log to trace injection
// even after the agent daemonizes and redirects stderr to /dev/null.
namespace {
    static FILE* injectLogFile() {
        static FILE* f = nullptr;
        if (!f) {
            f = ::fopen("/tmp/inject_debug.log", "a");
        }
        return f;
    }
    static void injectLog(const char* fmt, ...) {
        FILE* f = injectLogFile();
        if (!f) return;
        struct timeval tv;
        ::gettimeofday(&tv, nullptr);
        std::fprintf(f, "[%.3f] ", tv.tv_sec + tv.tv_usec / 1000000.0);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(f, fmt, ap);
        va_end(ap);
        std::fprintf(f, "\n");
        ::fflush(f);
    }
}
#define INJECT_LOG(...) injectLog(__VA_ARGS__)

// Shared subprocess helper (pipe + fork + exec + read) — local copy
// to avoid a dependency on EntitlementScanner.cpp internals.
static std::string runProcess(const char* cmd,
                               const std::vector<const char*>& args,
                               unsigned timeout_sec = 5) {
    int pipefd[2];
    if (::pipe(pipefd) != 0) return {};
    pid_t pid = ::fork();
    if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return {}; }
    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);
        if (timeout_sec > 0) ::alarm(timeout_sec);
        std::vector<const char*> argv = { cmd };
        argv.insert(argv.end(), args.begin(), args.end());
        argv.push_back(nullptr);
        ::execv(cmd, const_cast<char* const*>(argv.data()));
        ::_exit(1);
    }
    ::close(pipefd[1]);
    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        result += buf;
    }
    ::close(pipefd[0]);
    int status;
    ::waitpid(pid, &status, 0);
    return result;
}

// Re-sign an ad-hoc signed binary with the allow-dyld-environment-variables
// entitlement so DYLD_INSERT_LIBRARIES works even under hardened runtime.
// Uses a temporary entitlements plist generated inline.
// Returns true if the binary is ready (unsigned or re-signed).
static bool prepareBinaryForDyldInjection(const std::string& exec_path) {
    // First check whether codesign thinks the binary is signed
    std::string check = runProcess("/usr/bin/codesign",
                                    {"-d", exec_path.c_str()}, 3);
    if (check.empty() || check.find("not signed") != std::string::npos) {
        return true; // Unsigned — DYLD works directly
    }

    // Generate a temporary entitlements plist that enables DYLD injection
    // and disables library validation for the injected dylib.
    std::string ent_path = exec_path + ".inferno_entitlements.plist";
    {
        FILE* f = ::fopen(ent_path.c_str(), "w");
        if (!f) return false;
        std::fprintf(f, R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.allow-dyld-environment-variables</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
)");
        ::fclose(f);
    }

    // Re-sign with ad-hoc identity and the DYLD-enabling entitlements
    std::string result = runProcess("/usr/bin/codesign",
                                     {"-f", "-s", "-",
                                      "--entitlements", ent_path.c_str(),
                                      exec_path.c_str()}, 10);

    ::remove(ent_path.c_str());

    // Verify the new signature has the entitlement
    std::string verify = runProcess("/usr/bin/codesign",
                                     {"-d", "--entitlements", "-",
                                      exec_path.c_str()}, 3);
    bool ok = verify.find("allow-dyld-environment-variables") != std::string::npos;
    INJECT_LOG("Re-signed %s with DYLD entitlements (%s)",
               exec_path.c_str(), ok ? "OK" : "FAILED");
    return ok;
}

static bool launchWithDyldEnv(const TargetApp& target,
                                const std::string& dylib_path,
                                const std::string& server_ip,
                                uint16_t server_port) {
    // Ensure no existing instance is running — DYLD_INSERT_LIBRARIES
    // only works at process launch, and macOS prevents launching a
    // second instance of most GUI apps.
    size_t sep = target.executable_path.rfind('/');
    std::string app_name = (sep != std::string::npos)
        ? target.executable_path.substr(sep + 1) : target.executable_path;
    // Signal that we reached this function — write a sentinel file
    {
        FILE* f = ::fopen("/tmp/inject_reached.txt", "w");
        if (f) {
            std::fprintf(f, "launchWithDyldEnv called for %s at %ld\n",
                         app_name.c_str(), time(nullptr));
            ::fclose(f);
        }
    }

    pid_t kill_pid = ::fork();
    if (kill_pid == 0) {
        ::execlp("killall", "killall", app_name.c_str(), nullptr);
        ::_exit(0);
    }
    if (kill_pid > 0) {
        int ws;
        ::waitpid(kill_pid, &ws, 0);
    }
    INJECT_LOG("Killed any existing '%s' (killall exit)", app_name.c_str());
    ::sleep(2);

    INJECT_LOG("Re-signing %s for DYLD injection...",
               target.executable_path.c_str());
    prepareBinaryForDyldInjection(target.executable_path);

    pid_t pid = ::fork();
    if (pid < 0) {
        INJECT_LOG("fork() failed: %s", std::strerror(errno));
        return false;
    }

    INJECT_LOG("Launching %s with dylib %s",
               target.executable_path.c_str(), dylib_path.c_str());

    if (pid == 0) {
        ::setenv("DYLD_INSERT_LIBRARIES", dylib_path.c_str(), 1);
        ::setenv("INFERNO_SERVER_IP", server_ip.c_str(), 1);
        ::setenv("INFERNO_SERVER_PORT", std::to_string(server_port).c_str(), 1);
        ::unsetenv("DYLD_FORCE_FLAT_NAMESPACE");

        INJECT_LOG("Child: execv(%s) with DYLD=%s",
                   target.executable_path.c_str(), dylib_path.c_str());

        const char* const argv[] = {target.executable_path.c_str(), nullptr};
        ::execv(target.executable_path.c_str(),
                const_cast<char* const*>(argv));

        // execv failed — log to both file and stderr
        int err = errno;
        INJECT_LOG("Child: execv(%s) failed: %s",
                   target.executable_path.c_str(), std::strerror(err));
        std::fprintf(stderr, "[MachInjector] execv(%s) failed: %s\n",
                     target.executable_path.c_str(), std::strerror(err));
        ::_exit(1);
    }

    ::usleep(100000);
    int status;
    pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
        INJECT_LOG("%s exited immediately (status=%d) — may be already running",
                   target.executable_path.c_str(), status);
        return false;
    }
    INJECT_LOG("Injection launch appears successful (pid=%d running)", pid);
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
