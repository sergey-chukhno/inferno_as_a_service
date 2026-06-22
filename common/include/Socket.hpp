#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <basetsd.h> // For SSIZE_T
using socket_t = SOCKET;
using ssize_t = SSIZE_T;
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace inferno {

class Socket {
private:
    socket_t    m_socket_fd;
    std::string m_ip;
    uint16_t    m_port;

public:
    // Default constructor required to satisfy Coplien Canonical Form
    Socket();

    // Custom constructor used internally by acceptNode()
    Socket(socket_t fd, const std::string& ip, uint16_t port);

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
    std::optional<Socket> acceptNode();
    // Returns a new Socket via move semantics

    // I/O Operations
    ssize_t sendData(const std::vector<uint8_t>& data) const;
    ssize_t receiveData(std::vector<uint8_t>& buffer, size_t max_bytes) const;

    // Closes the socket and marks it as invalid
    void close() noexcept;

    // Timeouts & keepalive for dead-connection detection
    bool setReceiveTimeout(unsigned seconds);
    bool setKeepAlive(unsigned idle_sec, unsigned interval_sec);

    // Getters
    [[nodiscard]] socket_t    getFd()    const;
    [[nodiscard]] bool        isValid()  const;
    [[nodiscard]] std::string getIp()    const;
    [[nodiscard]] uint16_t    getPort()  const;
};

} // namespace inferno