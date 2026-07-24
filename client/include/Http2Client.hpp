#ifndef INFERNO_HTTP2_CLIENT_HPP
#define INFERNO_HTTP2_CLIENT_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include <nghttp2/nghttp2.h>

#include "../../common/include/Transport.hpp"

namespace inferno {

class TlsTransport;

class Http2Client {
public:
    Http2Client();
    ~Http2Client();

    Http2Client(const Http2Client&) = delete;
    Http2Client& operator=(const Http2Client&) = delete;
    Http2Client(Http2Client&&) noexcept;
    Http2Client& operator=(Http2Client&&) noexcept;

    // Chrome 120+ HTTP/2 SETTINGS (exposed for testing)
    static const nghttp2_settings_entry CHROME_SETTINGS[6];
    static constexpr size_t NUM_CHROME_SETTINGS = 6;

    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const;

    int recv(uint8_t* buf, size_t len);
    int send(const uint8_t* buf, size_t len);

    ITransport& transport();

private:
    std::unique_ptr<TlsTransport> m_transport;
    struct nghttp2_session*       m_session;
    struct nghttp2_session_callbacks* m_callbacks;

    std::vector<uint8_t> m_recv_buf;
    std::vector<uint8_t> m_post_data;
    int32_t              m_stream_id;
    bool                 m_connected;

    bool sendPreface();
    bool sendSettings();
    bool sendPost(const uint8_t* data, size_t len);
    int  feedData(const uint8_t* data, size_t len);

    // Static nghttp2 callbacks
    static int onFrameRecv(nghttp2_session* session,
                           const nghttp2_frame* frame, void* user_data);
    static int onDataChunkRecv(nghttp2_session* session,
                               uint8_t flags, int32_t stream_id,
                               const uint8_t* data, size_t len, void* user_data);
    static ssize_t onSendCallback(nghttp2_session* session,
                                   const uint8_t* data, size_t len,
                                   int flags, void* user_data);
    static ssize_t onDataProviderRead(nghttp2_session* session,
                                      int32_t stream_id,
                                      uint8_t* buf, size_t len,
                                      uint32_t* data_flags,
                                      nghttp2_data_source* source,
                                      void* user_data);
};

} // namespace inferno

#endif
