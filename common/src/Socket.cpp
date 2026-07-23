#include "Socket.hpp"
#include "Packet.hpp"
#include "CryptoContext.hpp"
#include <utility>
#include <string>
#include <optional>

#ifdef _WIN32
static bool initWinsock() {
    struct WinsockManager {
        bool initialized = false;
        WinsockManager() {
            WSADATA wsaData{};
            initialized = (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        }
        ~WinsockManager() {
            if (initialized) {
                ::WSACleanup();
            }
        }
    };
    static WinsockManager manager;
    return manager.initialized;
}
#endif

namespace inferno {

// Default Constructor
Socket::Socket() : m_socket_fd(INVALID_SOCKET), m_ip(""), m_port(0) {
#ifdef _WIN32
    initWinsock();
#endif
}

// Custom Constructor for Accept() overrides
Socket::Socket(socket_t fd, const std::string& ip, uint16_t port)
    : m_socket_fd(fd), m_ip(ip), m_port(port) {
#ifdef _WIN32
    initWinsock();
#endif
}

// Destructor
Socket::~Socket() {
    close();
}

// Safe Closure Hook
void Socket::close() noexcept {
    if (m_socket_fd != INVALID_SOCKET) {
#ifdef _WIN32
        ::closesocket(m_socket_fd);
#else
        ::close(m_socket_fd);
#endif
        m_socket_fd = INVALID_SOCKET;
    }
}

// Move Constructor
Socket::Socket(Socket&& other) noexcept
    : m_socket_fd(other.m_socket_fd),
      m_ip(std::move(other.m_ip)),
      m_port(other.m_port),
      m_malleable(other.m_malleable),
      m_send_counter(other.m_send_counter),
      m_recv_counter(other.m_recv_counter)
{
    std::memcpy(m_session_key, other.m_session_key, sizeof(m_session_key));
    other.m_socket_fd = INVALID_SOCKET;
    other.m_port = 0;
    other.m_malleable = false;
    other.m_send_counter = 0;
    other.m_recv_counter = 0;
    std::memset(other.m_session_key, 0, sizeof(other.m_session_key));
}

// Move Assignment Operator
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        m_socket_fd = other.m_socket_fd;
        m_ip = std::move(other.m_ip);
        m_port = other.m_port;
        m_malleable = other.m_malleable;
        m_send_counter = other.m_send_counter;
        m_recv_counter = other.m_recv_counter;
        std::memcpy(m_session_key, other.m_session_key, sizeof(m_session_key));

        other.m_socket_fd = INVALID_SOCKET;
        other.m_port = 0;
        other.m_malleable = false;
        other.m_send_counter = 0;
        other.m_recv_counter = 0;
        std::memset(other.m_session_key, 0, sizeof(other.m_session_key));
    }
    return *this;
}

// Core Networking Logic

bool Socket::bindNode(const std::string& ip, uint16_t port) {
#ifdef _WIN32
    if (!initWinsock()) {
        return false;
    }
#endif
    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0); 

    if (m_socket_fd == INVALID_SOCKET) {
        return false;
    }
#ifndef _WIN32
    int opt = 1; 
    ::setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::bind(m_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        close(); //we clean it if bind fails
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

    if (::listen(m_socket_fd, backlog) == SOCKET_ERROR) {
        return false;
    }
    return true; 
}

bool Socket::connectTo(const std::string& ip, uint16_t port, bool expectGreeting) {
    if (m_socket_fd != INVALID_SOCKET) {
        return false;
    }
#ifdef _WIN32
    if (!initWinsock()) {
        return false;
    }
#endif
    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd == INVALID_SOCKET) {
        return false;
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (::connect(m_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        close();
        return false;
    }
    m_ip = ip;
    m_port = port;

    setReceiveTimeout(10);
    setKeepAlive(30, 10);

    if (expectGreeting) {
        // Malleable C2 greeting exchange: read 64 random bytes from server
        uint8_t greeting[CryptoContext::GREETING_SIZE];
        size_t total = 0;
        while (total < sizeof(greeting)) {
            int recv_len = static_cast<int>(sizeof(greeting) - total);
            ssize_t n = ::recv(m_socket_fd,
                               reinterpret_cast<char*>(greeting) + total,
                               recv_len, 0);
            if (n <= 0) break;
            total += static_cast<size_t>(n);
        }
        if (total != sizeof(greeting)) {
            std::fprintf(stderr, "[Socket] Greeting read failed: got %zu of %zu bytes\n",
                         total, sizeof(greeting));
            close();
            return false;
        }

        auto key = CryptoContext::deriveSessionKey(greeting);
        if (key.size() != sizeof(m_session_key)) {
            std::fprintf(stderr, "[Socket] Greeting key derivation failed (%zu bytes)\n",
                         key.size());
            close();
            return false;
        }
        setSessionKey(key.data(), key.size());
        std::fprintf(stdout, "[Socket] Malleable session established (key=%zu bytes)\n",
                     key.size());
    }

    return true;
}

std::optional<Socket> Socket::acceptNode() {
    if (!isValid()) {
        return std::nullopt;
    }
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    socket_t client_fd = ::accept(m_socket_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKET) {
        return std::nullopt;
    }
    char client_ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    return Socket(client_fd, client_ip, ntohs(client_addr.sin_port));
}


// I/O Operations

bool Socket::sendRaw(const uint8_t* data, size_t len) const {
    if (!isValid()) return false;
    int send_len = static_cast<int>(len);
    if (static_cast<size_t>(send_len) != len) return false; // truncation check
    ssize_t sent = ::send(m_socket_fd,
                          reinterpret_cast<const char*>(data), send_len, 0);
    return sent == static_cast<ssize_t>(send_len);
}

ssize_t Socket::receiveRaw(uint8_t* buf, size_t max_len) const {
    if (!isValid()) return -1;
    int recv_len = static_cast<int>(max_len);
    if (static_cast<size_t>(recv_len) != max_len) return -1;
    return ::recv(m_socket_fd, reinterpret_cast<char*>(buf), recv_len, 0);
}

ssize_t Socket::sendPacket(uint16_t opcode, const std::string& payload) {
    if (!isValid()) return -1;
    std::vector<uint8_t> data;
    if (m_malleable) {
        Packet p(opcode, payload, m_session_key, m_send_counter);
        data = p.serialize();
        ++m_send_counter;
    } else {
        Packet p(opcode, payload);
        data = p.serialize();
    }
    if (data.empty()) return -1;
    int send_len = static_cast<int>(data.size());
    if (static_cast<size_t>(send_len) != data.size()) return -1;
    return ::send(m_socket_fd,
                  reinterpret_cast<const char*>(data.data()),
                  send_len, 0);
}

std::optional<Packet> Socket::receivePacket(std::vector<uint8_t>& buffer) {
    if (!isValid()) return std::nullopt;
    if (m_malleable) {
        auto result = Packet::deserialize(buffer, m_session_key, m_recv_counter);
        if (result.has_value()) ++m_recv_counter;
        return result;
    }
    return Packet::deserialize(buffer, nullptr, 0);
}

void Socket::setSessionKey(const uint8_t* key, size_t len) {
    if (key == nullptr || len != sizeof(m_session_key)) {
        std::memset(m_session_key, 0, sizeof(m_session_key));
        m_malleable = false;
        m_send_counter = 0;
        m_recv_counter = 0;
        return;
    }
    std::memcpy(m_session_key, key, sizeof(m_session_key));
    m_send_counter = 0;
    m_recv_counter = 0;
    m_malleable = true;
}

bool Socket::hasSessionKey() const { return m_malleable; }

ssize_t Socket::sendData(const std::vector<uint8_t>& data) const {
    if (!isValid()) {
        return -1;
    }
#ifdef _WIN32
    int len = static_cast<int>(data.size());
#else
    size_t len = data.size();
#endif
    ssize_t send_bytes = ::send(m_socket_fd, reinterpret_cast<const char*>(data.data()), len, 0); 
    return send_bytes; 
}

ssize_t Socket::receiveData(std::vector<uint8_t>& buffer, size_t max_bytes) const {
    if (!isValid()) {
        return -1;
    }
    buffer.resize(max_bytes);
#ifdef _WIN32
    int len = static_cast<int>(max_bytes);
#else
    size_t len = max_bytes;
#endif
    ssize_t read_bytes = ::recv(m_socket_fd, reinterpret_cast<char*>(buffer.data()), len, 0);

    if (read_bytes > 0) {
        buffer.resize(read_bytes);
    } else {
        buffer.clear();
    }
    return read_bytes;
}
// Getters

bool Socket::setReceiveTimeout(unsigned seconds) {
    if (m_socket_fd == INVALID_SOCKET) return false;
#if defined(_WIN32)
    DWORD tv = seconds * 1000;
#else
    struct timeval tv;
    tv.tv_sec = static_cast<long>(seconds);
    tv.tv_usec = 0;
#endif
    return ::setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                        reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
}

bool Socket::setKeepAlive(unsigned idle_sec, unsigned interval_sec) {
    if (m_socket_fd == INVALID_SOCKET) return false;
    int opt = 1;
    if (::setsockopt(m_socket_fd, SOL_SOCKET, SO_KEEPALIVE,
                     reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return false;
    }
#if defined(_WIN32)
    (void)idle_sec;
    (void)interval_sec;
#endif
#if defined(__APPLE__)
    opt = static_cast<int>(idle_sec);
    ::setsockopt(m_socket_fd, IPPROTO_TCP, TCP_KEEPALIVE,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));
    opt = static_cast<int>(interval_sec);
    ::setsockopt(m_socket_fd, IPPROTO_TCP, TCP_KEEPINTVL,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));
#elif defined(__linux__)
    opt = static_cast<int>(idle_sec);
    ::setsockopt(m_socket_fd, SOL_TCP, TCP_KEEPIDLE,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));
    opt = static_cast<int>(interval_sec);
    ::setsockopt(m_socket_fd, SOL_TCP, TCP_KEEPINTVL,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif
    return true;
}

socket_t Socket::getFd() const {
    return m_socket_fd;
}

bool Socket::isValid() const {
    return m_socket_fd != INVALID_SOCKET;
}

std::string Socket::getIp() const {
    return m_ip;
}

uint16_t Socket::getPort() const {
    return m_port;
}

// ── ITransport interface ──────────────────────────────────────────

bool Socket::connect(const std::string& host, uint16_t port) {
    return connectTo(host, port, true);
}

void Socket::disconnect() {
    close();
}

bool Socket::isConnected() const {
    return isValid();
}

int Socket::recv(uint8_t* buf, size_t len) {
    return static_cast<int>(receiveRaw(buf, len));
}

int Socket::send(const uint8_t* buf, size_t len) {
    return sendRaw(buf, len) ? static_cast<int>(len) : -1;
}

} // namespace inferno
