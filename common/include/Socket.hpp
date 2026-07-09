#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <optional>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <basetsd.h>
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

class Packet;

class Socket {
private:
    socket_t    m_socket_fd;
    std::string m_ip;
    uint16_t    m_port;

    // Malleable C2 session key (set via greeting exchange or externally)
    bool     m_malleable = false;
    uint8_t  m_session_key[16]{};
    uint64_t m_send_counter = 0;
    uint64_t m_recv_counter = 0;

public:
    Socket();
    Socket(socket_t fd, const std::string& ip, uint16_t port);
    ~Socket();

    Socket(const Socket& other)            = delete;
    Socket& operator=(const Socket& other) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Core networking
    bool                  bindNode(const std::string& ip, uint16_t port);
    bool                  listen(int backlog = SOMAXCONN);
    bool                  connectTo(const std::string& ip, uint16_t port,
                                    bool expectGreeting = true);
    std::optional<Socket> acceptNode();

    // Raw I/O (for greeting exchange before packet framing)
    bool     sendRaw(const uint8_t* data, size_t len) const;
    ssize_t  receiveRaw(uint8_t* buf, size_t max_len) const;

    // Packet I/O (uses malleable format when session key is negotiated)
    ssize_t sendPacket(uint16_t opcode, const std::string& payload);
    std::optional<class Packet> receivePacket(std::vector<uint8_t>& buffer);

    // Direct I/O (raw bytes, no framing)
    ssize_t sendData(const std::vector<uint8_t>& data) const;
    ssize_t receiveData(std::vector<uint8_t>& buffer, size_t max_bytes) const;

    // Session key management (for malleable C2 framing)
    void                 setSessionKey(const uint8_t* key, size_t len);
    bool                 hasSessionKey() const;

    // Timeouts & keepalive
    bool setReceiveTimeout(unsigned seconds);
    bool setKeepAlive(unsigned idle_sec, unsigned interval_sec);

    void close() noexcept;

    // Getters
    [[nodiscard]] socket_t    getFd()    const;
    [[nodiscard]] bool        isValid()  const;
    [[nodiscard]] std::string getIp()    const;
    [[nodiscard]] uint16_t    getPort()  const;
};

} // namespace inferno