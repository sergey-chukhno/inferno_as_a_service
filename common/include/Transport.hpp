#ifndef INFERNO_TRANSPORT_HPP
#define INFERNO_TRANSPORT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inferno {

enum class TransportType {
    TCP,      // Raw TCP with malleable C2 framing
    HTTP2     // TLS + HTTP/2 POST with malleable C2 in DATA frames
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool connect(const std::string& host, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual int recv(uint8_t* buf, size_t len) = 0;
    virtual int send(const uint8_t* buf, size_t len) = 0;

    virtual TransportType type() const = 0;
};

} // namespace inferno

#endif
