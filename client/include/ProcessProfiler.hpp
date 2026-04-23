#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <chrono>

namespace inferno {

struct ProcessEntry {
    uint32_t pid;
    std::string name;
};

class ProcessProfiler {
public:
    ProcessProfiler();
    ~ProcessProfiler() = default;

    /**
     * @brief Retrieves the current process list.
     * Uses internal caching to minimize OS overhead and maintain stealth.
     */
    std::vector<ProcessEntry> getSnapshot();

private:
    std::vector<ProcessEntry> captureFreshList();

    std::vector<ProcessEntry> m_cache;
    std::chrono::steady_clock::time_point m_last_update;
    const std::chrono::seconds m_cache_duration;
};

} // namespace inferno
