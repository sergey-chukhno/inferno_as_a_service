#include "../../include/network/Http2Server.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdint>

#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace inferno {

static const char* SERVER_CERT = "server.crt";
static const char* SERVER_KEY  = "server.key";

// ── ALPN select callback (plain C function) ──────────────────────
static int alpn_select_cb(SSL* /*ssl*/, const unsigned char** out,
                           unsigned char* outlen, const unsigned char* in,
                           unsigned int inlen, void* /*arg*/) {
    // Manual ALPN selection: server offers h2, check if client supports it
    // in contains length-prefixed protocol list from client
    for (unsigned int i = 0; i + 1 < inlen; ) {
        unsigned int proto_len = in[i];
        if (i + 1 + proto_len > inlen) break;
        if (proto_len == 2 && in[i + 1] == 'h' && in[i + 2] == '2') {
            *out = in + i + 1;
            *outlen = 2;
            return SSL_TLSEXT_ERR_OK;
        }
        i += 1 + proto_len;
    }
    return SSL_TLSEXT_ERR_NOACK;
}

// ═══════════════════════════════════════════════════════════════════
// nghttp2 callbacks
// ═══════════════════════════════════════════════════════════════════

int Http2Server::onBeginHeaders(nghttp2_session* /*session*/,
                                 const nghttp2_frame* frame,
                                 void* /*user_data*/) {
    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
        std::printf("[Http2Server] HEADERS stream=%d\n", frame->hd.stream_id);
    }
    return 0;
}

int Http2Server::onFrameRecv(nghttp2_session* session,
                              const nghttp2_frame* frame,
                              void* user_data) {
    auto* self = static_cast<Http2Server*>(user_data);
    if (!self) return 0;
    if (frame->hd.type == NGHTTP2_SETTINGS &&
        !(frame->hd.flags & NGHTTP2_FLAG_ACK)) {
        nghttp2_submit_settings(session, NGHTTP2_FLAG_ACK, nullptr, 0);
    }
    return 0;
}

int Http2Server::onDataChunkRecv(nghttp2_session* /*session*/,
                                  uint8_t /*flags*/, int32_t /*stream_id*/,
                                  const uint8_t* data, size_t len,
                                  void* user_data) {
    auto* self = static_cast<Http2Server*>(user_data);
    if (!self || !self->m_on_data) return 0;
    self->m_on_data(data, len);
    return 0;
}

ssize_t Http2Server::onSendCallback(nghttp2_session* /*session*/,
                                     const uint8_t* data, size_t len,
                                     int /*flags*/, void* user_data) {
    auto* self = static_cast<Http2Server*>(user_data);
    if (!self || !self->m_ssl) return -1;
    int written = SSL_write(self->m_ssl, data, static_cast<int>(len));
    if (written <= 0) return -1;
    return written;
}

// ═══════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════

Http2Server::Http2Server()
    : m_listen_fd(-1), m_running(false), m_session(nullptr)
    , m_callbacks(nullptr), m_ctx(nullptr), m_ssl(nullptr)
    , m_client_fd(-1) {}

Http2Server::~Http2Server() { stop(); }

bool Http2Server::setupTls() {
    m_ctx = SSL_CTX_new(TLS_server_method());
    if (!m_ctx) return false;

    if (SSL_CTX_use_certificate_file(m_ctx, SERVER_CERT, SSL_FILETYPE_PEM) != 1) {
        std::fprintf(stderr, "[Http2Server] Failed to load cert: %s\n", SERVER_CERT);
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(m_ctx, SERVER_KEY, SSL_FILETYPE_PEM) != 1) {
        std::fprintf(stderr, "[Http2Server] Failed to load key: %s\n", SERVER_KEY);
        return false;
    }

    SSL_CTX_set_min_proto_version(m_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(m_ctx, TLS1_3_VERSION);
    SSL_CTX_set_alpn_select_cb(m_ctx, alpn_select_cb, nullptr);
    return true;
}

bool Http2Server::acceptClient() {
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    m_client_fd = ::accept(m_listen_fd,
                           reinterpret_cast<struct sockaddr*>(&client_addr),
                           &addr_len);
    if (m_client_fd < 0) return false;

    m_ssl = SSL_new(m_ctx);
    if (!m_ssl) { ::close(m_client_fd); m_client_fd = -1; return false; }
    SSL_set_fd(m_ssl, m_client_fd);

    if (SSL_accept(m_ssl) != 1) {
        SSL_free(m_ssl); m_ssl = nullptr;
        ::close(m_client_fd); m_client_fd = -1;
        return false;
    }

    nghttp2_session_callbacks_new(&m_callbacks);
    nghttp2_session_callbacks_set_on_begin_headers_callback(m_callbacks, onBeginHeaders);
    nghttp2_session_callbacks_set_on_frame_recv_callback(m_callbacks, onFrameRecv);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(m_callbacks, onDataChunkRecv);
    nghttp2_session_callbacks_set_send_callback(m_callbacks, onSendCallback);

    if (nghttp2_session_server_new(&m_session, m_callbacks, this) != 0) {
        nghttp2_session_callbacks_del(m_callbacks); m_callbacks = nullptr;
        SSL_free(m_ssl); m_ssl = nullptr;
        ::close(m_client_fd); m_client_fd = -1;
        return false;
    }

    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1000},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 6291456},
    };
    nghttp2_submit_settings(m_session, NGHTTP2_FLAG_NONE, settings, 2);

    char ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    std::printf("[Http2Server] Client: %s\n", ip);
    return true;
}

bool Http2Server::start(uint16_t port, DataCallback on_data) {
    if (m_running) stop();
    m_on_data = std::move(on_data);
    if (!setupTls()) return false;

    m_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listen_fd < 0) return false;

    int opt = 1;
    ::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(m_listen_fd, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        ::close(m_listen_fd); m_listen_fd = -1;
        return false;
    }
    if (::listen(m_listen_fd, SOMAXCONN) < 0) {
        ::close(m_listen_fd); m_listen_fd = -1;
        return false;
    }

    m_running = true;
    std::printf("[Http2Server] Listening on port %u (HTTP/2 over TLS)\n", port);

    if (!acceptClient()) { m_running = false; return false; }

    uint8_t buf[4096];
    while (m_running) {
        int nread = SSL_read(m_ssl, buf, sizeof(buf));
        if (nread <= 0) {
            int err = SSL_get_error(m_ssl, nread);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
            break;
        }
        nghttp2_session_mem_recv(m_session, buf, nread);
        nghttp2_session_send(m_session);
    }

    stop();
    return true;
}

void Http2Server::stop() {
    m_running = false;
    if (m_session) { nghttp2_session_del(m_session); m_session = nullptr; }
    if (m_callbacks) { nghttp2_session_callbacks_del(m_callbacks); m_callbacks = nullptr; }
    if (m_ssl) { SSL_shutdown(m_ssl); SSL_free(m_ssl); m_ssl = nullptr; }
    if (m_client_fd >= 0) { ::close(m_client_fd); m_client_fd = -1; }
    if (m_listen_fd >= 0) { ::close(m_listen_fd); m_listen_fd = -1; }
    if (m_ctx) { SSL_CTX_free(m_ctx); m_ctx = nullptr; }
}

bool Http2Server::isRunning() const { return m_running; }

int Http2Server::sendResponse(const uint8_t* /*data*/, size_t len) {
    if (!m_session || !m_ssl) return -1;
    nghttp2_nv headers[] = {
        {(uint8_t*)":status", (uint8_t*)"200", 7, 3, NGHTTP2_NV_FLAG_NONE},
    };
    nghttp2_submit_headers(m_session, NGHTTP2_FLAG_END_HEADERS, 1, nullptr,
                           headers, 1, nullptr);
    // For a simple response, we store payload as static data
    // The data provider would need the same pattern as the client
    // For MVP, just submit an empty response
    nghttp2_submit_data(m_session, NGHTTP2_DATA_FLAG_EOF, 1, nullptr);
    nghttp2_session_send(m_session);
    return static_cast<int>(len);
}

} // namespace inferno
