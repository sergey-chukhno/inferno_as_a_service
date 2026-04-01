#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

// WSL/Linux headers
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

namespace inferno {

class Socket {
private:
    int         m_socket_fd;
    std::string m_ip;
    uint16_t    m_port;

    // Helper function to safely close the file descriptor
    void closeSocket() noexcept;

public:
    // Default constructor required to satisfy Coplien Canonical Form
    Socket();

    // Custom constructor used internally by acceptNode()
    Socket(int fd, const std::string& ip, uint16_t port);

    // Destructor
    ~Socket();

    // Copy semantics are deleted: a Socket owns a unique fd,
    // copying would lead to a double-close. Use std::move instead.
    Socket(const Socket& other)            = delete;
    Socket& operator=(const Socket& other) = delete;

    // Move Constructor and Move Assignment (transfers ownership of the fd)
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Core networking
    bool                  bindNode(const std::string& ip, uint16_t port);
    bool                  listen(int backlog = SOMAXCONN);
    bool                  connectTo(const std::string& ip, uint16_t port);
    std::optional<Socket> acceptNode(); // Returns a new Socket via move semantics

    // I/O Operations
    ssize_t sendData(const std::vector<uint8_t>& data) const;
    ssize_t receiveData(std::vector<uint8_t>& buffer, size_t max_bytes) const;

    // Getters
    [[nodiscard]] int         getFd()    const;
    [[nodiscard]] bool        isValid()  const;
    [[nodiscard]] std::string getIp()    const;
    [[nodiscard]] uint16_t    getPort()  const;
};

} // namespace inferno