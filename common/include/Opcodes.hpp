#pragma once
#include <cstdint>

namespace inferno {

    enum class Opcode : uint16_t {
        // System & Control
        PING = 0x0000, // Heartbeat, checks if node is alive
        PONG = 0x0001, // Response to ping
        SYS_REQ_INFO = 0x0002, // System Information Request
        SYS_RES_INFO = 0x0003, // System Information Response
        CMD_EXEC = 0x0004,
        CMD_RES = 0x0005,
        
        KEYLOG_START = 0x0100,
        KEYLOG_STOP = 0x0101,
        KEYLOG_DUMP = 0x0102,
        STREAM_FILE = 0x0103,
        STREAM_CAMERA = 0x0104,
        STREAM_DESKTOP = 0x0105,
    };

} // namespace inferno
