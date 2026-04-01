#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sys/socket.h> // Include cross-platform wrapper macros later 
#include <optional>

namespace inferno {
    
class Socket {
    private:
        int m_socket_fd;
        std::string m_ip;
        uint16_t m_port;

        // Helper function to safely close the file descriptor
        void closeSocket() noexcept; 
    
    public: 
        // Default constructor required to satisfy Coplien Canonical Form
        Socket(); 

        // Custom constructor that initializes the socket with an IP and Port
        Socket(int fd, const std::string& ip, uint16_t port); 

        // Destructor
        ~Socket(); 

        // Copy Constructor and Copy Assignment deleted to forbid copying
        Socket(const Socket& other) = delete; 
        Socket& operator=(const Socket& other) = delete; 

        // Move Constructor and Move Assignment (use move semantics instead of copying)
        Socket(Socket&& other) noexcept; 
        Socket& operator=(Socket&& other) noexcept; 

        // Core networking
        bool bindNode(const std::string& ip, uint16_t port);
        bool listen(int backlog = SOMAXCONN); 
        bool connectTo(const std::string&ip, uint16_t port); 
        std::optional<Socket> acceptNode(); //Returns a new Socket object using move semantics; 

        // I/O Operations
        ssize_t sendData(const std::vector<uint8_t>&data) const; 
        ssize_t receiveData(std::vector<uint8_t>&buffer, size_t max_bytes) const; 

        // Getters
        [[nodiscard]] int getFd() const; 
        [[nodiscard]] bool isValid() const;  
    };
}

