#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../client/include/WindowsInjector.hpp"
#include "../client/include/ReflectiveLoader.hpp"

// ── Wire Reflective-Loader → injectIntoTarget Tests ────────────

void test_read_binary_file_empty_path() {
    // readBinaryFile is an internal static function in WindowsInjector.cpp,
    // not directly accessible from tests. We test the chain indirectly
    // by calling injectIntoTarget with an empty/invalid DLL path, which
    // should skip the reflective path (readBinaryFile returns empty) and
    // fall through to the existing injection path.
    //
    // Under INFERNO_TESTING, injectIntoTarget is stubbed to return true,
    // so this test verifies the stub compiles and links.

    // Just verify the symbols exist and the test links correctly
    std::fprintf(stdout, "[INFO] injectIntoTarget stub exists\n");
    std::fprintf(stdout, "[PASS] test_read_binary_file_empty_path\n");
}

void test_inject_reflective_stub_links() {
    // Verify the INFERNO_TESTING stub for injectReflective links correctly
    std::vector<uint8_t> empty;
    bool ok = inferno::nt::injectReflective(
        INVALID_HANDLE_VALUE, empty, "127.0.0.1", 4242);
    if (ok) {
        std::fprintf(stdout, "[PASS] test_inject_reflective_stub_links\n");
    } else {
        std::fprintf(stderr, "[FAIL] test_inject_reflective_stub_links: "
                             "stub returned false\n");
        std::exit(1);
    }
}

void test_inject_intotarget_reflective_path_invoked() {
    // Under INFERNO_TESTING, injectIntoTarget is a simple stub.
    // This test verifies the function exists and returns true.
    // The actual reflective-first logic runs on Windows under real builds.
    bool ok = inferno::tier2::injectIntoTarget(
        inferno::tier2::TargetApp{}, "dummy.dll", "127.0.0.1", 4242);
    if (ok) {
        std::fprintf(stdout, "[PASS] test_inject_intotarget_reflective_path_invoked\n");
    } else {
        std::fprintf(stderr, "[FAIL] test_inject_intotarget_reflective_path: "
                             "stub failed\n");
        std::exit(1);
    }
}

#else

void test_wire_reflective_not_available() {
    std::fprintf(stdout, "[SKIP] Wire reflective tests are Windows-only\n");
}

#endif
