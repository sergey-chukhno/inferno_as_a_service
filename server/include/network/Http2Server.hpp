#ifndef INFERNO_HTTP2_SERVER_HPP
#define INFERNO_HTTP2_SERVER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <memory>

#include <nghttp2/nghttp2.h>

#include "../../../common/include/Transport.hpp"

struct ssl_ctx_st;
struct ssl_st;

namespace inferno {

class Http2Server {
public:
    using DataCallback = std::function<void(const uint8_t*, size_t)>;

    Http2Server();
    ~Http2Server();

    Http2Server(const Http2Server&) = delete;
    Http2Server& operator=(const Http2Server&) = delete;

    bool start(uint16_t port, DataCallback on_data);
    void stop();
    bool isRunning() const;

    int sendResponse(const uint8_t* data, size_t len);

private:
    int                      m_listen_fd;
    bool                     m_running;
    DataCallback             m_on_data;
    struct nghttp2_session*  m_session;
    struct nghttp2_session_callbacks* m_callbacks;
    struct ssl_ctx_st*       m_ctx;
    struct ssl_st*           m_ssl;
    int                      m_client_fd;
    std::vector<uint8_t>     m_send_buf;

    bool setupTls();
    bool acceptClient();
    int  handleData(const uint8_t* data, size_t len);

    static int onFrameRecv(nghttp2_session* session,
                           const nghttp2_frame* frame,
                           void* user_data);
    static int onDataChunkRecv(nghttp2_session* session,
                               uint8_t flags, int32_t stream_id,
                               const uint8_t* data, size_t len,
                               void* user_data);
    static ssize_t onSendCallback(nghttp2_session* session,
                                   const uint8_t* data, size_t len,
                                   int flags, void* user_data);
    static int onBeginHeaders(nghttp2_session* session,
                              const nghttp2_frame* frame,
                              void* user_data);
};

} // namespace inferno

#endif
