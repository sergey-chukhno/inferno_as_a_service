#include "Socket.hpp"
#include <utility> // std::move

namespace inferno {

// ─── Constructors / Destructor ───────────────────────────────────────────────

Socket::Socket() : m_socket_fd(-1), m_ip(""), m_port(0) {}

Socket::Socket(int fd, const std::string& ip, uint16_t port)
    : m_socket_fd(fd), m_ip(ip), m_port(port) {}

Socket::~Socket() {
    closeSocket();
}

// ─── Resource Management ─────────────────────────────────────────────────────

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
    // Nullify the donor so its destructor doesn't close our fd
    other.m_socket_fd = -1;
    other.m_port      = 0;
}

// Move Assignment Operator
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        // Release our current resource before stealing the new one
        closeSocket();

        m_socket_fd = other.m_socket_fd;
        m_ip        = std::move(other.m_ip);
        m_port      = other.m_port;

        other.m_socket_fd = -1;
        other.m_port      = 0;
    }
    return *this;
}

// ─── Core Networking ─────────────────────────────────────────────────────────

bool Socket::bindNode(const std::string& ip, uint16_t port) {
    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd == -1)
        return false;

    // Allow port reuse after a crash/restart
    int opt = 1;
    ::setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::bind(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket();
        return false;
    }

    m_ip   = ip;
    m_port = port;
    return true;
}

bool Socket::listen(int backlog) {
    if (!isValid())
        return false;

    return ::listen(m_socket_fd, backlog) >= 0;
}

bool Socket::connectTo(const std::string& ip, uint16_t port) {
    if (m_socket_fd != -1)
        return false; // Already in use

    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd == -1)
        return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::connect(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket();
        return false;
    }

    m_ip   = ip;
    m_port = port;
    return true;
}

std::optional<Socket> Socket::acceptNode() {
    if (!isValid())
        return std::nullopt;

    struct sockaddr_in client_addr{};
    socklen_t          addr_len = sizeof(client_addr);

    int client_fd = ::accept(m_socket_fd,
                             reinterpret_cast<struct sockaddr*>(&client_addr),
                             &addr_len);
    if (client_fd == -1)
        return std::nullopt;

    char client_ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    return Socket(client_fd, client_ip, ntohs(client_addr.sin_port));
}

// ─── I/O Operations ──────────────────────────────────────────────────────────

ssize_t Socket::sendData(const std::vector<uint8_t>& data) const {
    if (!isValid())
        return -1;
    return ::send(m_socket_fd, data.data(), data.size(), 0);
}

ssize_t Socket::receiveData(std::vector<uint8_t>& buffer, size_t max_bytes) const {
    if (!isValid())
        return -1;

    buffer.resize(max_bytes);
    ssize_t read_bytes = ::recv(m_socket_fd, buffer.data(), max_bytes, 0);

    if (read_bytes > 0)
        buffer.resize(static_cast<size_t>(read_bytes));
    else
        buffer.clear();

    return read_bytes;
}

// ─── Getters ─────────────────────────────────────────────────────────────────

int         Socket::getFd()   const { return m_socket_fd; }
bool        Socket::isValid() const { return m_socket_fd != -1; }
std::string Socket::getIp()   const { return m_ip; }
uint16_t    Socket::getPort() const { return m_port; }

} // namespace inferno