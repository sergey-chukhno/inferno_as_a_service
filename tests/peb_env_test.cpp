#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifdef _WIN64

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../client/include/ReflectiveLoader.hpp"
#include "../client/include/NtApi.hpp"

// ── PEB Environment Variable Injection Tests ────────────────────

static bool readTargetPeb(HANDLE hProcess, PVOID& processParams,
                           PVOID& oldEnvironment) {
    auto& nt = inferno::nt::NtApi::resolve();
    if (!nt.isResolved()) return false;

    inferno::nt::MY_PROCESS_BASIC_INFORMATION pbi;
    ULONG retLen = 0;
    LONG status = nt.NtQueryInformationProcess(
        hProcess, inferno::nt::kProcessBasicInformation,
        &pbi, sizeof(pbi), &retLen);
    if (!NT_SUCCESS(status) || retLen < sizeof(pbi) || !pbi.PebBaseAddress)
        return false;

    inferno::nt::MY_PEB peb;
    SIZE_T br = 0;
    if (!::ReadProcessMemory(hProcess, pbi.PebBaseAddress,
                              &peb, sizeof(peb), &br) ||
        br != sizeof(peb) || !peb.ProcessParameters)
        return false;

    processParams = peb.ProcessParameters;

    if (!::ReadProcessMemory(hProcess,
            static_cast<BYTE*>(peb.ProcessParameters) +
                offsetof(inferno::nt::MY_RTL_USER_PROCESS_PARAMETERS,
                         Environment),
            &oldEnvironment, sizeof(PVOID), &br) ||
        br != sizeof(PVOID))
        return false;

    return true;
}

void test_peb_env_vars_visible() {
    // Save original Environment pointer so we can restore after test
    PVOID savedParams = nullptr;
    PVOID savedEnv = nullptr;
    if (!readTargetPeb(::GetCurrentProcess(), savedParams, savedEnv)) {
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "failed to read original PEB environment\n");
        std::exit(1);
    }

    // Run injection — this replaces the entire environment block
    bool ok = inferno::nt::setTargetEnv(
        ::GetCurrentProcess(), "1.2.3.4", 5678);
    if (!ok) {
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "setTargetEnv returned false\n");
        std::exit(1);
    }

    // Verify the injected values are visible
    char ip[64] = {0};
    ::GetEnvironmentVariableA("INFERNO_SERVER_IP", ip, sizeof(ip));
    if (std::strcmp(ip, "1.2.3.4") != 0) {
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "expected IP='1.2.3.4', got '%s'\n", ip);
        std::exit(1);
    }

    char port[64] = {0};
    ::GetEnvironmentVariableA("INFERNO_SERVER_PORT", port, sizeof(port));
    if (std::strcmp(port, "5678") != 0) {
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "expected PORT='5678', got '%s'\n", port);
        std::exit(1);
    }

    // Restore original Environment pointer so the process environment
    // (PATH, TEMP, TEST_BINARY_DIR, etc.) is fully recovered
    SIZE_T written = 0;
    if (!::WriteProcessMemory(::GetCurrentProcess(),
            static_cast<BYTE*>(savedParams) +
                offsetof(inferno::nt::MY_RTL_USER_PROCESS_PARAMETERS,
                         Environment),
            &savedEnv, sizeof(PVOID), &written) ||
        written != sizeof(PVOID)) {
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_visible: "
                             "failed to restore original environment\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_peb_env_vars_visible\n");
}

void test_peb_env_vars_fallback_on_failure() {
    // An invalid handle should not crash — setTargetEnv should log the
    // error and return true (best-effort fallback).
    bool ok = inferno::nt::setTargetEnv(
        reinterpret_cast<HANDLE>(-1), "10.0.0.1", 9999);
    if (!ok) {
        std::fprintf(stderr, "[FAIL] test_peb_env_vars_fallback_on_failure: "
                             "setTargetEnv returned false (should be best-effort)\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_peb_env_vars_fallback_on_failure\n");
}

#endif // _WIN64
#endif // _WIN32
