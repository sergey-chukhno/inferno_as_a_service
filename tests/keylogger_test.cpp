#include "../client/include/KeyLogger.hpp"
#include <iostream>
#include <cassert>

using namespace inferno;

void test_keylogger_init_state() {
    KeyLogger kl;
    assert(!kl.isRunning() && "KeyLogger should start in stopped state");
    assert(kl.dump().empty() && "KeyLogger buffer should be empty on init");
    std::cout << "[PASS] test_keylogger_init_state\n";
}

void test_keylogger_start_stop() {
    KeyLogger kl;
    kl.start();
    assert(kl.isRunning() && "KeyLogger should be running after start()");
    kl.stop();
    assert(!kl.isRunning() && "KeyLogger should be stopped after stop()");
    std::cout << "[PASS] test_keylogger_start_stop\n";
}

void test_keylogger_dump_clears_buffer() {
    KeyLogger kl;
#ifdef INFERNO_TESTING
    kl.injectKeystroke('A');
    kl.injectKeystroke('B');
    kl.injectKeystroke('C');
    
    std::string dump1 = kl.dump();
    assert(dump1 == "ABC" && "Dump should return injected keystrokes");
    
    std::string dump2 = kl.dump();
    assert(dump2.empty() && "Second dump should be empty");
    std::cout << "[PASS] test_keylogger_dump_clears_buffer\n";
#else
    std::cout << "[SKIP] test_keylogger_dump_clears_buffer (INFERNO_TESTING not defined)\n";
#endif
}

void test_keylogger_capacity_limit() {
    KeyLogger kl;
#ifdef INFERNO_TESTING
    for (size_t i = 0; i < KeyLogger::MAX_BUFFER_SIZE + 10; ++i) {
        kl.injectKeystroke('X');
    }
    std::string dump = kl.dump();
    assert(dump.size() == KeyLogger::MAX_BUFFER_SIZE && "Dump should not exceed MAX_BUFFER_SIZE");
    std::cout << "[PASS] test_keylogger_capacity_limit\n";
#else
    std::cout << "[SKIP] test_keylogger_capacity_limit (INFERNO_TESTING not defined)\n";
#endif
}


