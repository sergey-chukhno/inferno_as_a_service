#pragma once

#include <string>
#include <vector>

namespace inferno { namespace tier2 {

enum class InjectionCapability {
    NONE,
    DYLD_INSERT_LIBRARIES,
    MACH_VM_ALLOCATE,
    DYLIB_PROXYING,
};

struct TargetApp {
    std::string path;
    std::string bundle_id;
    std::string executable_path;
    InjectionCapability capability;
    int score;
};

struct ScanReport {
    std::vector<TargetApp> candidates;
    int selected_index;
    InjectionCapability used_capability;
    bool injection_successful;
};

std::vector<TargetApp> scanApplications();
ScanReport buildReport(const std::vector<TargetApp>& candidates,
                       int selected_index,
                       InjectionCapability used,
                       bool success);

}} // namespace inferno::tier2
