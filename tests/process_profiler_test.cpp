#include "../client/include/ProcessProfiler.hpp"
#include <iostream>
#include <cassert>

using namespace inferno;

void test_process_profiler_snapshot() {
    std::cout << "[TEST] Testing ProcessProfiler Snapshot..." << std::endl;
    
    ProcessProfiler profiler;
    const auto& list = profiler.getSnapshot();
    
    // Assert that we get a list of processes
    assert(!list.empty() && "Process list should not be empty");
    
    // Validate individual elements
    bool found_valid_pid = false;
    for (const auto& entry : list) {
        assert(!entry.name.empty() && "Process name must not be empty");
        if (entry.pid > 0) {
            found_valid_pid = true;
        }
    }
    
    assert(found_valid_pid && "Process list must contain at least one valid process with PID > 0");
    std::cout << "[PASS] test_process_profiler_snapshot (" << list.size() << " processes found)" << std::endl;
}
