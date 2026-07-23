#ifndef INFERNO_TLS_TRANSPORT_HPP
#define INFERNO_TLS_TRANSPORT_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

#include "Transport.hpp"

struct ssl_ctx_st;
struct ssl_st;

namespace inferno {

class TlsTransport : public ITransport {
public:
    TlsTransport();
    ~TlsTransport() override;

    TlsTransport(const TlsTransport&) = delete;
    TlsTransport& operator=(const TlsTransport&) = delete;
    TlsTransport(TlsTransport&& other) noexcept;
    TlsTransport& operator=(TlsTransport&& other) noexcept;

    bool connect(const std::string& host, uint16_t port) override;
    void disconnect() override;
    bool isConnected() const override;

    int recv(uint8_t* buf, size_t len) override;
    int send(const uint8_t* buf, size_t len) override;

    TransportType type() const override { return TransportType::HTTP2; }

    bool hasAlpn() const { return m_alpn_negotiated; }
    bool isAlpnH2() const;

private:
    struct ssl_ctx_st* m_ctx;
    struct ssl_st*     m_ssl;
    int                m_fd;
    std::string        m_host;
    uint16_t           m_port;
    bool               m_alpn_negotiated;
    bool               m_connected;

    void configureFingerprint();
    bool tcpConnect();
    bool tlsHandshake();
};

} // namespace inferno

#endif
