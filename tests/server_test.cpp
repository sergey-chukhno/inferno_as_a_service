#include "../server/include/network/server.hpp"
#include "Socket.hpp"
#include <QThread>
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

void test_server_disconnect_agent() {
    Server s(0);
    assert(s.start() && "Server should start");
    uint16_t port = s.getPort();

    QThread thread;
    s.moveToThread(&thread);
    QObject::connect(&thread, &QThread::started, &s, &Server::run);
    thread.start();

    // Now connect a client socket
    Socket client;
    assert(client.connectTo("127.0.0.1", port) && "Client should connect");

    // Wait a brief moment to let server accept connection
    QThread::msleep(100);

    // Read the initial SYS_REQ_INFO packet sent by the server on connect
    std::vector<uint8_t> buf;
    ssize_t initial_bytes = client.receiveData(buf, 1024);
    assert(initial_bytes > 0 && "Should receive initial SYS_REQ_INFO packet");

    // Call disconnectAgent on client's IP
    s.disconnectAgent("127.0.0.1");

    // Wait a brief moment for the disconnection to propagate
    QThread::msleep(100);

    // Try to receive from client, it should fail / indicate EOF
    ssize_t bytes = client.receiveData(buf, 1024);
    assert(bytes <= 0 && "Client connection should have been closed/shut down by the server");

    // Clean up
    s.stop();
    thread.quit();
    thread.wait();

    std::cout << "[PASS] test_server_disconnect_agent" << std::endl;
}
