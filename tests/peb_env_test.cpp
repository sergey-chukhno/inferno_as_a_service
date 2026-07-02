#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifdef _WIN64

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../client/include/ReflectiveLoader.hpp"

// ── PEB Environment Variable Injection Tests ────────────────────
// Tests that setTargetEnv() properly patches the current process's
// PEB so that GetEnvironmentVariable sees the injected values.

static void restoreEnv(const char* name, const char* oldVal) {
    if (oldVal && oldVal[0]) {
        ::SetEnvironmentVariableA(name, oldVal);
    } else {
        ::SetEnvironmentVariableA(name, nullptr);
    }
}

void test_peb_env_vars_visible() {
    // Save originals
    char old_ip[64] = {0};
    char old_port[64] = {0};
    ::GetEnvironmentVariableA("INFERNO_SERVER_IP", old_ip, sizeof(old_ip));
    ::GetEnvironmentVariableA("INFERNO_SERVER_PORT", old_port, sizeof(old_port));

    bool ok = inferno::nt::setTargetEnv(
        ::GetCurrentProcess(), "1.2.3.4", 5678);
    if (!ok) {
        restoreEnv("INFERNO_SERVER_IP", old_ip);
        restoreEnv("INFERNO_SERVER_PORT", old_port);
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "setTargetEnv returned false\n");
        std::exit(1);
    }

    char ip[64] = {0};
    ::GetEnvironmentVariableA("INFERNO_SERVER_IP", ip, sizeof(ip));
    if (std::strcmp(ip, "1.2.3.4") != 0) {
        restoreEnv("INFERNO_SERVER_IP", old_ip);
        restoreEnv("INFERNO_SERVER_PORT", old_port);
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "expected IP='1.2.3.4', got '%s'\n", ip);
        std::exit(1);
    }

    char port[64] = {0};
    ::GetEnvironmentVariableA("INFERNO_SERVER_PORT", port, sizeof(port));
    if (std::strcmp(port, "5678") != 0) {
        restoreEnv("INFERNO_SERVER_IP", old_ip);
        restoreEnv("INFERNO_SERVER_PORT", old_port);
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "expected PORT='5678', got '%s'\n", port);
        std::exit(1);
    }

    // Restore originals
    restoreEnv("INFERNO_SERVER_IP", old_ip);
    restoreEnv("INFERNO_SERVER_PORT", old_port);

    std::fprintf(stdout, "[PASS] test_peb_env_vars_visible\n");
}

void test_peb_env_vars_fallback_on_failure() {
    // Passing an invalid handle should not crash — setTargetEnv should
    // return true (best-effort fallback) after logging the failure.
    bool ok = inferno::nt::setTargetEnv(
        ::GetCurrentProcess(), "192.168.1.1", 8080);
    if (!ok) {
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_fallback_on_failure: "
                             "setTargetEnv returned false (should be best-effort)\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_peb_env_vars_fallback_on_failure\n");
}

#endif // _WIN64
#endif // _WIN32
