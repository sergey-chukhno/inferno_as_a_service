#include "../include/ProcessProfiler.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>

#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#elif defined(__linux__)
#include <dirent.h>
#include <fstream>
#include <unistd.h>
#include <cctype>
#endif

namespace inferno {

ProcessProfiler::ProcessProfiler() 
    : m_cache_duration(std::chrono::seconds(30)) {
    m_last_update = std::chrono::steady_clock::now() - (m_cache_duration + std::chrono::seconds(1));
}

const std::vector<ProcessEntry>& ProcessProfiler::getSnapshot() {
    auto now = std::chrono::steady_clock::now();
    
    if (m_cache.empty() || (now - m_last_update) > m_cache_duration) {
        m_cache = captureFreshList();
        m_last_update = now;
    }
    
    return m_cache;
}

#ifdef __APPLE__
std::vector<ProcessEntry> ProcessProfiler::captureFreshList() {
    std::vector<ProcessEntry> list;
    
    int count = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (count <= 0) return list;

    std::vector<pid_t> pids(count);
    count = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (count <= 0) return list;

    for (int i = 0; i < count; i++) {
        if (pids[i] == 0) continue;

        char path_buffer[PROC_PIDPATHINFO_MAXSIZE];
        int ret = proc_name(pids[i], path_buffer, sizeof(path_buffer));
        
        ProcessEntry entry;
        entry.pid = static_cast<uint32_t>(pids[i]);
        entry.name = (ret > 0) ? std::string(path_buffer) : "<Access Denied / Unknown>";
        
        list.push_back(std::move(entry));
    }
    return list;
}
#elif defined(__linux__)
std::vector<ProcessEntry> ProcessProfiler::captureFreshList() {
    std::vector<ProcessEntry> list;
    DIR* dir = opendir("/proc");
    if (!dir) return list;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // We only care about directories that are numbers (PIDs)
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            uint32_t pid = static_cast<uint32_t>(std::stoul(entry->d_name));
            
            // Read name from /proc/[pid]/comm
            std::string comm_path = "/proc/" + std::string(entry->d_name) + "/comm";
            std::ifstream comm_file(comm_path);
            std::string name;
            
            if (comm_file >> name) {
                list.push_back({pid, name});
            } else {
                list.push_back({pid, "<Access Denied>"});
            }
        }
    }
    closedir(dir);
    return list;
}
#else
std::vector<ProcessEntry> ProcessProfiler::captureFreshList() {
    return {}; // Unsupported platform
}
#endif

} // namespace inferno
