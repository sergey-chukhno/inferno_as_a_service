#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <string>
#include "Opcodes.hpp"

namespace inferno {

#pragma pack(push, 1) // Ensures no padding is added to the struct by compiler

struct PacketHeader {
    uint32_t magic; //Fixed to 0xDEADBEEF
    uint16_t opcode; //Command type (e.g., 0x0001, 0x0002, etc.)
    uint32_t payload_size; //Size of the following payload in bytes
    uint32_t checksum; //CRC32 of the payload to verify integrity
    };
    
    
#pragma pack(pop)

class Packet {
public:
    static constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB limit

private:
    PacketHeader m_header;
    std::vector<uint8_t> m_payload;

    void calculateChecksum();

    Packet(const PacketHeader& header, std::vector<uint8_t> payload); // Private constructor used by deserialize()

public:
    Packet();
    explicit Packet(uint16_t opcode, const std::vector<uint8_t>& payload);
    explicit Packet(uint16_t opcode, const std::string& string_payload); // Overload for easy text messages
    ~Packet();
    Packet(const Packet&)            = delete;
    Packet& operator=(const Packet&) = delete;

    // Move semantics
    Packet(Packet&& other) noexcept = default;
    Packet& operator=(Packet&& other) noexcept = default;

    static std::optional<Packet> deserialize(const std::vector<uint8_t>& raw_data); // Factory to build packet from raw bytes stream

    [[nodiscard]] std::vector<uint8_t> serialize() const; // Convert packet to raw bytes for sending



    // Getters
    [[nodiscard]] uint16_t getOpcode() const;
    [[nodiscard]] const std::vector<uint8_t>& getPayload() const;
}; 
}
