#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <string>
#include "Opcodes.hpp"

namespace inferno {

#pragma pack(push, 1)

struct PacketHeader {
    uint32_t magic;         // 0xDEADBEEF
    uint16_t opcode;        // Command type
    uint32_t payload_size;  // Size of payload data on wire (includes GCM IV + tag)
};

#pragma pack(pop)

class Packet {
public:
    // Max plaintext payload. Wire payload adds CryptoContext::OVERHEAD (28 bytes).
    static constexpr size_t MAX_PLAINTEXT_SIZE  = 10 * 1024 * 1024; // 10MB
    static constexpr size_t MAX_WIRE_SIZE       = MAX_PLAINTEXT_SIZE + 28;

private:
    PacketHeader m_header;
    std::vector<uint8_t> m_payload;
    size_t m_wire_payload_size = 0;

    Packet(const PacketHeader& header, std::vector<uint8_t> payload, size_t wire_payload_size);

public:
    Packet();
    explicit Packet(uint16_t opcode, const std::vector<uint8_t>& payload);
    explicit Packet(uint16_t opcode, const std::string& string_payload);
    ~Packet();
    Packet(const Packet&)            = delete;
    Packet& operator=(const Packet&) = delete;

    Packet(Packet&& other) noexcept = default;
    Packet& operator=(Packet&& other) noexcept = default;

    static std::optional<Packet> deserialize(const std::vector<uint8_t>& raw_data);
    std::vector<uint8_t> serialize() const;

    [[nodiscard]] uint16_t getOpcode() const;
    [[nodiscard]] const std::vector<uint8_t>& getPayload() const;
    [[nodiscard]] size_t getWirePayloadSize() const;
};

} // namespace inferno
