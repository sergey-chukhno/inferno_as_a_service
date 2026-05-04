#include "Socket.hpp"
#include <optional>
#include <iostream>
#include <cassert>

using namespace inferno;

void test_socket_creation() {
    Socket s;
    assert(s.getFd() == -1 && "Default socket should have FD -1");
    assert(!s.isValid() && "Default socket should be invalid");
    std::cout << "[PASS] test_socket_creation" << std::endl;
}

void test_end_to_end_connection() {
    // Port 0: OS-assigned ephemeral port to completely eliminate collisions
    Socket server;
    assert(server.bindNode("127.0.0.1", 0)==true && "Server should bind to the specified IP and port");
    assert(server.listen()==true && "Server should listen for incoming connections");
    
    uint16_t bound_port = server.getPort();
    assert(bound_port > 0 && "Server should have a valid bound port");

    Socket client;
    assert(client.connectTo("127.0.0.1", bound_port)==true && "Client should connect to the server");
    std::optional<Socket> accepted_client = server.acceptNode();
    assert(accepted_client.has_value() && "Server should accept the connection");
    assert(accepted_client->isValid() && "Accepted client socket should be valid!");
    std::cout << "[PASS] test_end_to_end_connection" << std::endl;
}

// We have not implemented a test for sendData and receiveData. 
// - sendData / receiveData buffer allocations


