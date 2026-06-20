#pragma once

#include <atomic>

namespace inferno { namespace agent {

#ifdef INFERNO_TESTING
bool didAgentConstructorRun();
#endif

bool isDylibShuttingDown();

}} // namespace inferno::agent
