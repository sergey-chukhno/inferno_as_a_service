#pragma once

#include "EntitlementScanner.hpp"
#include <string>

namespace inferno { namespace tier2 {

bool injectIntoTarget(const TargetApp& target,
                      const std::string& dylib_path,
                      const std::string& server_ip,
                      uint16_t server_port);

}} // namespace inferno::tier2
