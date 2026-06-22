#include <cstdio>
#include <cstdlib>

#include "EntitlementScanner.hpp"

#ifdef __APPLE__

void test_scanner_classification() {
    auto targets = inferno::tier2::scanApplications();

    // Scanner should not crash and should return a list (may be empty on CI without /Applications)
    std::fprintf(stdout, "[PASS] test_scanner_classification: %zu apps scanned\n",
                 targets.size());

    // Verify results are sorted by score descending
    for (size_t i = 1; i < targets.size(); ++i) {
        if (targets[i - 1].score < targets[i].score) {
            std::fprintf(stderr, "[FAIL] test_scanner_classification: "
                                 "results not sorted by score\n");
            std::exit(1);
        }
    }

    // Verify each entry has non-empty path
    for (const auto& t : targets) {
        if (t.path.empty()) {
            std::fprintf(stderr, "[FAIL] test_scanner_classification: "
                                 "empty path in result\n");
            std::exit(1);
        }
    }

    std::fprintf(stdout, "[PASS] test_scanner_classification\n");
}

void test_scanner_empty_report() {
    auto report = inferno::tier2::buildReport({}, -1,
        inferno::tier2::InjectionCapability::NONE, false);

    if (!report.candidates.empty()) {
        std::fprintf(stderr, "[FAIL] test_scanner_empty_report: "
                             "expected empty candidates\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_scanner_empty_report\n");
}

#else

void test_scanner_classification() {
    auto targets = inferno::tier2::scanApplications();
    if (!targets.empty()) {
        std::fprintf(stderr, "[FAIL] test_scanner_classification: "
                             "expected empty on non-macOS\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_scanner_classification\n");
}

void test_scanner_empty_report() {
    std::fprintf(stdout, "[PASS] test_scanner_empty_report (stub)\n");
}

#endif
