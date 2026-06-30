#include "../client/include/MachInjector.hpp"
#include "../client/include/EntitlementScanner.hpp"
#include "../common/include/CryptoContext.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

// ── E2E Injection Test (macOS only) ─────────────────────────────────
// Verifies that injection into a real macOS app (DBeaver) works.
// Requires DBeaver.app to be installed in /Applications.
// Cleans up the launched DBeaver process on completion.

int main() {
    const char* dbeaver_path = "/Applications/DBeaver.app/Contents/MacOS/dbeaver";
    struct stat st;
    if (::stat(dbeaver_path, &st) != 0) {
        std::fprintf(stdout, "[SKIP] DBeaver not found at %s\n", dbeaver_path);
        return 0;
    }

    // Ensure the dylib exists at the expected cache path
    const char* home = ::getenv("HOME");
    if (!home) {
        std::fprintf(stderr, "[FAIL] No HOME env var\n");
        return 1;
    }
    std::string cache_dir = std::string(home) + "/.cache";
    std::string dylib_path = cache_dir + "/com.apple.amp.itmstransporter.dylib";

    // If dylib doesn't exist at cache path, copy from build
    if (::stat(dylib_path.c_str(), &st) != 0) {
        std::string src = TEST_BINARY_DIR;
        src += "/libinferno_agent.dylib";
        if (::stat(src.c_str(), &st) == 0) {
            ::mkdir(cache_dir.c_str(), 0755);
            std::string cmd = "/bin/cp \"" + src + "\" \"" + dylib_path + "\"";
            ::system(cmd.c_str());
            ::chmod(dylib_path.c_str(), 0644);
            std::fprintf(stdout, "[INFO] Copied dylib to %s\n", dylib_path.c_str());
        } else {
            std::fprintf(stderr, "[FAIL] Dylib not found at %s or %s\n",
                         dylib_path.c_str(), src.c_str());
            return 1;
        }
    }

    // Kill any existing DBeaver instance so we start fresh
    ::system("pkill -x dbeaver 2>/dev/null");
    ::usleep(500000);

    // Init crypto
    inferno::CryptoContext::instance().initDefault();

    // Build target
    inferno::tier2::TargetApp target;
    target.executable_path = dbeaver_path;
    target.path = "/Applications/DBeaver.app";
    target.capability = inferno::tier2::InjectionCapability::DYLD_INSERT_LIBRARIES;

    // Inject
    std::fprintf(stdout, "[TEST] Injecting into DBeaver...\n");
    bool ok = inferno::tier2::injectIntoTarget(target, dylib_path,
                                                "127.0.0.1", 9999);
    std::fprintf(stdout, "[TEST] injectIntoTarget returned: %s\n",
                 ok ? "true" : "false");

    if (!ok) {
        std::fprintf(stderr, "[FAIL] Injection into DBeaver failed\n");
        return 1;
    }

    // Wait for DBeaver to launch
    ::sleep(3);
    int running = ::system("pgrep -x dbeaver > /dev/null 2>&1");
    if (running != 0) {
        std::fprintf(stderr, "[FAIL] DBeaver not running after injection\n");
        return 1;
    }
    std::fprintf(stdout, "[PASS] DBeaver launched and running with injected dylib\n");

    // Cleanup: kill DBeaver
    ::system("pkill -x dbeaver 2>/dev/null");
    std::fprintf(stdout, "[PASS] inject_e2e_macos\n");
    return 0;
}
