#include "../include/EntitlementScanner.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

#ifdef __APPLE__
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
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

static std::string readEntitlements(const std::string& exec_path) {
    int pipefd[2];
    if (::pipe(pipefd) != 0) return {};

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return {};
    }

    if (pid == 0) {
        // Child: exec codesign with 3-second timeout
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);

        // Timeout: kill self after 3 seconds
        ::alarm(3);

        ::execl("/usr/bin/codesign", "codesign", "-d", "--entitlements", "-",
                exec_path.c_str(), nullptr);
        ::_exit(1);
    }

    // Parent: read from pipe
    ::close(pipefd[1]);
    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        result += buf;
    }
    ::close(pipefd[0]);

    // Reap the child — already exited (alarm killed it or it finished)
    int status;
    ::waitpid(pid, &status, 0);
    return result;
}

static InjectionCapability classifyEntitlements(const std::string& entitlements) {
    if (entitlements.empty()) {
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
                std::string line;
                while (std::getline(plist, line)) {
                    auto pos = line.find("CFBundleIdentifier");
                    if (pos != std::string::npos) {
                        auto start = line.find("<string>", pos);
                        if (start != std::string::npos) {
                            start += 8;
                            auto end = line.find("</string>", start);
                            if (end != std::string::npos) {
                                bundle_id = line.substr(start, end - start);
                            }
                        }
                    }
                }
            }
        }

        results.push_back({app_path, bundle_id, exec_path, cap, sc});
    }
    ::closedir(d);
}

std::vector<TargetApp> scanApplications() {
    std::vector<TargetApp> results;
    scanDirectory("/Applications", results);
    scanDirectory("/Applications/Utilities", results);
    const char* home = ::getenv("HOME");
    if (home) {
        scanDirectory(std::string(home) + "/Applications", results);
    }
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
