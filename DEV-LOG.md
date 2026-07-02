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

*Status: 100% COMPLETE.*

---

## 🕶️ Circle 8: Ruse et Tromperie — Phase I: Transport Security & Traffic Stealth — [2026-06-08]

**Objective**: Hide agent traffic from network inspection and timing analysis through payload encryption, transmission jitter, and heartbeat piggybacking.

### Technical Milestones

**1.1 AES-256-GCM Payload Encryption**
- Replaced CRC32 checksum with AEAD (Authenticated Encryption with Associated Data) using AES-256-GCM via OpenSSL EVP.
- New `CryptoContext` singleton managing a 256-bit AES key, 12-byte random IV per packet, and 16-byte GCM authentication tag.
- Wire format: `[Header(10)] [IV(12)] [Ciphertext(N)] [Tag(16)]` — every payload is encrypted before leaving the agent and decrypted at the server.
- Packet header shrunk from 14 to 10 bytes (removed `checksum` field — integrity now guaranteed by GCM).
- Static compiled-in key via `initDefault()` — a placeholder for the DH key exchange planned in Phase I-bis.
- Graceful fallback: if `CryptoContext` is not initialized, packets are sent in cleartext with a single warning.
- CMake integration: `find_package(OpenSSL REQUIRED)`, linked to `inferno_common`, `inferno_server`, `inferno_client`, `inferno_tests`.

**1.2 Keylogger Jitter Thread**
- Decoupled `KEYLOG_DUMP` handling from network transmission with a dedicated jitter thread.
- When `KEYLOG_DUMP` arrives, the handler sets a flag and signals a condition variable.
- The jitter thread wakes, sleeps a random 500–3000ms, reads the keylogger buffer, and stores the data in a shared pending buffer.
- The PONG handler (1.4) consumes the shared buffer on the next heartbeat cycle — no `KEYLOG_DATA` packets are sent directly.
- Prevents timing correlation between operator button presses and network traffic bursts, without blocking the agent's main FSM loop.

**1.3 Shell Output Inter-Chunk Jitter**
- Added random 50–250ms delay between `CMD_RES` chunks during shell output streaming.
- Uses `thread_local std::mt19937` seeded once per thread for cryptographically neutral randomness.
- No jitter after the final chunk to avoid delaying command completion on the C2 dashboard.
- 4096-byte chunk size + randomized inter-chunk delay makes shell output traffic statistically indistinguishable from HTTPS bulks.

**1.4 Server Heartbeat & PONG Piggybacking**
- Added a 5-second PING heartbeat to the server's `select()` event loop, enabling detection of dead connections and providing a natural transmission carrier.
- Agent piggybacks keylog data on PONG responses exclusively from the shared buffer staged by the jitter thread (1.2) — no direct `m_keylogger.dump()` fallback, preventing buffer fragmentation across heartbeats.
- Keystroke data is embedded inside heartbeat response packets — no `KEYLOG_DATA` packet ever appears on the wire.
- Server extracts piggybacked keylog data from PONG payloads and routes it through the existing `keylogReceived` signal path.
- From a network forensics perspective, the agent only ever sends PONG responses and encrypted command results — no dedicated telemetry packet types visible in a packet capture.

### Verification Milestone

- **Full Build**: Project compiles warning-free on macOS with OpenSSL 3.6 (also tested with OpenSSL 3.5).
- **TDD Success**: All **26 unit tests** pass, including `test_empty_payload_encrypt_decrypt`.
- **Tagged**: `v0.8.0-ruse`.

### Debugging Note — Three Bugs Found & Fixed During Integration

1. **Empty-payload GCM ambiguity**: `CryptoContext::decrypt()` returned an empty vector for both "decryption succeeded (plaintext is empty)" and "decryption failed (tag mismatch)". `Packet::deserialize()` treated any empty result as failure, rejecting valid packets like `SYS_REQ_INFO`, `PING`, and `KEYLOG_START` which have empty payloads. Fixed by changing `decrypt()` return type to `std::optional<std::vector<uint8_t>>`.

2. **Wire vs decrypted payload size**: Both `Agent::handleListening()` and `Server::processPacketBuffer()` used `getPayload().size()` (decrypted size) to calculate how many bytes to remove from the receive buffer after deserialization. With encryption, the wire payload is larger (+28 bytes GCM overhead), so bytes were left in the buffer, corrupting subsequent reads. Fixed by adding `getWirePayloadSize()` to `Packet`, set during `deserialize()`.

3. **OpenSSL EVP_EncryptUpdate with nullptr**: Passing `plaintext.data()` for an empty `std::vector` (which returns `nullptr`) to `EVP_EncryptUpdate` may not properly initialize the GCM internal state, even with length 0. Fixed by always passing a non-null pointer (the output buffer) for zero-length operations.

4. **PONG handler draining keylogger buffer**: The PONG handler's fallback `m_keylogger.dump()` independently consumed the keylogger buffer every heartbeat, stealing data from the jitter thread and fragmenting keystrokes across 5-second windows. Fixed by removing the direct `dump()` call — PONG only sends data explicitly staged by the jitter thread.

5. **Jitter thread overwriting pending buffer**: The server polls KEYLOG_DUMP every 1.5s, but PONG only delivers every 5s. Each jitter cycle was overwriting `m_keylog_pending_data`, so only the last fragment within a heartbeat window survived. Fixed by appending instead of replacing, with a 1MB cap.

### Security Architecture Decisions

- **Static Key**: The compiled-in 256-bit key defeats passive DPI/in-line network forensics but not binary reverse engineering. DH key exchange (Phase I-bis) will upgrade to per-session ephemeral keys with perfect forward secrecy.
- **Fallback to Cleartext (development builds)**: If `CryptoContext::init()` is never called, packets are sent/received in cleartext. This allows local testing and debugging without initialising the crypto layer, but **production builds always require OpenSSL** and will emit a warning on first unencrypted send.
- **Combined Jitter**: Keylog jitter (500–3000ms) + shell jitter (50–250ms) + PONG heartbeat carrier (5s) create a multi-layered traffic obfuscation that defeats both timing-correlation and packet-category analysis.

*Status: 100% COMPLETE.*

---

## 🕶️ Circle 8: Ruse et Tromperie — Phase II: Agent Evasion & Discretion — [2026-06-08]

**Objective**: Transform the agent from a visible command-line binary into a hidden, persistent background implant.

### Technical Milestones

**2.1 Hide Console Window**
- **Windows**: `FreeConsole()` on startup to detach from parent console. The standard `main()` entry point is preserved for cross-platform compatibility — console hiding is done at runtime, not at link time.
- **macOS/Linux**: Double-fork daemonization (`fork` → `setsid` → `fork`) with stdin/stdout/stderr redirected to `/dev/null`. Guarded by `INFERNO_TESTING` so debug builds keep the console visible.

**2.2 Exponential Reconnect Backoff**
- Replaced fixed 5-second retry with exponential backoff starting at 1s, doubling each failure up to a 300s (5 min) cap.
- ±30% jitter applied to each interval to prevent predictable reconnection timing.
- Backoff resets to 1s on successful connection.

**2.3 Wrapper/Dropper Binary**
- New `inferno_wrapper` target that embeds the full `inferno_client` binary as a C byte array (auto-generated by `bin2header.py` at build time).
- On execution: extracts the agent to a hidden platform-specific path, sets executable permissions, spawns the agent process, and exits immediately.
- `Python3` required at build time; the wrapper itself has zero external runtime dependencies.

**2.4 Discreet Installation Paths**
- **Windows**: `%APPDATA%\Microsoft\Crypto\RSA\S-1-5-21-...-svchost.exe` (inside a real-looking Crypto key directory).
- **macOS**: `~/Library/Application Support/.Spotlight/V100/SpotlightIndex` (mimics a Spotlight indexer cache file).
- **Linux**: `~/.cache/apt/archives/.apt-get` (mimics an apt package cache).

**2.5 Auto-Start Persistence**
- **Windows**: Registry `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` key.
- **macOS**: LaunchAgent plist at `~/Library/LaunchAgents/com.apple.softwareupdate.helper.plist` with `RunAtLoad=true`. Loaded via `launchctl bootstrap` (modern API) with legacy `launchctl load` fallback.
- **Linux**: Autostart `.desktop` file at `~/.config/autostart/inferno-agent.desktop`.

### Verification Milestone

- **Full Build**: All targets (`inferno_client`, `inferno_wrapper`) compile warning-free on macOS.
- **TDD Success**: All **26 unit tests** pass.
- **End-to-End**: Wrapper extracts agent to hidden path, agent spawns, connects to server, and appears in GUI with correct system info.

### Architecture Decisions

- **Wrapper embeds the full binary** rather than downloading from a remote URL — eliminates network signature during installation. The tradeoff is a larger wrapper binary (~330KB).
- **Persistence is best-effort**: If `launchctl bootstrap` fails (e.g., macOS sandboxing), the plist still exists in `LaunchAgents/` and will be loaded on the next user login automatically.
- **Backoff jitter uses thread_local RNG** — no locking overhead and the agent is single-threaded for all FSM operations.

*Status: 100% COMPLETE.*

---

## 🕶️ Circle 8: Ruse et Tromperie — Phase II-bis: Persistence & Wrapper Hardening — [2026-06-10]

**Objective**: Resolve production bugs in macOS launchd persistence and Windows wrapper directory creation identified during cross-platform testing.

### Technical Milestones

**macOS — LaunchAgent Fixes**
- **Skip daemonization when launched by launchd**: Added `getppid() == 1` check in `daemonize()` — launchd already backgrounds the process and needs to track its PID for `KeepAlive` crash-restart to work.
- **Plist permissions**: Set `umask(022)` before writing the plist file — launchd rejects world-writable plists with EIO.
- **Server args in plist**: Plist `ProgramArguments` now includes server IP and port, so launchd re-launches the agent with correct connection parameters.
- **Unique plist label**: Changed from `com.apple.softwareupdate.helper` to `com.inferno.agent` — avoids collision with Apple's own softwareupdate plist.
- **`KeepAlive true`**: Agent auto-restarts on crash without user intervention.
- **Removed `launchctl bootstrap` call**: macOS automatically scans `~/Library/LaunchAgents/` at login and loads all plists. No explicit load command needed — the plist activates on next login silently.

**Windows — Wrapper Directory Creation Fix**
- **Error checking**: `CreateDirectoryA` return values are now validated via `GetLastError()`. `ERROR_ALREADY_EXISTS` is tolerated; all other failures abort with `false`.
- **Fallback path**: If the primary `%APPDATA%\Microsoft\Crypto\RSA\S-1-5-21-\...` directory cannot be created (blocked by Defender, Controlled Folder Access, or permissions), the wrapper falls back to `%TEMP%\<8-hex-chars>\` and updates the path so extraction and execution use the working location.
- **MSVC build fix**: Guarded `::getppid()` with `#ifndef _WIN32` — the POSIX function is not available on MSVC.

### Roadmap Restructure
- Replaced the architecture-first roadmap (`ROADMAP_FORWARD.md`) with an **evasion-first priority ordering**.
- Security concerns (wrapper hardening, obfuscation, injection) now take precedence over performance and code aesthetics.
- Circle 9 (Trahison / Propagation) integrated as Phase 2 (basic SSH/SMB/dropper) and Phase 7 (evasive WMI/DCOM lateral movement post-injection).
- Old architectural phases (AI analysis, Kafka/ClickHouse decoupling, DI framework) pushed to lowest priority (Phase 10).

### Bug Fixes Log

| # | Symptom | Root Cause | Fix |
|---|---|---|---|
| 1 | macOS: agent daemonizes on launchd start, launchd loses PID tracking | `daemonize()` ran unconditionally | Skip daemonization when `getppid() == 1` |
| 2 | macOS: launchd rejects plist with EIO | World-writable plist file permissions | `umask(022)` before `fopen` |
| 3 | macOS: launchd launches agent without server args | Plist `ProgramArguments` had only the binary path | Added IP and port as additional array items |
| 4 | macOS: plist label conflicts with Apple's softwareupdate | Generic label `com.apple.softwareupdate.helper` | Renamed to `com.inferno.agent` |
| 5 | Windows: Crypto\RSA directory not created | `CreateDirectoryA` return values ignored; path blocked by Defender | Added `GetLastError()` checking + `%TEMP%` fallback |
| 6 | Windows: MSVC build error `C2039: 'getppid' is not a member of global namespace` | `getppid()` is POSIX-only | Guarded with `#ifndef _WIN32` |

### Verification Milestone
- **Full Build**: All targets compile warning-free on macOS (Apple Clang) and Windows (MSVC via GitHub Actions).
- **TDD Success**: All **26 unit tests** pass across all platforms.
- **End-to-End**: macOS launchd persistence verified working across reboot — agent auto-starts, connects with correct IP/port, and reconnects on crash via `KeepAlive`.

---

## 🦠 Circle 9: Trahison — Phase 2: Basic Propagation (Dropper + Lateral Movement) — [2026-06-14]

**Objective**: Enable the agent to propagate to adjacent machines via SSH/SMB lateral movement, and deliver a social-engineering dropper disguised as a PDF invoice.

### Technical Milestones

**2A — Dropper / Social Engineering (wrapper)**
- **Decoy PDF Extraction**: Embedded decoy PDF (`decoy.pdf`) is extracted to the user-visible `Downloads/` folder and opened via `xdg-open`/`open`, presenting a believable cover document.
- **Windows Dropper**: PDF icon applied via `.rc` resource (`pdf.ico`); output binary named `invoice.pdf.exe` to appear as a harmless PDF.
- **macOS Dropper**: `Invoice.pdf.app` bundle with `Info.plist` at `Contents/Info.plist`, bundle name set to `Invoice.pdf` so Finder displays it as a PDF file.
- **Execution Flow**: Extract decoy → open → spawn agent → self-delete the dropper binary.

**2B — Lateral Movement (SSH/SMB)**
- **Propagator Module**: New `Propagator` class with three commands:
  - `SCAN` — ARP scan on Docker subnet (`172.17.0.0/16`) via `arp-scan`/`nmap` + port scan for SSH (22) and SMB (445).
  - `BRUTE` — Credential brute-force against discovered targets using SSH (`sshpass`) and SMB (`smbclient`) with common credential pairs.
  - `DEPLOY` — On successful brute, uploads agent binary via SCP and executes it remotely via SSH with `nohup`.
- **New Opcodes**: `PROPAGATE` (0x0106) and `PROPAGATE_RES` (0x0107).

**2C — Server C2 Dashboard**
- **PropagationPanel Widget**: New `PropagationPanel` with target IP/subnet input, three action buttons (SCAN/BRUTE/DEPLOY), and a scrollable log output with timestamped results.
- **Server Propagation Handler**: `sendPropagationCommand()` serializes command byte + target into `PROPAGATE` packets; `PROPAGATE_RES` handler decodes success flag and output text.
- **Network Propagation Tab**: Added as a new tab in the MainWindow dashboard, routed through agent-selection context.

**2D — Debugging & Resilience**
- **Agent Reconnect Fix**: `Agent::handleListening()` now closes the socket on connection loss before reconnecting, fixing a bug where a stale fd blocked `Socket::connectTo()`.
- **Wrapper Hardening**:
  - Windows install path moved to `%LOCALAPPDATA%\Microsoft\Edge\Application\<8-hex>\msedge.exe`.
  - macOS: `com.apple.quarantine` extended attribute removed via `xattr -d` before `execv` to bypass Gatekeeper.
  - Both: 5–15s random execution jitter between extract and spawn to evade behavioral detection.
  - Both: `GetLastError()`/`strerror(errno)` logged on every failure path.
  - macOS bundle: Info.plist placed at `Contents/Info.plist` (not `Contents/Resources/`), binary chmod'd `755`.

### Verification Milestone
- **Full Build**: All targets (`inferno_client`, `inferno_wrapper`, `inferno_server`) compile warning-free on macOS (Apple Clang).
- **TDD Success**: All **26 unit tests** pass.
- **End-to-End**: Propagation panel renders in C2 dashboard; SCAN/BRUTE/DEPLOY commands are serialized, sent, and results displayed.

### Security Architecture Decisions
- **Docker Subnet Default**: The scanner targets only `172.17.0.0/16` by default to avoid accidental lateral movement outside the lab environment. Production deployment should override with the target subnet.
- **SSH/SMB Over netcat fallback**: The port scanner uses bash `/dev/tcp` which requires a shell with `noclobber` unset — no external dependencies beyond `sshpass` and `smbclient`.
- **Dropper Self-Deletion**: The wrapper deletes itself after extraction, reducing forensic surface on the initial infection vector.
- **OPSEC Jitter**: 5–15s random delay between extraction and agent launch defeats simple time-window correlation rules in EDR solutions.

### Bug Fixes Log

| # | Symptom | Root Cause | Fix |
|---|---|---|---|
| 1 | macOS: .app bundle not recognized by Finder | `Info.plist` at `Contents/Resources/` instead of `Contents/` | Moved to `Contents/Info.plist` |
| 2 | macOS: binary inside .app has no execute permission | `cp` does not preserve permissions | Added `chmod 755` after copy |
| 3 | Agent fails to reconnect after server restart | Stale socket fd blocks `Socket::connectTo()` | Close socket on connection loss in `handleListening()` |
| 4 | Windows wrapper: MSVC build error | `getppid()` is POSIX-only | Guarded with `#ifndef _WIN32` |
| 5 | Windows wrapper: directory creation silently fails | `CreateDirectoryA` return values not checked | Added `GetLastError()` validation + `%TEMP%` fallback |

---

## 🕶️ Circle 8: Ruse et Tromperie — Phase 3: Embedding Obfuscation — [2026-06-18]

**Objective**: Prevent static detection of the wrapper binary through build-time encryption of the embedded agent and runtime deobfuscation of all sensitive strings.

### Technical Milestones

**3.1 XOR Agent Binary Encryption at Build Time**
- Extended `bin2header.py` with `--key <hex>` argument: the agent binary is XOR-encrypted *before* embedding into `agent_binary.h`, defeating static AV/YARA signature matching against known agent bytes (`MZ` PE header, `DEADBEEF` magic, agent strings).
- Added `decryptInPlace()` stub in `main.cpp` — decrypts the mutable buffer before `fwrite()` at runtime.
- Key (`2B7E151628AED2A6`) is passed from `CMakeLists.txt` as a hex constant and embedded as `XOR_KEY[]`/`XOR_KEY_LEN` in the generated header.
- After encryption: `strings` on the wrapper binary finds **zero** occurrences of `MZ`, `DEADBEEF`, `KEYLOG`, or `Inferno Agent`.

**3.2 Compile-Time String Obfuscation**
- New `wrapper/include/unlit.hpp` header implementing `Unlit<N>` — a `constexpr` struct that XORs string literals at compile time using a 16-byte key, with deobfuscation on first `get()` call at runtime.
- `UNLIT("string")` macro wraps ~25 high-signal strings across the wrapper:
  - Install paths (`Edge\\Application\\`, `.SpotlightIndex`, `apt/archives/.apt-get`)
  - macOS quarantine bypass command (`xattr -dr com.apple.quarantine`)
  - Windows batch script artifacts (`del_inferno.bat`, `@echo off`, `ping ... > nul`)
  - All `[Wrapper]` error format strings
  - Shell binary paths (`/bin/sh`, `xdg-open`, `open`)
  - Default C2 IP (`127.0.0.1`)
- Zero runtime overhead: ciphertext is baked into the binary's data section by the `constexpr` constructor, deobfuscated lazily on first access.
- Verified: `strings` on the packed wrapper finds **zero** plaintext matches for any of the above patterns.

### Verification Milestone
- **Full Build**: All targets compile warning-free on macOS (Apple Clang).
- **TDD Success**: All **26 unit tests** pass.
- **OPSEC Verification**: `strings` analysis on the wrapper binary returns zero sensitive plaintext strings and zero agent PE header bytes.

### Security Architecture Decisions
- **XOR over AES**: The embedded agent uses XOR (not AES) for encryption — sufficient to defeat static byte-signatures, with zero code footprint and no external dependency. AES-256-GCM on the *wire* (Phase I) already protects transport.
- **Separate XOR keys**: Agent binary encryption uses an 8-byte key (`2B7E151628AED2A6`); string obfuscation uses a separate 16-byte key (`4F8CD13A...`). Compromise of one does not reveal the other.
- **String obfuscation at compile time**: Ciphertext is computed by the `constexpr` constructor and stored in `.rdata`. The `decrypted` flag prevents double-deobfuscation. No runtime cost beyond the first-access XOR loop.
- **Phase 3 is prerequisite for Phase 4**: Without obfuscation, the injected agent bytes in Phase 4 would be recognized by memory scanners. Phase 3 ensures the wrapper and its payload are opaque to static analysis.

*Status: Items 1, 2, and 4 — 100% COMPLETE.*

### Deferred: Item 3 (Custom Packer)
A custom section packer (compressing `.text`/`.rdata` of the wrapper binary) was
analyzed but **deferred indefinitely** in favour of Phase 4 (Process Injection).

**Rationale**:
- Items 1 + 4 already defeat static AV/YARA/`strings` analysis of the wrapper — the
  highest-value detection vectors for the initial infection vector.
- The wrapper's lifespan is measured in seconds (extract → spawn → self-delete). It is
  never a persistent resident.
- Phase 4 eliminates the wrapper entirely from the operational footprint: the agent runs
  in-process inside a trusted host binary. Any investment in packing is obsoleted by
  Phase 4.
- Custom packing introduces significant fragility (entry point manipulation, relocation
  fixups, code signing invalidation across 3 platforms) for diminishing returns.

**Revisit condition**: If Phase 4 injection is not feasible on a target, or if the
wrapper binary itself becomes repeatedly signatured by on-access AV despite Items 1+4,
the custom packer remains available as a targeted countermeasure.

---

## 🧬 Phase 4: Process Injection — MacOS Dylib Injection Strategy

**Objective**: Eliminate the standalone `inferno_client` process. The agent runs as a
Mach-O dylib injected into a host process — memory-only, no binary on disk after
injection.

### Two-Tier Strategy

Modern macOS (10.14+) enforces **Hardened Runtime** on almost all third-party apps
(Discord, Zoom, Slack, browsers). `DYLD_INSERT_LIBRARIES` is blocked unless the target
has specific insecure entitlements. True process injection into a TCC-authorized app is
target-specific and cannot be guaranteed on every machine.

| Tier | Approach | TCC inheritance? | Guaranteed to work? |
|---|---|---|---|
| **Tier 1** (this phase) | Custom shim binary calls `dlopen()` on agent dylib | ❌ No | ✅ Yes — always works |
| **Tier 2** (future) | Entitlement scanner + injection into a vulnerable target app | ✅ If found | ❌ Depends on installed apps |

**Tier 1 achieves**: memory-only agent, no binary on disk, no `inferno_client` visible in
`ps`, bypasses on-access filesystem AV. The agent runs in a custom `inferno_shim` process
that we control.

**Tier 2 adds**: inheritance of the target app's TCC permissions — enabling camera,
microphone, screen recording, and filesystem access without prompting the user. This is a
**force multiplier** when the right vulnerable app is present.

### Tier 2 — Concrete Approach (Future Phase)

A scanner module enumerates `/Applications` and checks each binary's code signing
entitlements via `codesign -d --entitlements -` or direct Mach-O parsing. Targets are
reported to the C2 dashboard — the operator chooses one. Injection vector depends on
what the app allows:

| App has... | Injection vector |
|---|---|
| `com.apple.security.cs.allow-dyld-environment-variables` + `disable-library-validation` | `DYLD_INSERT_LIBRARIES` |
| `com.apple.security.cs.disable-library-validation` only | Mach `vm_allocate` + thread creation |
| No Hardened Runtime at all | `DYLD_INSERT_LIBRARIES` |
| Writable rpath directory | Dylib proxying / side-loading |

If no injectable app is found, Tier 2 reports failure — the agent continues via Tier 1
(shim) without crashing or blocking.

### Impact on Phase 4D (Media Capture)

| Feature | macOS (Tier 1 only) | macOS (Tier 2 succeeds) | Windows |
|---|---|---|---|
| Screenshot | ❌ Blocked by TCC | ✅ Via injected host | ✅ `BitBlt` |
| Camera | ❌ Blocked by TCC | ✅ Via injected host | ⚠️ One-click prompt |
| Keylogger | ✅ Already works | ✅ Same | ✅ Already works |

Phase 4D is **Windows-first + macOS Tier-2-dependent**. If Tier 2 never succeeds on a
given target, media capture on macOS is not possible without user prompting.

### Completed
- **Phase 4A** ✅ — Tier 1 implementation: macOS Dylib injection via custom shim
  (`entry_dylib.cpp`, `shim.cpp`). Agent runs memory-only inside `inferno_shim`
  process. Shim binary embedded in wrapper via XOR encryption, extracted at runtime.
  PR feedback: port validation (strtol), thread lifecycle (destructor join),
  dylib constructor verification via dlsym. **27 unit tests passing**.
- **Tier 2 Injector** ✅ — macOS entitlement scanner + Mach injection:
  - `EntitlementScanner` enumerates `/Applications`, parses code signing
    entitlements via `codesign` (fork+pipe+alarm for safety), classifies targets
    by injection vector (`DYLD_INSERT_LIBRARIES`, `MACH_VM_ALLOCATE`).
  - `MachInjector` launches target app with `DYLD_INSERT_LIBRARIES` via
    `fork`+`execv`, detecting immediate exits (already-running apps).
  - New opcode `SCAN_RESULT (0x0109)` for agent-to-server scanner reporting.
  - Injected agent calls scanner on `SYS_REQ_INFO`, sends report to server.
  - `InjectionPanel` dashboard tab displays per-agent scan results (dark theme).
  - Persistence: dylib copied to `~/.cache/com.apple.amp.itmstransporter.dylib`,
    launchd plist at `~/Library/LaunchAgents/com.inferno.agent.plist` launches
    target app executable directly with `DYLD_INSERT_LIBRARIES` on login.
  - Shim embedding fixed: `bin2header.py` now embeds both agent + shim, wrapper
    extracts both to PID-specific temp paths.
  - Server-side crypto already initialized (`server/src/main.cpp:44`).

### Bug Fixes — Persistence & GUI (Phase 4 Post-Commit)

| # | Symptom | Root Cause | Fix |
|---|---|---|---|
| 1 | After reboot, DBeaver launches but agent isn't injected | macOS Resume restores DBeaver before LaunchAgents run, bypassing `DYLD_INSERT_LIBRARIES` | Disabled macOS Resume via `defaults write -g NSQuitAlwaysKeepsWindows -bool false` |
| 2 | Plist `KeepAlive` ineffective with `open -a` | `open` exits immediately after launching the app, so launchd cannot track DBeaver's PID | Changed `ProgramArguments` from `/usr/bin/open -a DBeaver.app` to `/Applications/DBeaver.app/Contents/MacOS/dbeaver` directly |
| 3 | Agent dies when DBeaver is closed | `KeepAlive = false` in plist | Set `KeepAlive = true` so launchd auto-restarts DBeaver (and the injected agent) |
| 4 | White row-number column in Injection Targets / Intelligence Analysis tables on macOS | Qt's native Cocoa `QHeaderView` ignores `::section` background unless the base widget background is also set | Added `QHeaderView { background: #0c0c0c; }` to `INTEL_TABLE` stylesheet in `StyleSheets.hpp` |

### Phase 4A Completion — Manual Injection & Reconnection Hardening — [2026-06-22]

**Objective**: Complete the macOS injection stack with manual injection from the GUI, reconnection resilience, cross-platform build fixes, and CodeRabbit address.

#### Protocol Extensions
- **`INJECT (0x010A)`** — Server→Agent: inject dylib into target app. Payload = target executable path.
- **`INJECT_RES (0x010B)`** — Agent→Server: injection result. Payload = `path||capability|success(0/1)`.

#### Server
- `sendInjectCommand(ip, targetPath)` dispatches INJECT packet to agent by IP.
- `processPacketBuffer` handles INJECT_RES, emits `injectResultReceived` signal.
- New signal: `injectResultReceived(QString ip, bool success, QString targetPath)`.

#### Agent
- `handleInjection(Packet&&)` constructs `TargetApp` from path, calls `injectIntoTarget()`, sends INJECT_RES.
- `getAgentDylibPath()` — extracted shared helper for dylib path (used by both `handleInjection` and `persistInjectedAgent`).
- **Persistence guard**: `m_persistence_installed` flag prevents rewriting the plist on every `SYS_REQ_INFO`.

#### GUI — Injection Targets Panel
- Table expanded to 5 columns: Agent, Target App, Vector, Status, **Action**.
- Per-row **Inject** button: green (`#004d00` bg, `#00ff41` border) for "Ready" targets, grey/disabled for "✅ Injected".
- Button click emits `injectRequested(ip, targetPath)` → server → agent.
- `onInjectResult` updates the status cell and swaps the button to disabled "Injected" state.
- Full target path stored in `Qt::UserRole` for row identification.
- Action column fixed at 110px, buttons at 95×34px with increased padding.
- Row height set to 42px via `verticalHeader()->setDefaultSectionSize`.

#### Reconnection Fix (`Socket.cpp`)
- **`setReceiveTimeout(10s)`** — `recv()` returns after 10s if no data arrives, preventing the agent from blocking forever on a dead connection.
- **`setKeepAlive(30s idle, 10s interval)`** — TCP keepalive probes detect half-open connections.
- Both configured automatically in `Socket::connectTo()` after a successful connection.

#### Multi-Target Reporting
- Agent now sends **all** scanner targets (newline-delimited) instead of only `targets[0]`.
- Server parses multi-line report and creates one table row per target.
- Current host process detected via `_NSGetExecutablePath()` and marked as "✅ Injected".

#### Cross-Platform Build Fixes
- `MachInjector.cpp`: restructured with three compile paths — `INFERNO_TESTING` stub (returns true), `__APPLE__` real implementation (fork+execv), generic stub (returns false).
- Added `<cstdint>` include to `MachInjector.hpp` for `uint16_t`.
- Moved `MachInjector.cpp` and `EntitlementScanner.cpp` to general source lists (not just `if(APPLE)`).
- `_CRT_SECURE_NO_WARNINGS` added for MSVC `getenv` deprecation.
- `(void)idle_sec; (void)interval_sec;` guarded by `_WIN32` in `setKeepAlive`.
- Windows CI Qt version bumped from 6.6.0 to 6.7.3 (broken qtdeclarative archive).
- `scanner_test.cpp`: moved `#include "EntitlementScanner.hpp"` outside `__APPLE__` guard.

#### CodeRabbit Address
| # | Issue | Fix |
|---|---|---|
| 1 | Plist rewritten on every SYS_REQ_INFO | `m_persistence_installed` flag |
| 2 | Dylib path duplicated in 2 functions | Extracted `getAgentDylibPath()` |
| 3 | verify_phase4a.sh grep patterns wrong | Updated to match actual wrapper output |
| 4 | XML special chars in plist values | Added `escapeXml()` helper |
| 5 | readEntitlements comment says WNOHANG but code uses 0 | Fixed comment |
| 6 | Plist parsed line-by-line (fails on multi-line) | Read entire file into string |
| 7 | `(IOError, OSError)` redundant in Python 3 | Simplified to `OSError` |
| 8 | `fprintf` runtime string as format arg | Added `"%s"` format specifier |
| 9 | Shim dlopen race condition (500ms too tight) | Increased to 1s/2s delays |
| 10 | Duplicate decryption + wrong comment label | Copy temp dylib instead of re-decrypting; fixed "Step 2b" → "Step 2c" |

#### Tests
- `test_inject_packet_roundtrip` — INJECT opcode serialization/deserialization.
- `test_inject_res_packet_roundtrip` — INJECT_RES opcode round-trip.
- `test_inject_e2e` — full server↔client INJECT/INJECT_RES round-trip without forking.
- All **34 tests passing** across macOS, Linux, and Windows.

#### Verification
- `verify_phase4a.sh` manual verification passes:
  - Stage 1: All unit tests pass.
  - Stage 2: Shim + dylib loads correctly, agent starts in-process.
  - Stage 3: Wrapper injects into DBeaver (Tier 2), launches Tier 1 shim, both agents connect to C2.
  - No standalone `inferno_client` process (memory-only injection confirmed).
  - Dylib deleted from disk after loading.

### Phase 4B — Windows DLL Injection — [2026-06-24]

**Objective**: The agent runs as a Windows DLL injected into a target process, with the same two-tier approach as macOS (standalone DLL + process injection).

#### Tier 1 — Standalone DLL
- **`entry_dll.cpp`** — `DllMain` entry point that spawns the agent thread on `DLL_PROCESS_ATTACH`, signals shutdown on `DLL_PROCESS_DETACH`. Guarded by `INFERNO_DLL`.
- **`windows_loader.cpp`** — Minimal `.exe` that reads DLL path from `argv[1]` or `INFERNO_DLL_PATH` env var, calls `LoadLibraryA`, then `Sleep(INFINITE)`. Analogous to macOS `inferno_shim`.
- **Build target**: `inferno_agent_dll` (SHARED) + `inferno_loader` (EXECUTABLE) under `if(WIN32)`.

#### Tier 2 — Process Injection
- **`WindowsInjector.cpp`** — `CreateRemoteThread` + `LoadLibraryA` injection:
  1. `CreateToolhelp32Snapshot` to find the target PID by executable name
  2. `OpenProcess` with `PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE`
  3. `VirtualAllocEx` to allocate memory in the target for the DLL path
  4. `WriteProcessMemory` to write the path
  5. `CreateRemoteThread` starting at `LoadLibraryA` with the path as argument
  6. `WaitForSingleObject(5000ms)` then cleanup
- Platform-guarded: `MachInjector.cpp` for macOS, `WindowsInjector.cpp` for Windows — avoids linker conflict on `inferno::tier2::injectIntoTarget`.

#### Tests (Windows-only, guarded by `#ifdef _WIN32`)
- **`test_agent_dll_loads`** — `LoadLibrary` the production DLL, verify constructor ran via `GetProcAddress` on the exported `inferno_agent_entry_ran` symbol.
- **`test_loader_binary_exists`** — verify `inferno_loader.exe` exists in the build directory.
- **`test_windows_injector_stub`** — verify the `INFERNO_TESTING` injector stub returns true.

#### Detection Surface Analysis
The current `CreateRemoteThread(LoadLibraryA)` has a high EDR detection surface mapped to 5 vectors:

| # | Vector | Severity | Status |
|---|--------|----------|--------|
| 1 | `CreateRemoteThread` + `LoadLibraryA` — 4-call sequence signature | High | ❌ Not mitigated |
| 2 | `OpenProcess(PROCESS_ALL_ACCESS)` | High | ✅ Already using minimal specific masks |
| 3 | DLL file on disk — AV scan + forensic artifact | High | ❌ Not mitigated |
| 4 | DLL in PEB module list — visible to `EnumProcessModules` | Medium | ❌ Not mitigated |
| 5 | `WaitForSingleObject` + `VirtualFreeEx` cleanup pattern | Low | ❌ Not mitigated (low priority) |

#### Completed Evasion Hardening (4B.5)

All three workstreams implemented across two PRs:

**4B.5-A — Native API Injection** (`feat/injection/phase4b5-ntapi`):
- New `NtApi.hpp` / `NtApi.cpp`: NT API function pointer types + thread-safe runtime resolver via C++17 static lambda initializer
- All 6 Win32 injection APIs replaced with NT equivalents: `OpenProcess` → `NtOpenProcess`, `VirtualAllocEx` → `NtAllocateVirtualMemory`, `WriteProcessMemory` → `NtWriteVirtualMemory`, `CreateRemoteThread` → `NtCreateThreadEx`, `CloseHandle` → `NtClose`, `VirtualFreeEx` → `NtFreeVirtualMemory`
- Self-defined `MY_CLIENT_ID` / `MY_OBJECT_ATTRIBUTES` types to avoid `winternl.h` MSVC incompatibility
- `isResolved()` guard before dereferencing function pointers
- Self-defined `NT_SUCCESS` macro (no `ntstatus.h` dependency)
- Added `NtApi.cpp` to all Windows targets

**4B.5-B — Windows Process Scanner** (`feat/injection/phase4b5-ntapi`):
- Added `#elif defined(_WIN32)` branch to `EntitlementScanner.cpp`
- Enumerates processes via `CreateToolhelp32Snapshot` (`Process32FirstW`/`Process32NextW` for UNICODE safety)
- Filters: self PID, critical system process denylist (`csrss.exe`, `smss.exe`, `services.exe`, `lsass.exe`, `svchost.exe`, etc.), unknown executables
- Scoring: browsers/Office/Discord/Zoom/editors = 100, terminals/dev tools = 50
- Output format: `full_path|PID|1|is_host` — compatible with existing server `InjectionPanel`
- Updated `test_scanner_classification` to expect results on Windows

**4B.5-C — Execution-Only Injection** (`feat/injection/phase4b5-execution-only`):
- `findNtdllString()` — PE parser that scans `ntdll.dll`'s `.rdata` section for a needle string
- `injectExecutionOnly()` — uses the found ntdll string address as `LoadLibraryA` argument with `NtCreateThreadEx` only; no `VirtualAllocEx`/`WriteProcessMemory`
- `ReadProcessMemory` sanity check confirms the string exists at the same address in the target (ntdll is a known DLL with shared ASLR base across processes)
- Fallback: if execution-only fails (string not found, verification mismatch), silently falls back to standard NT API injection
- `injectIntoTarget()` tries execution-only first, then falls back
- 3 Windows unit tests: found, not-found, multi-char variants

**Pending (future)**:
- Reflective DLL loader (#4) — manual PE mapper, eliminates DLL-on-disk + PEB module list entry
- API call stack spoofing (#5) — Moonwalk++ / Draugr technique, deferred

### Detection Surface Update

| # | Vector | Severity | Status |
|---|--------|----------|--------|
| 1 | `CreateRemoteThread` + `LoadLibraryA` — 4-call sequence signature | High | ✅ Partially mitigated — `NtCreateThreadEx` bypasses `kernel32` hooks; execution-only path eliminates `VirtualAllocEx`/`WriteProcessMemory` |
| 2 | `OpenProcess(PROCESS_ALL_ACCESS)` | High | ✅ Already using minimal specific masks |
| 3 | DLL file on disk — AV scan + forensic artifact | High | ❌ Not mitigated (requires reflective loader) |
| 4 | DLL in PEB module list — visible to `EnumProcessModules` | Medium | ❌ Not mitigated (requires reflective loader) |
| 5 | `WaitForSingleObject` + `VirtualFreeEx` cleanup pattern | Low | ✅ Eliminated in execution-only path (no cleanup needed) |

---

## Phase 4B.5-#4 — Reflective DLL Loader ✅

**Objective**: Eliminate the DLL-on-disk artifact by manually mapping `inferno_agent.dll` from a memory buffer into the target process. The DLL never touches disk and is never registered in the PEB module list.

**New files**: `ReflectiveLoader.hpp`, `ReflectiveLoader.cpp`, `ReflectiveLdrShellcode.asm`, `reflective_loader_test.cpp`

### Technical milestones

- **`injectReflective()`**: Full orchestrator — parse PE → `NtAllocateVirtualMemory` at preferred base → write sections → resolve imports → apply relocations → launch shellcode → wait for DllMain → cleanup stub.
- **PE section mapper** (`mapPESections()`): Writes DOS/NT headers + each section (`.text`, `.rdata`, `.data`, `.reloc`) to the target at correct virtual addresses via `NtWriteVirtualMemory`.
- **Import resolver** (`resolveImports()`): Walks `IMAGE_IMPORT_DESCRIPTOR` array from the local buffer. For each imported DLL/function, calls `LoadLibraryA` + `GetProcAddress` in the injector (system DLLs share ASLR base across processes), writes the resolved address into the target's IAT via `WriteProcessMemory`.
- **Relocation applier** (`applyRelocations()`): Walks `.reloc` blocks from local buffer, reads current values from target via `ReadProcessMemory`, applies `delta = actual_base - preferred_base`, writes patched values back.
- **Shellcode stub**: 35-byte x64 byte array (with MASM `.asm` equivalent): reads `ReflectiveLoaderParams` from stack → calls `DllMain(hinstDLL, DLL_PROCESS_ATTACH, NULL)`. Embedded directly in the binary — no external dependency.
- **Target env var injection** (`setTargetEnv()`): Allocates `INFERNO_SERVER_IP` / `INFERNO_SERVER_PORT` strings in target memory via `NtAllocateVirtualMemory` / `NtWriteVirtualMemory`. Note: linking into PEB is incomplete (see follow-ups).

### Key design decisions

- **All parsing from local buffer**: `resolveImports` and `applyRelocations` read PE structures from the injector's local `dll_bytes` buffer, not from target memory (which isn't in our address space). Only writes go to the target via `WriteProcessMemory`/`NtWriteVirtualMemory`.
- **System-wide ASLR**: On Windows, system DLLs (`kernel32.dll`, `ntdll.dll`, etc.) are mapped at the same virtual address in every process. This allows resolving `LoadLibraryA` / `GetProcAddress` in the injector and writing the same address into the target's IAT.
- **`if constexpr` for compile-time constants**: Used in tests to avoid MSVC C4127 warning on `sizeof` comparisons.

### Detection Surface Update

| # | Vector | Severity | Status |
|---|--------|----------|--------|
| 1 | `CreateRemoteThread` + `LoadLibraryA` — 4-call sequence signature | High | ✅ Eliminated — replaced by `NtCreateThreadEx` + shellcode stub |
| 2 | `OpenProcess(PROCESS_ALL_ACCESS)` | High | ✅ Already using minimal specific masks |
| 3 | DLL file on disk — AV scan + forensic artifact | High | ✅ **Eliminated** — memory-only mapping |
| 4 | DLL in PEB module list — visible to `EnumProcessModules` | Medium | ✅ **Eliminated** — never registered |
| 5 | `WaitForSingleObject` + `VirtualFreeEx` cleanup pattern | Low | ✅ Eliminated in execution-only path |

### Test Coverage (Windows-only)

| Test | What it verifies |
|------|-----------------|
| `test_pe_header_validation` | Valid MZ/PE headers are accepted |
| `test_pe_header_invalid_signature` | Bad DOS signature is rejected |
| `test_pe_header_empty_buffer` | Empty buffer is rejected |
| `test_shellcode_bytes_valid` | Shellcode is non-null, < 128 bytes |
| `test_parameter_block_layout` | `ReflectiveLoaderParams` is exactly 16 bytes |
| `test_relocation_no_delta` | `applyRelocations` with delta=0 succeeds (no-op) |

---

## Follow-up #1 — Wire Reflective Loader into Injection Chain ✅

**Objective**: Make the reflective DLL loader the primary injection path, with automatic fallback to execution-only → NT API methods.

**New files**: `tests/wire_reflective_test.cpp`

**Modified files**: `WindowsInjector.cpp`, `ReflectiveLoader.cpp`, `tests/main_test.cpp`, `CMakeLists.txt`

### Technical milestones

- **`readBinaryFile()` helper**: Loads DLL from disk into `std::vector<uint8_t>` for the reflective mapper.
- **`injectIntoTarget(TargetApp& ...)`** — primary path used by `Agent::handleInjection`:
  - Opens target process → reads DLL bytes → calls `injectReflective()`
  - On failure: falls back to execution-only → NT API injection
- **`injectIntoTarget(DWORD pid, ...)`** — used by IFEO re-injector:
  - Same reflective-first strategy
  - Falls back to `injectIntoProcess()` (NT API LoadLibraryA)
- **`INFERNO_TESTING` stubs**: Only `injectReflective()` and `setTargetEnv()` are stubbed — PE helpers keep their real implementations for unit testing.

### Injection chain (final)

```
1. injectReflective()              ← fileless, no PEB entry
   Read DLL → map sections → resolve imports → relocations → DllMain

2. injectExecutionOnly()           ← no VirtualAllocEx
   Use ntdll "0" string as LoadLibraryA argument

3. injectViaRemoteThread()         ← full NT API
   NtAllocateVirtualMemory → NtWriteVirtualMemory → NtCreateThreadEx
```

### Next Steps
- **Phase 4D**: Media Capture — Camera snapshot + screenshot exfiltration
  (Windows-first, macOS Tier-2-dependent).
- **Phase 5**: Transport & Protocol Evasion — Malleable C2 framing, covert transports,
  mTLS 1.3.

---

## Follow-up #2 — Embed DLL Bytes in Agent Binary ✅

**Objective**: Make the reflective loader truly fileless by embedding an XOR-encrypted copy of
`inferno_agent.dll` inside `inferno_client.exe`. The agent decrypts it in memory and passes it
directly to `injectReflective()` — no file I/O at all.

### New files

- **`client/dll2header.py`** — Build-time script that XOR-encrypts the compiled DLL and emits
  `inferno_agent_dll_embedded.h` with `ENCRYPTED_DLL[]`, `XOR_KEY[]`, and `ENCRYPTED_DLL_SIZE`.
  Key (`2B7E151628AED2A6`) matches the wrapper's binary encryption key.
- **`client/include/EmbeddedDll.hpp`** — Includes the generated header and provides
  `decryptEmbeddedDll()` (returns `std::vector<uint8_t>` in memory) and
  `extractEmbeddedDllTo(path)` (last-resort disk write for LoadLibraryA fallback).
- **`tests/embedded_dll_test.cpp`** — Two tests:
  - `test_decrypted_dll_is_valid_pe` — decrypts, validates MZ/PE/NT signatures, machine type,
    and non-zero entry point.
  - `test_decrypted_dll_roundtrip_to_disk` — extracts via `extractEmbeddedDllTo`, reads back,
    validates DOS magic.

### Modified files

- **`CMakeLists.txt`** — On Windows, adds a `add_custom_command` (Python3) that runs
  `dll2header.py` after `inferno_agent_dll` builds. Wires the generated header as a dependency
  and include path for both `inferno_client` and `inferno_tests`. Defines
  `INFERNO_HAS_EMBEDDED_DLL` for agent/tests when Python3 is available; falls back to disk
  read with a CMake warning.
- **`client/src/WindowsInjector.cpp`** — Two-tier injection decision:
  - **EXE build** (`INFERNO_HAS_EMBEDDED_DLL` + no `INFERNO_DLL`): Tier 1 uses
    `decryptEmbeddedDll()` directly — no disk I/O. If reflective fails,
    `extractEmbeddedDllTo()` writes the DLL to `dll_path` just before the LoadLibraryA
    fallback (last resort, common path stays fileless).
  - **DLL build** (`INFERNO_DLL`) or **EXE without Python3**: Falls back to original
    `readBinaryFile(dll_path)` disk read.

### Detection Surface Update

| # | Vector | Severity | Status |
|---|--------|----------|--------|
| 1 | `CreateRemoteThread` + `LoadLibraryA` | High | ✅ Eliminated |
| 2 | `OpenProcess(PROCESS_ALL_ACCESS)` | High | ✅ Already mitigated |
| 3 | DLL file on disk — AV scan + forensic artifact | High | ✅ **Eliminated** — reflective path is fully fileless; last-resort disk write only on reflective failure |
| 4 | DLL in PEB module list | Medium | ✅ Eliminated |
| 5 | Shellcode / API sequence signatures | Low | ✅ Mitigated |

### Architecture Decisions

- **Dual path**: The EXE build decrypts from embedded bytes; the DLL build (which cannot embed
  itself) reads from disk. This avoids a circular dependency while keeping the primary injector
  fileless.
- **Last-resort extraction**: Only writes to disk if reflective injection fails *and* the
  LoadLibraryA fallback is about to run. The common path (reflective succeeds) never touches
  disk.
- **Test guards**: The test file is guarded by both `_WIN32` and `INFERNO_HAS_EMBEDDED_DLL` —
  skipped on non-Windows or when Python3 is unavailable. No test infrastructure change needed
  for other platforms.

---

## Follow-up #3 — Set Env Vars in Target PEB ✅

**Objective**: Make `INFERNO_SERVER_IP` and `INFERNO_SERVER_PORT` visible to the reflectively-loaded
DLL's `getenv()` by patching the target process's PEB environment block pointer. Previously,
`setTargetEnv()` allocated the env strings in target memory but never linked them into the PEB —
the DLL always fell back to `127.0.0.1:4242` regardless of what the injector passed.

### Modified files

- **`client/include/NtApi.hpp`** — Added self-defined structs for PEB walking (`MY_PEB`,
  `MY_RTL_USER_PROCESS_PARAMETERS`, `MY_PROCESS_BASIC_INFORMATION`) with x64 field offsets
  and `static_assert` layout verification. Added `pNtQueryInformationProcess` function pointer
  type and `NtQueryInformationProcess` member to the `NtApi` struct.

- **`client/src/NtApi.cpp`** — Resolved `NtQueryInformationProcess` from ntdll alongside
  existing NT API functions.

- **`client/src/ReflectiveLoader.cpp`** — Replaced the `setTargetEnv()` no-op stub with real
  PEB patching:
  1. Allocates env strings in target memory via `NtAllocateVirtualMemory` (unchanged)
  2. Calls `NtQueryInformationProcess` to get the PEB base address
  3. Reads `PEB.ProcessParameters` via `ReadProcessMemory`
  4. Patches the Environment field inside ProcessParameters (+0x80) to point
     at our allocated block via `WriteProcessMemory`
  6. On any failure, logs and returns `true` (DLL uses fallback defaults) — no regression
  - x86 fallback: skipped with a log message (x64-only, matching the reflective loader's architecture)
  - `setTargetEnv` moved outside the `INFERNO_TESTING` guard so unit tests can call it directly

### New files

- **`tests/peb_env_test.cpp`** — Two tests (Windows x64 only):
  - `test_peb_env_vars_visible` — calls `setTargetEnv(GetCurrentProcess(), "1.2.3.4", 5678)`,
    then verifies `GetEnvironmentVariable` returns the injected values. Restores originals.
  - `test_peb_env_vars_fallback_on_failure` — verifies the function returns `true` (best-effort)
    even when parts of the pipeline fail.

### Architecture Decisions

- **Struct completeness**: Defined full `MY_PEB` and `MY_RTL_USER_PROCESS_PARAMETERS` structs
  with correct x64 offsets rather than hardcoded magic numbers. `static_assert` guards catch
  layout regressions.
- **No RtlFreeHeap on old environment**: The old environment block pointer is read but not freed.
  A single `PAGE_READWRITE` allocation (~100 bytes) per injection is negligible and cleaned up
  on process exit. Calling `RtlFreeHeap` would require a remote thread, increasing detection
  surface.
- **No RtlCreateEnvironment/RtlSetEnvironmentVariable**: The PEB walk approach is simpler and
  avoids a secondary remote thread. These can be added in the future if needed.
- **x64 only**: The struct layouts are x64-specific. x86 support deferred — would require
  different struct layouts (32-bit pointers, different offsets) and is incompatible with the
  existing x64-only shellcode.
