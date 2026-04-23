#include "../include/Agent.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    uint16_t port = 8080;

    if (argc >= 3) {
        ip = argv[1];
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    std::cout << "[Inferno Agent] Initializing Deployment to " << ip << ":" << port << "...\n";
    
    inferno::Agent agent(ip, port);
    agent.run();

    return 0;
}
