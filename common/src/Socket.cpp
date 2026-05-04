#include "Socket.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <utility> // For std::move
#include <string>
#include <optional>
#include <arpa/inet.h>
#include <netinet/in.h>

namespace inferno {

// Default Constructor
Socket::Socket() : m_socket_fd(-1), m_ip(""), m_port(0) {}

// Custom Constructor for Accept() overrides
Socket::Socket(int fd, const std::string& ip, uint16_t port)
    : m_socket_fd(fd), m_ip(ip), m_port(port) {}

// Destructor
Socket::~Socket() {
    closeSocket();
}

// Safe Closure Hook
void Socket::closeSocket() noexcept {
    if (m_socket_fd != -1) {
        ::close(m_socket_fd);
        m_socket_fd = -1;
    }
}

// Move Constructor
Socket::Socket(Socket&& other) noexcept
    : m_socket_fd(other.m_socket_fd),
      m_ip(std::move(other.m_ip)),
      m_port(other.m_port) 
{
    // We "steal" the resources and nullify the donor's socket file descriptor. 
    // If we don't nullify 'other.m_socket_fd', its destructor will close the socket!
    other.m_socket_fd = -1;
    other.m_port = 0;
}

// 6. Move Assignment Operator
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) { // Guard against self-assignment: Socket A = std::move(A);
        
        // Step 1: Clean up our EXISTING resource before stealing a new one.
        closeSocket(); 

        // Step 2: Steal the donor's resources
        m_socket_fd = other.m_socket_fd;
        m_ip = std::move(other.m_ip);
        m_port = other.m_port;

        // Step 3: Nullify the donor
        other.m_socket_fd = -1;
        other.m_port = 0;
    }
    return *this;
}

// Core Networking Logic

bool Socket::bindNode(const std::string& ip, uint16_t port) {
    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0); 

    if (m_socket_fd == -1) {
        return false;
    }
    int opt = 1; 
    ::setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::bind(m_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closeSocket(); //we clean it if bind fails
        return false;
    }
    
    m_ip = ip;
    if (port == 0) {
        socklen_t len = sizeof(addr);
        if (::getsockname(m_socket_fd, (struct sockaddr *)&addr, &len) == 0) {
            m_port = ntohs(addr.sin_port);
        } else {
            m_port = port;
        }
    } else {
        m_port = port;
    }
    
    return true; 
}

bool Socket::listen(int backlog) {
    if (!isValid()) {
        return false;
    }

    if (::listen(m_socket_fd, backlog) < 0) {
        return false;
    }
    return true; 
}

bool Socket::connectTo(const std::string& ip, uint16_t port) {
    if (m_socket_fd != -1) {
        return false;
    }
    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd == -1) {
        return false;
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (::connect(m_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closeSocket();
        return false;
    }
    m_ip = ip;
    m_port = port;
    return true; 
}

std::optional<Socket> Socket::acceptNode() {
    if (!isValid()) {
        return std::nullopt;
    }
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = ::accept(m_socket_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd == -1) {
        return std::nullopt;
    }
    char client_ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    return Socket(client_fd, client_ip, ntohs(client_addr.sin_port));
}


// I/O Operations

ssize_t Socket::sendData(const std::vector<uint8_t>& data) const {
    if (!isValid()) {
        return -1;
    }
    ssize_t send_bytes = ::send(m_socket_fd, data.data(), data.size(), 0); 
    return send_bytes; 
}

ssize_t Socket::receiveData(std::vector<uint8_t>& buffer, size_t max_bytes) const {
    if (!isValid()) {
        return -1;
    }
    buffer.resize(max_bytes);
    ssize_t read_bytes = ::recv(m_socket_fd, buffer.data(), max_bytes, 0);

    if (read_bytes > 0) {
        buffer.resize(read_bytes);
    } else {
        buffer.clear();
    }
    return read_bytes;
}
// Getters

int Socket::getFd() const {
    return m_socket_fd;
}

bool Socket::isValid() const {
    return m_socket_fd != -1;
}

std::string Socket::getIp() const {
    return m_ip;
}

uint16_t Socket::getPort() const {
    return m_port;
}

} // namespace inferno
