#include <iostream>
#include "../include/server.hpp"

int main(int argc, char** argv) {
    uint16_t port = 4242;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    inferno::Server server(port);
    
    if (!server.start()) {
        std::cerr << "[Server] Failed to start\n";
        return 1;
    }
    
    server.run();
    return 0;
}