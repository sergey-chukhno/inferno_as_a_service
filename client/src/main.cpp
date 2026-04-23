#include "../include/Agent.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    uint16_t port = 8080;

    if (argc >= 3) {
        ip = argv[1];
        try {
            size_t consumed = 0;
            int p = std::stoi(argv[2], &consumed);
            if (consumed != std::string(argv[2]).length() || p < 1 || p > 65535) {
                throw std::out_of_range("Invalid port");
            }
            port = static_cast<uint16_t>(p);
        } catch (...) {
            std::cerr << "[Error] Invalid port: " << argv[2] << ". Must be 1-65535.\n";
            std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port>\n";
            return 1;
        }
    }

    std::cout << "[Inferno Agent] Initializing Deployment to " << ip << ":" << port << "...\n";
    
    inferno::Agent agent(ip, port);
    agent.run();

    return 0;
}
