#include <iostream>
#include "../include/server.hpp"

int main() {
    inferno::Server server(4242);
    
    if (!server.start()) {
        std::cerr << "[Server] Failed to start\n";
        return 1;
    }
    
    server.run();
    return 0;
}