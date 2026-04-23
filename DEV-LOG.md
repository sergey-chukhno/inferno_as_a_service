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

---
*Next: Establishing Remote Shell capabilities (Circle 4)...*
