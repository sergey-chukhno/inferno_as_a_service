#include "../server/include/server.hpp"
#include <iostream>
#include <cassert>

using namespace inferno;

void test_server_constructors() {
    Server s1;
    assert(!s1.isRunning() && "Default server should not be running");
    assert(s1.getPort() == 0 && "Default server port should be 0");

    Server s2(4243);
    assert(!s2.isRunning() && "Server should not be running yet");
    assert(s2.getPort() == 4243 && "Server port should match constructor");
    
    std::cout << "[PASS] test_server_constructors" << std::endl;
}

void test_server_start() {
    Server s1(0); // Use OS-assigned port
    assert(s1.start() && "Server start should succeed");
    assert(s1.isRunning() && "Server should be marked as running");
    
    uint16_t bound_port = s1.getPort();
    assert(bound_port > 0 && "Server should have a valid assigned port");

    // Attempting to bind a second server to the exact same port must fail!
    Server s2(bound_port);
    assert(!s2.start() && "Second server binding to same port should fail");
    
    std::cout << "[PASS] test_server_start" << std::endl;
}
