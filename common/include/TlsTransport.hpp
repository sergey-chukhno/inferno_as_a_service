#ifndef INFERNO_TLS_TRANSPORT_HPP
#define INFERNO_TLS_TRANSPORT_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

#include "Socket.hpp"
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

    // ── Testing support ────────────────────────────────────────────
    struct ssl_ctx_st* context() const { return m_ctx; }
    const std::string& cipherList() const { return m_cipher_list; }
    const std::string& groupsList() const { return m_groups_list; }
    const std::string& sigalgsList() const { return m_sigalgs_list; }
    const unsigned char* alpnProtos() const { return m_alpn_protos.data(); }
    size_t alpnProtosLen() const { return m_alpn_protos.size(); }

private:
    struct ssl_ctx_st* m_ctx;
    struct ssl_st*     m_ssl;
    socket_t           m_fd;
    std::string        m_host;
    uint16_t           m_port;
    bool               m_alpn_negotiated;
    bool               m_connected;

    std::string              m_cipher_list;
    std::string              m_groups_list;
    std::string              m_sigalgs_list;
    std::vector<unsigned char> m_alpn_protos;

    void configureFingerprint();
    bool tcpConnect();
    bool tlsHandshake();
};

} // namespace inferno

#endif
