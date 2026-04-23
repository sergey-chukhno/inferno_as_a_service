#include "../include/ProcessProfiler.hpp"
#include <libproc.h>
#include <unistd.h>
#include <iostream>

namespace inferno {

ProcessProfiler::ProcessProfiler() 
    : m_cache_duration(std::chrono::seconds(30)) {
    m_last_update = std::chrono::steady_clock::now() - (m_cache_duration + std::chrono::seconds(1));
}

std::vector<ProcessEntry> ProcessProfiler::getSnapshot() {
    auto now = std::chrono::steady_clock::now();
    
    if (m_cache.empty() || (now - m_last_update) > m_cache_duration) {
        m_cache = captureFreshList();
        m_last_update = now;
    }
    
    return m_cache;
}

std::vector<ProcessEntry> ProcessProfiler::captureFreshList() {
    std::vector<ProcessEntry> list;
    
    // 1. Get the number of processes currently running
    int count = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (count <= 0) return list;

    // 2. Allocate buffer and get PIDs
    std::vector<pid_t> pids(count);
    count = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (count <= 0) return list;

    // 3. For each PID, get the name
    for (int i = 0; i < count; i++) {
        if (pids[i] == 0) continue;

        char path_buffer[PROC_PIDPATHINFO_MAXSIZE];
        int ret = proc_name(pids[i], path_buffer, sizeof(path_buffer));
        
        ProcessEntry entry;
        entry.pid = static_cast<uint32_t>(pids[i]);
        
        if (ret > 0) {
            entry.name = std::string(path_buffer);
        } else {
            // Handle Access Denied / Dead processes between list and name retrieval
            entry.name = "<Access Denied / Unknown>";
        }
        
        list.push_back(std::move(entry));
    }

    return list;
}

} // namespace inferno
