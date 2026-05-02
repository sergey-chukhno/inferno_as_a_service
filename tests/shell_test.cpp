#include "../client/include/ShellExecutor.hpp"
#include <iostream>
#include <cassert>
#include <string>

using namespace inferno;

void test_shell_executor_echo() {
    ShellExecutor executor;
    const auto result = executor.execute("echo hello");

    assert(result.success && "echo command should succeed");
    // Output should contain "hello" (may have trailing newline)
    assert(result.output.find("hello") != std::string::npos &&
           "Output should contain 'hello'");

    std::cout << "[PASS] test_shell_executor_echo" << std::endl;
}

void test_shell_executor_failure() {
    ShellExecutor executor;
    // A command that does not exist should set success = false
    const auto result = executor.execute("this_command_does_not_exist_xyz");

    assert(!result.success && "Unknown command should report failure");
    assert(!result.output.empty() && "Failure should produce error output");

    std::cout << "[PASS] test_shell_executor_failure" << std::endl;
}

void test_shell_executor_stderr_redirect() {
    ShellExecutor executor;
    // ls on a non-existent path produces stderr output
    const auto result = executor.execute("ls /this/does/not/exist");

    assert(!result.output.empty() && "stderr should be captured via 2>&1 redirect");

    std::cout << "[PASS] test_shell_executor_stderr_redirect" << std::endl;
}

void test_shell_executor_chunk_size() {
    // Verify the compile-time constant is set to the expected page size
    static_assert(ShellExecutor::CHUNK_SIZE == 4096,
                  "CHUNK_SIZE must be 4096 (system page size for stealth traffic profiling)");

    std::cout << "[PASS] test_shell_executor_chunk_size" << std::endl;
}
