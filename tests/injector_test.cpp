#include <cstdio>
#include <cstdlib>
#include <string>
#include <dlfcn.h>
#include <unistd.h>

#ifdef __APPLE__

#include "entry_dylib.hpp"

void test_agent_dylib_loads() {
    // Set env so the real dylib constructor fails connection fast
    ::setenv("INFERNO_SERVER_IP", "0.0.0.0", 1);
    ::setenv("INFERNO_SERVER_PORT", "1", 1);

    std::string dylib_path = std::string(TEST_BINARY_DIR) + "/libinferno_agent.dylib";
    void* handle = ::dlopen(dylib_path.c_str(), RTLD_NOW);
    if (!handle) {
        std::fprintf(stderr, "[FAIL] test_agent_dylib_loads: dlopen failed: %s\n",
                     ::dlerror());
        std::exit(1);
    }

    // Verify the production dylib's constructor actually ran
    int* entry_ran = static_cast<int*>(::dlsym(handle, "inferno_agent_entry_ran"));
    if (!entry_ran || *entry_ran != 1) {
        std::fprintf(stderr, "[FAIL] test_agent_dylib_loads: "
                             "constructor did not run (entry_ran=%p, val=%d)\n",
                     (void*)entry_ran, entry_ran ? *entry_ran : -1);
        std::exit(1);
    }

    ::dlclose(handle);
    std::fprintf(stdout, "[PASS] test_agent_dylib_loads\n");
}

void test_shim_binary_exists() {
    const char* shim_path = TEST_BINARY_DIR "/inferno_shim";
    FILE* f = ::fopen(shim_path, "rb");
    if (!f) {
        std::fprintf(stderr, "[FAIL] test_shim_binary_exists: "
                             "not found at %s\n", shim_path);
        std::exit(1);
    }
    ::fclose(f);
    std::fprintf(stdout, "[PASS] test_shim_binary_exists\n");
}

#endif
