# 📓 Inferno-as-a-Service: Development Log

This log tracks the ascension through the **9 Cercles de l'Enfer**, documenting core architectural decisions, security rationale, and verification milestones.

---

## 🏛️ Circle 2: Luxure (The Protocol) — [2026-04-21]
**Objective**: Establish a secure, binary communication baseline.

### Technical Milestones
- **Binary Header**: Implemented `PacketHeader` with Magic Value (`0xDEADBEEF`), Opcode, Payload Size, and Checksum.
- **Endianness Enforcement**: Introduced `htonl`/`ntohl` and `htons`/`ntohs` to ensure cross-platform compatibility (Little-Endian vs Big-Endian).
- **Socket Wrapper**: Encapsulated raw POSIX syscalls into a RAII-compliant `Socket` class.

### Security Rationale
- **OOM Protection**: Defined `MAX_PAYLOAD_SIZE` (10MB). Any packet claiming a larger size is rejected during deserialization to prevent malicious memory exhaustion.
- **Header Integrity**: Magic value validation ensures the server doesn't waste cycles on junk data.

---

## 🍗 Circle 3: Gourmandise (The Agent) — [2026-04-22]
**Objective**: Implement autonomous agent behavior and identity profiling.

### Technical Milestones
- **Agent Core Architecture**: Implemented an FSM-based `Agent` class (`INIT` -> `CONNECTING` -> `CONNECTED` -> `LISTENING` -> `DISPATCHING`).
- **Sliding Buffer Accumulator**: Refactored the Server to use `std::vector<uint8_t>` for each client, supporting fragmented TCP streams.
- **System Profiler**: Integrated POSIX APIs (`gethostname`, `getlogin_r`, `uname`) to extract target metadata.
- **Process Monitor Subsystem**: Implemented `ProcessProfiler` (macOS via `libproc`) to enumerate running processes.
- **Paged Discovery Protocol**: Designed a chunked transmission schema (`Opcode::PROC_LIST_RES`) to handle large data sets with low detection profile.
- **Stealth Caching**: Introduced a 30-second timed cache to minimize CPU usage spikes during discovery.

### Verification Milestone
- **Identity Handshake**: Verified that the Agent successfully identifies itself upon connection.
- **Intelligence Discovery**: Successfully tested paged process list transmission and table rendering via `process_list_test.sh`.
- **Remote Shell**: Verified server-initiated `whoami` command is executed by Agent and output streamed back via `remote_shell_test.sh`.

---

## 🔥 Circle 3 (Gourmandise) — Remote Shell Engine — [2026-05-02]
**Objective**: Enable the Server to execute arbitrary shell commands on the Agent.

### Technical Milestones
- **ShellExecutor Module**: Implemented a SOLID SRP-compliant `ShellExecutor` class using `popen()` to capture `stdout`+`stderr`.
- **Chunked Output Protocol**: Defined `CMD_EXEC` (0x0004) and `CMD_RES` (0x0005) binary schemas with a 3-byte header (status + length).
- **4096-byte Chunk Size**: Chosen to match the system page size and libc I/O buffer for stealth traffic profiling.
- **Sliding Buffer Fix**: Applied the same sliding buffer accumulation loop to the Agent's `handleListening()` that the Server uses, eliminating the single-packet discard bug.
- **4 New Unit Tests**: `test_shell_executor_echo`, `_failure`, `_stderr_redirect`, `_chunk_size`.

### Security Architecture Decisions
- **Stealth Chunking**: 4096-byte output chunks are indistinguishable from SSH/HTTPS traffic at the packet level.
- **TODO Circle 7**: Evolve `popen()` to non-blocking pipe + `select()` for long-running commands.
- **TODO Circle 7**: Add inter-chunk jitter for timing fingerprint evasion.
- **TODO Circle 8**: Encrypt payloads with AES-256-GCM to defeat DPI content inspection.

---

## 🕵️ Circle 3 (Gourmandise) — Stealth Keylogger Engine — [2026-05-04]
**Objective**: Implement an OS-abstracted, non-blocking keystroke capture engine.

### Technical Milestones
- **KeyLogger Engine**: Implemented `KeyLogger.hpp/cpp` using the "Buffer + Dump" architecture.
- **macOS Native Hook**: Successfully utilized `CGEventTapCreate` to intercept CoreGraphics events.
- **Thread Encapsulation**: Deployed a localized `std::thread` to host the `CFRunLoop` for the tap, preserving the main Agent FSM's non-blocking constraint.
- **Protocol Extensibility**: Added `KEYLOG_START` (0x0100), `KEYLOG_STOP` (0x0101), `KEYLOG_DUMP` (0x0102), and `KEYLOG_DATA` (0x0108).
- **Automated Trigger**: Bound an automated dump request to the end of `PROC_LIST_RES` via `INFERNO_KEYLOG_TRIGGER` to enable robust E2E testing without a GUI.
- **CI Resilience**: Built an `#ifdef INFERNO_TESTING` bypass to gracefully simulate tap hook creation when macOS Accessibility permissions are absent (e.g., in GitHub Actions).

### Security Architecture Decisions
- **Buffer Starvation Cap**: Set `MAX_BUFFER_SIZE` to 1MB to prevent host memory exhaustion if the operator goes offline.
- **Detached Event Handling**: Physical keystroke interception is detached from the networking plane, preventing packet-burst timing signatures.

---

## 💰 Circle 4: Avarice (The GUI) — [2026-05-05]
**Objective**: Transition the Server to a graphical Command & Control (C2) dashboard using Qt 6.

### Technical Milestones
- **Qt6 Infrastructure**: Integrated Qt6 Widgets/Core/Gui into the CMake build system with `AUTOMOC` support.
- **Asynchronous Signal Migration**: Refactored `inferno::Server` to inherit from `QObject`, replacing blocking/CLI logging with a thread-safe Signal/Slot architecture.
- **Worker Thread Model**: Designed the server to run in a dedicated `QThread`, ensuring UI responsiveness during heavy telemetry throughput.
- **High-Fidelity Dashboard**: Implemented a 3-pane layout (Agents, Telemetry, Keylogs) with a dark "Neon-on-Charcoal" design system.
- **Surveillance Pulse**: Implemented real-time keystroke polling (1.5s interval) with smart silence suppression.
- **Intelligence Auditing**: Developed a robust instant-filtering engine for telemetry and keystrokes using atomic regex splitting and historical session buffers.
- **Operator UX**: Unified utility iconography (Copy, Paste, Search, History) and implemented tactical timestamping `[HH:mm:ss]`.
- **Animation Engine**: Developed a `QTimer`-driven radar rotation and state-based surveillance toggle logic.
- **Protocol Stability**: Fixed binary payload truncation issues and hardened the agent-server command dispatch loop.
- **CI/CD Maturity**: Integrated Qt6 SDK into GitHub Actions with cross-platform build caching for macOS and Linux.

---

### Security Architecture Decisions
- **UI/Net Isolation**: By decoupling the UI from the networking thread, we prevent GUI hangs (e.g., during window dragging) from causing socket timeouts or TCP buffer overflows.
- **Silent Intelligence**: Data filters ensure only meaningful intelligence is displayed, reducing operator cognitive load and preventing dashboard noise.

## 🏛️ Circle 5: Wrath (The Persistence) — [2026-05-07]
**Objective**: Transition to a production-grade, forensic-compliant persistence and intelligence platform.

### Technical Milestones
- **PostgreSQL 16 Integration**: Migrated from volatile in-memory history to a persistent PostgreSQL 16 backend with automated schema migrations.
- **Hardware Fingerprinting**: Implemented a Zero-Footprint UUID generator utilizing IOKit (macOS) and machine-id (Linux) to ensure immutable victim identity across disconnections.
- **Intelligence Profiling**: Developed the "Agent Card" forensic viewer, tracking session timelines, OS metadata, and persistent liveness status (🟢/🔴).
- **Loot Engine (Binary Persistence)**: Implemented the `loot` table (BYTEA) for high-volume binary exfiltration (Files/Screenshots) with 100% integrity verification.
- **Asynchronous Telemetry History**: Built a type-aware history retrieval engine supporting Shell Output vs. Process Snapshot filtering.
- **Environment-Aware TDD**: Hardened the CI pipeline to use PostgreSQL 16 on Linux with an intelligent SQLite fallback on macOS, ensuring 100% test coverage across hostile environments.

### Security & OPSEC Hardening
- **Secret Decoupling**: Removed all hardcoded database credentials from the source code. Implemented a native `.env` loader utility to manage tactical secrets at runtime.
- **Network Isolation**: Restricted the database service to `127.0.0.1`, preventing unauthorized remote scanning of the intelligence store.
- **Persistence Auditing**: Developed a persistent audit trail for all agent interactions, ensuring forensics are preserved even if the victim goes offline.
- **Infrastructure Protection**: Protected tactical configurations via `.env` / `.gitignore` and provided a `.env.example` blueprint for secure deployments.

## 🕵️ Circle 6: Hérésie (Intelligence Analysis Engine) — [2026-05-25]

**Objective**: Parse incoming keystroke sequences and telemetry in real-time or historically to extract classified data.

### Technical Milestones

- **Analysis Utility Module**: Created a static utility class `Analysis` exposing regex classification pipelines.
  - **Emails**: Captures RFC-compliant formats.
  - **Phone Numbers**: Grouping filter that captures diverse international telephone formats (7 to 15 digits) while strictly filtering out false positives such as IP addresses, dates, and timestamp prefixes.
  - **Credit Cards**: Identifies numeric sequences and validates them using a C++ implementation of the **Luhn Algorithm (modulo 10)**.
  - **Passwords**: Scans keyword indicators (`password:`, `pwd=`, etc.) and keystroke heuristics looking for `[TAB]credential[ENTER]` keylogger patterns.
- **Backspace Reconstruction Engine**: Implemented `Analysis::filterBackspaces` to resolve typing corrections (e.g. `y[BACKSPACE]uper` -> `super`), enabling reconstruction of coherent logs from raw buffers.
- **Chronological Session Reconstruction**: Implemented `Inferno_Database::getRawKeylogsChronological` to aggregate agent keystroke chunks sequentially, preserving chronological continuity over packet transmission windows.
- **Sub-string Merging & Deduplication**: Hardened `Inferno_Database::logIntelligence` to merge growing real-time entries (e.g., updating database record `+33744181920` to `+337441819201` as the user continues typing) and automatically discard redundant intermediate substrings.

### Security Rationale

- **Target Profiling**: Focused regex extractions dramatically reduce the size of the exfiltrated database, reducing storage footprint and network traffic overhead.
- **Deduplication Engine**: De-clutters the operator dashboard and prevents database write amplification during continuous keystroke logging.

---

## 🏗️ Clean Architecture & SOLID Refactoring — [2026-05-26]

**Objective**: Address technical debt, enforce SRP/DRY/KISS, and modularize the server codebase into clean layer abstractions.

### Technical Milestones

- **Modular Directory Reorganization**: Reorganized the flat `server/` workspace into distinct clean architectural layers:
  - `server/include/network/` & `server/src/network/` (Network communication layers)
  - `server/include/database/` & `server/src/database/` (Data persistence layers)
  - `server/include/services/` & `server/src/services/` (Business logic and services)
  - `server/include/ui/` & `server/src/ui/` (Presentation/GUI layer and custom panels)
- **Single Responsibility Principle (SRP) Enforcement**:
  - Slimmed down the monolithic `MainWindow` (from ~900 to ~420 lines) to act strictly as a layout coordinator.
  - Extracted UI widgets and slots into three custom panel components: `TelemetryPanel`, `KeylogPanel`, and `IntelligencePanel`.
  - Moved QSS style constants into a centralized `StyleSheets.hpp` module.
- **Decoupled Business Services**:
  - Implemented the singleton `IntelAnalysisService` to manage in-memory raw keystroke buffers (`m_agentRawKeylogs`) and run the regex classification pipelines.
  - Bound UI updates to the service's `intelligenceUpdated` signal, separating visual updates from the database stream.

### Verification Milestone

- **Full Compile**: Reconfigured CMake target sources to compile successfully under the new directory layout.
- **TDD Success**: Verified that all **22 unit tests** in the test suite (`inferno_tests`) pass successfully, proving zero regression.

---

## 🔥 Circle 7: Violence (Cross-Platform Agent Support) — [2026-06-06]
**Objective**: Introduce complete cross-platform support (Windows, macOS, Linux) to the Client Agent and its underlying layers, ensuring warning-free compilation and successful execution of the test suite across all target platforms.

### Technical Milestones
- **Unified Socket Layer**: Abstracted socket descriptors using `socket_t` (mapping to Win32 `SOCKET` or POSIX `int`). Handled socket lifecycle discrepancies (using `closesocket` vs `close`, and mapping `SHUT_RDWR` to `SD_BOTH` on Windows).
- **Winsock Safety & Lifecycle**: Implemented a thread-safe, local static RAII `WinsockManager` inside [Socket.cpp](file:///Users/sergeychukhno/Desktop/C:C++/inferno_as_a_service/common/src/Socket.cpp) to validate `WSAStartup` execution and automatically call `WSACleanup` on application termination.
- **Port Reuse Compatibility**: Disabled `SO_REUSEADDR` conditionally on Windows platforms to prevent duplicate port binding success, ensuring test assertion consistency for port exhaustion scenarios.
- **Client Profiling Portability**: Modified [Agent.cpp](file:///Users/sergeychukhno/Desktop/C:C++/inferno_as_a_service/client/src/Agent.cpp) to retrieve Windows metrics: MachineGuid via registry queries, computer name via `GetComputerNameA`, username via `GetUserNameA`, and OS version via Windows NT CurrentVersion registry paths. Removed the redundant `KEY_WOW64_64KEY` flag to guarantee 32-bit and 64-bit Windows registry compatibility.
- **Process Profiling Portability**: Modified [ProcessProfiler.cpp](file:///Users/sergeychukhno/Desktop/C:C++/inferno_as_a_service/client/src/ProcessProfiler.cpp) to capture process entries on Windows using the lightweight `CreateToolhelp32Snapshot` snapshot API, converting UTF-16 wide-character names (`pe32.szExeFile`) to UTF-8 strings. Incorporated bounds checks for `WideCharToMultiByte` to prevent undefined behavior on empty/invalid process names.
- **Keylogger Engine Portability**: Ported the event-tap keylogger in [KeyLogger.cpp](file:///Users/sergeychukhno/Desktop/C:C++/inferno_as_a_service/client/src/KeyLogger.cpp) to Windows using a low-level thread-bound keyboard hook (`WH_KEYBOARD_LL`) coupled with a Win32 message loop, translating virtual keycodes using `ToUnicodeEx`.
- **Database Fallback Resilience**: Hardened [Inferno_Database.cpp](file:///Users/sergeychukhno/Desktop/C:C++/inferno_as_a_service/server/src/database/Inferno_Database.cpp) to attempt PostgreSQL (`QPSQL`) first, but gracefully fall back to an in-memory SQLite (`QSQLITE`) database if connection fails, preventing database initialization failures in environments without a running database daemon (like the Windows CI runner).
- **CI/CD Build & Security Hardening**:
  - Integrated a Windows build job using MSVC in [.github/workflows/inferno_ci.yml](file:///Users/sergeychukhno/Desktop/C:C++/inferno_as_a_service/.github/workflows/inferno_ci.yml).
  - Pinned `actions/checkout` and `jurplel/install-qt-action` to immutable commit SHAs (`692973e3d937129bcbf40652eb9f2f61becf3332` and `b3ea5275e37b734d027040e2c7fe7a10ea2ef946` respectively) to eliminate supply chain vulnerabilities.
  - Disabled GitHub credentials persistence in checkout actions (`persist-credentials: false`) to safeguard repository tokens.
- **TDD Expansion**: Added [tests/process_profiler_test.cpp](file:///Users/sergeychukhno/Desktop/C:C++/inferno_as_a_service/tests/process_profiler_test.cpp) to verify multi-platform process snapshot retrieval. All **24 unit tests** now compile and pass cleanly on macOS, Linux, and Windows.

### Security & OPSEC Hardening
- **Process Snapshot Minimization**: Retained stealth caching (30s) across all platforms to reduce CPU activity footprint.
- **Memory Protection**: Secured UTF-16 to UTF-8 conversion buffer allocations from zero-length inputs, ensuring memory safety during process execution checks.

*Status: 100% COMPLETE.*

---

## 🕶️ Circle 8: Ruse et Tromperie — Phase 0: Linux Keylogger — [2026-06-08]

**Objective**: Close the cross-platform gap left by Circle 7 by replacing the Linux keylogger stub with a full evdev-based backend, enabling keystroke capture on Linux desktops.

### Technical Milestones
- **evdev Device Discovery**: Implemented `findKeyboardDevice()` with a dual-strategy scanner:
  - *Primary*: Walks `/dev/input/by-path/` matching `-kbd` / `-keyboard` symlinks (udev-managed naming).
  - *Fallback*: Iterates `/dev/input/event*` using `ioctl(EVIOCGBIT)` to probe for `EV_KEY` capability and verify keyboard heuristics (ENTER + letter keys present).
- **Event Loop**: Implemented `evdevLoop()` — a dedicated `std::thread` running `poll()` with a 100ms timeout on the keyboard device fd. Reads `struct input_event` and filters for `EV_KEY` press events (`value == 1`).
- **US Layout Keycode Translation**: Built a comprehensive keycode-to-string mapper with modifier state tracking (shift, caps lock, ctrl, alt):
  - Letters A–Z with uppercase via shift/caps XOR.
  - Number row with shifted symbols (`!`, `@`, `#`, etc.).
  - Punctuation, navigation (arrows, HOME, END, PAGEUP/DOWN), function keys F1–F12, keypad digits and operators.
  - Special tags: `[ENTER]`, `[BACKSPACE]`, `[TAB]`, `[ESC]`, `[DEL]`, `[INSERT]`, `[MENU]`, `[PRTSC]`, `[PAUSE]`.
  - Unknown keys fall back to `[KEY:code]` notation.
- **Graceful Permission Handling**: On Linux desktop sessions, `systemd-logind` grants ACL access to `/dev/input/event*` automatically — no root required. Non-desktop environments (SSH, headless) fail with a logged warning and disable keylogger, identical to macOS Accessibility rejection behavior.
- **Thread Safety**: The evdev thread checks `m_running` every 100ms via `poll()` timeout, enabling clean shutdown without a signaling pipe. The fd is closed only after the thread joins, preventing race conditions.
- **No New Dependencies**: evdev uses only Linux kernel headers (`<linux/input.h>`, `<linux/input-event-codes.h>`) and POSIX APIs (`poll`, `ioctl`, `open`, `read`). CMakeLists.txt is unchanged.

### Verification Milestone
- **Full Build**: Project compiles warning-free on macOS (existing backends) with zero regression.
- **TDD Success**: All **24 unit tests** pass. New Linux-specific test `test_keylogger_linux_backend_compiles` verifies the evdev backend links and runs correctly under `INFERNO_TESTING` mode.
- **Tagged**: `v0.8.0-ruse`.

### Architecture Decisions
- **evdev over X11 XRecord**: evdev works on both X11 and Wayland, requires no additional libraries, and operates at the kernel level (lower detection profile). The tradeoff is that non-desktop environments without `systemd-logind` ACLs require root or `input` group membership.
- **US Layout Hardcoded**: The initial implementation uses a fixed US keyboard layout. Layout-aware translation via `libxkbcommon` is deferred to a later iteration (planned for Phase III enhancements).
- **WSL Limitations**: WSL2 does not pass through host keyboard devices to the VM. The device scanner returns empty gracefully (no crash). Windows keystroke capture should use the native Windows agent binary instead.

*Status: 100% COMPLETE. Next: Circle 8 Phase I — Transport Security (AES-256-GCM, Jitter, PONG Piggybacking)...**
