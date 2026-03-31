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
- `0x0001` : System Information Request (Server -> Client)
- `0x0002` : System Information Response (Client -> Server)
- `0x0003` : Execute Shell Command (Server -> Client)
- `0x0004` : Shell Command Output (Client -> Server)

### Surveillance (Gourmandise)
- `0x0100` : Start Keylogger (Server -> Client)
- `0x0101` : Stop Keylogger (Server -> Client)
- `0x0102` : Keylogger Buffer Dump (Client -> Server)
- `0x0103` : Start/Stop File Stream (Server <-> Client)
- `0x0104` : Start/Stop Camera/Desktop Event Stream (Server <-> Client)

## 4. Endianness
All multi-byte integers (`uint16_t`, `uint32_t`) MUST be transmitted in Network Byte Order (Big-Endian). Implementations MUST use `htonl()`/`ntohl()` and `htons()`/`ntohs()`.