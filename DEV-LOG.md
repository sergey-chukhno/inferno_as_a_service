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
- **Design System established**: Created `styles.qss` defining the "Inferno" aesthetic (Neon Green on Deep Charcoal).
- **High-Fidelity MainWindow**: Implemented a three-pane layout (Agents, Telemetry, Keylogs) using a robust `QSplitter` architecture.
- **Interactive Animation Engine**: 
    *   **Rotating Radar**: Implemented a `QTimer`-driven rotation for the Scan button.
    *   **State-Based Surveillance**: Developed a toggleable Eye icon system with distinct 'closed' and 'active neon' states.
    *   **Tactical Data Stream**: Built a multi-line, high-density binary/hex scrolling footer for real-time visual feedback.
- **Asset Optimization**: Migrated from sprite-sheets to dedicated state-based PNG assets for pixel-perfect icon rendering.

- **High-Fidelity Dashboard**: Completed transition to 3-pane responsive operational console.
- **Network Wiring**: Linked GUI slots to `Server` binary packet dispatchers for Shell, Procs, and Keylog.
- **Surveillance Pulse**: Implemented 1.5s polling loop for live keystroke streaming with 'Smart Silence' filtering.
- **Operator UX**: 
    *   Created custom `CommandDialog` (Dark/Neon, Multi-line, Copy/Paste).
    *   Enhanced Telemetry Console with integrated Search and Clear utilities.
    *   Implemented 'True Tux' Linux icons and dynamic OS platform detection.
- **Bug Fixes**: Resolved startup segfault (Initialization order) and Shell payload truncation (Protocol mismatch).

### Security Architecture Decisions
- **UI/Net Isolation**: By decoupling the UI from the networking thread, we prevent GUI hangs (e.g., during window dragging) from causing socket timeouts or TCP buffer overflows.
- **Silent Intelligence**: Data filters ensure only meaningful intelligence is displayed, reducing operator cognitive load and preventing dashboard noise.

*Next: Circle 5 (Wrath) — Propagation & Privilege Escalation...*
