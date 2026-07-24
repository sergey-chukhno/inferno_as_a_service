#include "../include/Http2Client.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

#include <nghttp2/nghttp2.h>

#include "../../common/include/TlsTransport.hpp"

namespace inferno {

// ── Chrome 120+ HTTP/2 SETTINGS (captured from Wireshark) ─────────
const nghttp2_settings_entry Http2Client::CHROME_SETTINGS[6] = {
    {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 65536},
    {NGHTTP2_SETTINGS_ENABLE_PUSH, 1},
    {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1000},
    {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 6291456},
    {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 16384},
    {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, 262144},
};

// ── Realistic POST headers (Chrome-like) ───────────────────────────
static const nghttp2_nv REQUEST_HEADERS[] = {
    {(uint8_t*)":method", (uint8_t*)":method", 7, 7, NGHTTP2_NV_FLAG_NONE},
    {(uint8_t*)":path", (uint8_t*)"/api/v2/telemetry", 4, 18, NGHTTP2_NV_FLAG_NONE},
    {(uint8_t*)":scheme", (uint8_t*)":scheme", 6, 6, NGHTTP2_NV_FLAG_NONE},
    {(uint8_t*)":authority", (uint8_t*)":authority", 10, 10, NGHTTP2_NV_FLAG_NONE},
    {(uint8_t*)"user-agent", (uint8_t*)"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36", 10, 116, NGHTTP2_NV_FLAG_NO_INDEX},
    {(uint8_t*)"content-type", (uint8_t*)"application/octet-stream", 12, 24, NGHTTP2_NV_FLAG_NONE},
    {(uint8_t*)"accept", (uint8_t*)"*/*", 6, 3, NGHTTP2_NV_FLAG_NONE},
};
static const size_t NUM_REQUEST_HEADERS = 7;

// ═══════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════

Http2Client::Http2Client()
    : m_transport(std::make_unique<TlsTransport>())
    , m_session(nullptr)
    , m_callbacks(nullptr)
    , m_stream_id(-1)
    , m_connected(false)
{
}

Http2Client::~Http2Client() {
    disconnect();
}

Http2Client::Http2Client(Http2Client&&) noexcept = default;
Http2Client& Http2Client::operator=(Http2Client&&) noexcept = default;

// ═══════════════════════════════════════════════════════════════════
// Static nghttp2 callbacks
// ═══════════════════════════════════════════════════════════════════

int Http2Client::onFrameRecv(nghttp2_session* /*session*/,
                              const nghttp2_frame* frame,
                              void* user_data) {
    auto* self = static_cast<Http2Client*>(user_data);
    if (!self) return 0;
    if (frame->hd.type == NGHTTP2_SETTINGS && (frame->hd.flags & NGHTTP2_FLAG_ACK)) {
        std::printf("[Http2Client] Server acknowledged our SETTINGS\n");
    }
    return 0;
}

int Http2Client::onDataChunkRecv(nghttp2_session* /*session*/,
                                  uint8_t /*flags*/, int32_t /*stream_id*/,
                                  const uint8_t* data, size_t len,
                                  void* user_data) {
    auto* self = static_cast<Http2Client*>(user_data);
    if (!self) return 0;
    self->m_recv_buf.insert(self->m_recv_buf.end(), data, data + len);
    return 0;
}

ssize_t Http2Client::onSendCallback(nghttp2_session* /*session*/,
                                     const uint8_t* data, size_t len,
                                     int /*flags*/, void* user_data) {
    auto* self = static_cast<Http2Client*>(user_data);
    if (!self) return -1;
    // Write raw HTTP/2 bytes to the TLS transport
    int written = self->m_transport->send(data, len);
    if (written < 0) return -1;
    return written;
}

ssize_t Http2Client::onDataProviderRead(nghttp2_session* /*session*/,
                                         int32_t /*stream_id*/,
                                         uint8_t* buf, size_t len,
                                         uint32_t* data_flags,
                                         nghttp2_data_source* source,
                                         void* /*user_data*/) {
    auto* vec = static_cast<std::vector<uint8_t>*>(source->ptr);
    if (!vec || vec->empty()) {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }
    size_t to_copy = std::min(len, vec->size());
    std::memcpy(buf, vec->data(), to_copy);
    vec->erase(vec->begin(), vec->begin() + to_copy);
    if (vec->empty()) {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
    }
    return static_cast<ssize_t>(to_copy);
}

// ═══════════════════════════════════════════════════════════════════
// ITransport accessor
// ═══════════════════════════════════════════════════════════════════

ITransport& Http2Client::transport() {
    return *static_cast<ITransport*>(m_transport.get());
}

// ═══════════════════════════════════════════════════════════════════
// Connection management
// ═══════════════════════════════════════════════════════════════════

bool Http2Client::connect(const std::string& host, uint16_t port) {
    if (m_connected) disconnect();

    if (!m_transport->connect(host, port)) {
        std::fprintf(stderr, "[Http2Client] TLS connect to %s:%u failed\n",
                     host.c_str(), port);
        return false;
    }

    if (!m_transport->isAlpnH2()) {
        std::fprintf(stderr, "[Http2Client] ALPN negotiation failed — server didn't select h2\n");
        m_transport->disconnect();
        return false;
    }

    nghttp2_session_callbacks_new(&m_callbacks);
    nghttp2_session_callbacks_set_on_frame_recv_callback(m_callbacks, onFrameRecv);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(m_callbacks, onDataChunkRecv);
    nghttp2_session_callbacks_set_send_callback(m_callbacks, onSendCallback);

    int rv = nghttp2_session_client_new(&m_session, m_callbacks, this);
    if (rv != 0) {
        std::fprintf(stderr, "[Http2Client] nghttp2_session_client_new failed: %s\n",
                     nghttp2_strerror(rv));
        disconnect();
        return false;
    }

    if (!sendPreface()) { disconnect(); return false; }
    if (!sendSettings()) { disconnect(); return false; }

    m_connected = true;
    return true;
}

void Http2Client::disconnect() {
    if (m_session) { nghttp2_session_del(m_session); m_session = nullptr; }
    if (m_callbacks) { nghttp2_session_callbacks_del(m_callbacks); m_callbacks = nullptr; }
    m_transport->disconnect();
    m_recv_buf.clear();
    m_post_data.clear();
    m_stream_id = -1;
    m_connected = false;
}

bool Http2Client::isConnected() const {
    return m_connected && m_transport->isConnected();
}

// ═══════════════════════════════════════════════════════════════════
// HTTP/2 frame helpers
// ═══════════════════════════════════════════════════════════════════

bool Http2Client::sendPreface() {
    static const char PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    size_t len = std::strlen(PREFACE);
    int written = m_transport->send(
        reinterpret_cast<const uint8_t*>(PREFACE), len);
    return written == static_cast<int>(len);
}

bool Http2Client::sendSettings() {
    nghttp2_submit_settings(m_session, NGHTTP2_FLAG_NONE,
                            CHROME_SETTINGS, NUM_CHROME_SETTINGS);
    int rv = nghttp2_session_send(m_session);
    if (rv != 0) {
        std::fprintf(stderr, "[Http2Client] nghttp2_session_send (SETTINGS) failed: %s\n",
                     nghttp2_strerror(rv));
        return false;
    }
    return true;
}

bool Http2Client::sendPost(const uint8_t* data, size_t len) {
    m_recv_buf.clear();

    m_stream_id = nghttp2_submit_headers(m_session, NGHTTP2_FLAG_END_HEADERS,
                                         -1, nullptr,
                                         REQUEST_HEADERS, NUM_REQUEST_HEADERS,
                                         nullptr);
    if (m_stream_id < 0) {
        std::fprintf(stderr, "[Http2Client] nghttp2_submit_headers failed: %s\n",
                     nghttp2_strerror(m_stream_id));
        return false;
    }

    m_post_data.assign(data, data + len);
    nghttp2_data_provider data_provider{};
    data_provider.source.ptr = &m_post_data;
    data_provider.read_callback = onDataProviderRead;

    int rv = nghttp2_submit_data(m_session, NGHTTP2_DATA_FLAG_NONE,
                                 m_stream_id, &data_provider);
    if (rv != 0) {
        std::fprintf(stderr, "[Http2Client] nghttp2_submit_data failed: %s\n",
                     nghttp2_strerror(rv));
        return false;
    }

    rv = nghttp2_session_send(m_session);
    if (rv != 0) {
        std::fprintf(stderr, "[Http2Client] nghttp2_session_send (POST) failed: %s\n",
                     nghttp2_strerror(rv));
        return false;
    }

    return true;
}

int Http2Client::feedData(const uint8_t* data, size_t len) {
    return nghttp2_session_mem_recv(m_session, data, len);
}

// ═══════════════════════════════════════════════════════════════════
// I/O
// ═══════════════════════════════════════════════════════════════════

int Http2Client::send(const uint8_t* buf, size_t len) {
    if (!m_connected) return -1;
    return sendPost(buf, len) ? static_cast<int>(len) : -1;
}

int Http2Client::recv(uint8_t* buf, size_t len) {
    if (!m_connected) return -1;

    if (!m_recv_buf.empty()) {
        size_t to_copy = std::min(len, m_recv_buf.size());
        std::memcpy(buf, m_recv_buf.data(), to_copy);
        m_recv_buf.erase(m_recv_buf.begin(), m_recv_buf.begin() + to_copy);
        return static_cast<int>(to_copy);
    }

    uint8_t raw[4096];
    int nread = m_transport->recv(raw, sizeof(raw));
    if (nread <= 0) return nread;

    feedData(raw, static_cast<size_t>(nread));

    int rv = nghttp2_session_send(m_session);
    if (rv != 0) return -1;

    if (!m_recv_buf.empty()) {
        size_t to_copy = std::min(len, m_recv_buf.size());
        std::memcpy(buf, m_recv_buf.data(), to_copy);
        m_recv_buf.erase(m_recv_buf.begin(), m_recv_buf.begin() + to_copy);
        return static_cast<int>(to_copy);
    }

    return 0;
}

} // namespace inferno
