#include "../include/Agent.hpp"
#include <iostream>

int main() {
    std::cout << "[Inferno Agent] Initializing Deployment...\n";
    
    // Default to localhost for now
    inferno::Agent agent("127.0.0.1", 8888);
    agent.run();

    return 0;
}
