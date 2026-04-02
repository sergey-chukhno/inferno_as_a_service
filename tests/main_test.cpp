#include <iostream>

// External socket tests
extern void test_socket_creation();
extern void test_end_to_end_connection();

// External server tests
extern void test_server_constructors();
extern void test_server_start();

int main() {
    std::cout << "\n=== Inferno TDD Suite ===" << std::endl;
    
    // Execute all test functions here
    test_socket_creation();
    test_end_to_end_connection();
    test_server_constructors();
    test_server_start();
    
    std::cout << "=========================\n" << std::endl;
    return 0;
}
