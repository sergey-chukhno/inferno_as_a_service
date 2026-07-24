#include "TlsTransport.hpp"

#include <cstring>
#include <cstdio>
#include <stdexcept>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

namespace inferno {

TlsTransport::TlsTransport()
    : m_ctx(nullptr)
    , m_ssl(nullptr)
    , m_fd(-1)
    , m_host()
    , m_port(0)
    , m_alpn_negotiated(false)
    , m_connected(false)
{
    m_ctx = SSL_CTX_new(TLS_method());
    if (!m_ctx) {
        std::fprintf(stderr, "[TlsTransport] SSL_CTX_new failed\n");
        return;
    }

    configureFingerprint();
}

TlsTransport::~TlsTransport() {
    disconnect();
    if (m_ctx) SSL_CTX_free(m_ctx);
}

TlsTransport::TlsTransport(TlsTransport&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_ssl(other.m_ssl)
    , m_fd(other.m_fd)
    , m_host(std::move(other.m_host))
    , m_port(other.m_port)
    , m_alpn_negotiated(other.m_alpn_negotiated)
    , m_connected(other.m_connected)
    , m_cipher_list(std::move(other.m_cipher_list))
    , m_groups_list(std::move(other.m_groups_list))
    , m_sigalgs_list(std::move(other.m_sigalgs_list))
    , m_alpn_protos(std::move(other.m_alpn_protos))
{
    other.m_ctx = nullptr;
    other.m_ssl = nullptr;
    other.m_fd = -1;
    other.m_connected = false;
    other.m_alpn_negotiated = false;
}

TlsTransport& TlsTransport::operator=(TlsTransport&& other) noexcept {
    if (this != &other) {
        disconnect();
        if (m_ctx) SSL_CTX_free(m_ctx);
        m_ctx = other.m_ctx;
        m_ssl = other.m_ssl;
        m_fd = other.m_fd;
        m_host = std::move(other.m_host);
        m_port = other.m_port;
        m_alpn_negotiated = other.m_alpn_negotiated;
        m_connected = other.m_connected;
        m_cipher_list = std::move(other.m_cipher_list);
        m_groups_list = std::move(other.m_groups_list);
        m_sigalgs_list = std::move(other.m_sigalgs_list);
        m_alpn_protos = std::move(other.m_alpn_protos);
        other.m_ctx = nullptr;
        other.m_ssl = nullptr;
        other.m_fd = -1;
        other.m_connected = false;
        other.m_alpn_negotiated = false;
    }
    return *this;
}

void TlsTransport::configureFingerprint() {
    // ── TLS 1.3 only (Chrome 120+ negotiates TLS 1.3 by default) ──
    SSL_CTX_set_min_proto_version(m_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(m_ctx, TLS1_3_VERSION);

    // ── TLS 1.3 cipher suites in Chrome 120+ order ──
    m_cipher_list = "TLS_AES_128_GCM_SHA256:"     // Chrome primary
                    "TLS_AES_256_GCM_SHA384:"     // Chrome secondary
                    "TLS_CHACHA20_POLY1305_SHA256"; // Chrome fallback (mobile)
    SSL_CTX_set_ciphersuites(m_ctx, m_cipher_list.c_str());

    // ── Supported groups in Chrome 120+ order ──
    m_groups_list = "X25519:P-256:P-384";
    SSL_CTX_set1_groups_list(m_ctx, m_groups_list.c_str());

    // ── Signature algorithms in Chrome 120+ order ──
    m_sigalgs_list = "ECDSA+SHA256:"
                     "RSA-PSS+SHA256:"
                     "RSA-PSS+SHA384:"
                     "RSA-PSS+SHA512:"
                     "RSA+SHA256:"
                     "RSA+SHA384:"
                     "RSA+SHA512:"
                     "ECDSA+SHA384";
    SSL_CTX_set1_sigalgs_list(m_ctx, m_sigalgs_list.c_str());

    // ── ALPN: h2 primary, http/1.1 fallback (matches Chrome) ──
    static const unsigned char alpn_data[] = {
        2, 'h', '2',             // length-prefixed "h2"
        8, 'h', 't', 't', 'p', '/', '1', '.', '1'  // length-prefixed "http/1.1"
    };
    m_alpn_protos.assign(alpn_data, alpn_data + sizeof(alpn_data));
    SSL_CTX_set_alpn_protos(m_ctx, m_alpn_protos.data(),
                            static_cast<unsigned int>(m_alpn_protos.size()));
}

bool TlsTransport::tcpConnect() {
    struct addrinfo hints{};
    struct addrinfo* result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(m_port);
    int err = ::getaddrinfo(m_host.c_str(), port_str.c_str(), &hints, &result);
    if (err != 0 || !result) {
        std::fprintf(stderr, "[TlsTransport] getaddrinfo(%s) failed: %s\n",
                     m_host.c_str(), ::gai_strerror(err));
        return false;
    }

    for (struct addrinfo* rp = result; rp; rp = rp->ai_next) {
        m_fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (m_fd < 0) continue;
        if (::connect(m_fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        ::close(m_fd);
        m_fd = -1;
    }
    ::freeaddrinfo(result);

    if (m_fd < 0) {
        std::fprintf(stderr, "[TlsTransport] TCP connect to %s:%u failed\n",
                     m_host.c_str(), m_port);
        return false;
    }

    return true;
}

bool TlsTransport::tlsHandshake() {
    if (!m_ctx) return false;

    m_ssl = SSL_new(m_ctx);
    if (!m_ssl) {
        std::fprintf(stderr, "[TlsTransport] SSL_new failed\n");
        return false;
    }

    SSL_set_fd(m_ssl, m_fd);
    SSL_set_tlsext_host_name(m_ssl, m_host.c_str());

    int ret = SSL_connect(m_ssl);
    if (ret != 1) {
        unsigned long err = ERR_get_error();
        std::fprintf(stderr, "[TlsTransport] SSL_connect failed: %s\n",
                     ERR_error_string(err, nullptr));
        SSL_free(m_ssl);
        m_ssl = nullptr;
        return false;
    }

    // ── Verify ALPN negotiated h2 ──
    const unsigned char* alpn_proto = nullptr;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(m_ssl, &alpn_proto, &alpn_len);
    m_alpn_negotiated = (alpn_proto != nullptr && alpn_len > 0);

    if (!m_alpn_negotiated) {
        std::fprintf(stderr, "[TlsTransport] ALPN negotiation failed — server doesn't support h2?\n");
        SSL_free(m_ssl);
        m_ssl = nullptr;
        return false;
    }

    return true;
}

bool TlsTransport::isAlpnH2() const {
    if (!m_alpn_negotiated || !m_ssl) return false;
    const unsigned char* alpn_proto = nullptr;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(m_ssl, &alpn_proto, &alpn_len);
    return (alpn_len == 2 && std::memcmp(alpn_proto, "h2", 2) == 0);
}

bool TlsTransport::connect(const std::string& host, uint16_t port) {
    if (m_connected) disconnect();

    m_host = host;
    m_port = port;

    if (!tcpConnect()) return false;
    if (!tlsHandshake()) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_connected = true;
    return true;
}

void TlsTransport::disconnect() {
    if (m_ssl) {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
        m_ssl = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_alpn_negotiated = false;
    m_connected = false;
}

bool TlsTransport::isConnected() const {
    return m_connected && m_ssl && m_fd >= 0;
}

int TlsTransport::recv(uint8_t* buf, size_t len) {
    if (!m_ssl) return -1;
    int ret = SSL_read(m_ssl, buf, static_cast<int>(len));
    if (ret <= 0) {
        int err = SSL_get_error(m_ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return 0; // non-blocking: no data available
        }
        return -1;
    }
    return ret;
}

int TlsTransport::send(const uint8_t* buf, size_t len) {
    if (!m_ssl) return -1;
    int ret = SSL_write(m_ssl, buf, static_cast<int>(len));
    if (ret <= 0) {
        int err = SSL_get_error(m_ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return 0; // non-blocking: would block
        }
        return -1;
    }
    return ret;
}

} // namespace inferno
