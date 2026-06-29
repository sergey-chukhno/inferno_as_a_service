#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../client/include/WindowsInjector.hpp"

void test_agent_dll_loads() {
    // Add build dir to DLL search path so OpenSSL dependencies resolve
    ::SetDllDirectoryA(TEST_BINARY_DIR);

    std::string dll_path = std::string(TEST_BINARY_DIR) + "/inferno_agent.dll";

    // Use _putenv_s (not SetEnvironmentVariableA) so getenv() in the DLL sees them
    ::_putenv_s("INFERNO_SERVER_IP", "0.0.0.0");
    ::_putenv_s("INFERNO_SERVER_PORT", "1");

    HMODULE handle = ::LoadLibraryA(dll_path.c_str());
    if (!handle) {
        DWORD err = ::GetLastError();
        std::fprintf(stderr, "[SKIP] test_agent_dll_loads: LoadLibraryA failed "
                             "(error %lu) — %s not available in this environment\n",
                     err, dll_path.c_str());
        return;
    }

    // Give the spawned thread time to set inferno_agent_entry_ran
    ::Sleep(200);

    int* entry_ran = reinterpret_cast<int*>(
        ::GetProcAddress(handle, "inferno_agent_entry_ran"));
    if (!entry_ran) {
        std::fprintf(stderr, "[FAIL] test_agent_dll_loads: "
                             "GetProcAddress failed (error %lu)\n",
                     ::GetLastError());
        ::FreeLibrary(handle);
        std::exit(1);
    }

    if (*entry_ran != 1) {
        std::fprintf(stderr, "[FAIL] test_agent_dll_loads: "
                             "constructor did not run (entry_ran=%d)\n",
                     *entry_ran);
        ::FreeLibrary(handle);
        std::exit(1);
    }

    ::FreeLibrary(handle);
    std::fprintf(stdout, "[PASS] test_agent_dll_loads\n");
}

void test_loader_binary_exists() {
    const char* loader_path = TEST_BINARY_DIR "/inferno_loader.exe";
    FILE* f = ::fopen(loader_path, "rb");
    if (!f) {
        std::fprintf(stderr, "[FAIL] test_loader_binary_exists: "
                             "not found at %s\n", loader_path);
        std::exit(1);
    }
    ::fclose(f);
    std::fprintf(stdout, "[PASS] test_loader_binary_exists\n");
}

void test_find_ntdll_string() {
    const char* found = inferno::tier2::findNtdllString("0", 1);
    if (!found) {
        std::fprintf(stderr, "[FAIL] test_find_ntdll_string: "
                             "\"0\" not found in ntdll.dll .rdata\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_find_ntdll_string: found at %p\n",
                 (void*)found);
}

void test_windows_injector_stub() {
    inferno::tier2::TargetApp target;
    target.path = "C:\\test.exe";
    target.executable_path = "test.exe";
    target.bundle_id = "test";
    target.capability = inferno::tier2::InjectionCapability::DYLD_INSERT_LIBRARIES;
    target.score = 100;

    bool result = inferno::tier2::injectIntoTarget(target, "test.dll", "127.0.0.1", 4242);
    if (!result) {
        std::fprintf(stderr, "[FAIL] test_windows_injector_stub: "
                             "injectIntoTarget returned false\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_windows_injector_stub\n");
}

#endif // _WIN32
