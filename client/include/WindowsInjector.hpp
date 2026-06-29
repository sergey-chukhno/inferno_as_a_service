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
#endif

}} // namespace inferno::tier2
