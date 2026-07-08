#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <string>
#include "Opcodes.hpp"
#include "CryptoContext.hpp"

namespace inferno {

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;         // 0xDEADBEEF (legacy) or masked
    uint16_t opcode;        // Malleable: XOR-masked
    uint32_t payload_size;  // Malleable: XOR-masked
};
#pragma pack(pop)

// Malleable header — 10 bytes, same size as PacketHeader
struct MalleableHeader {
    uint8_t bytes[10];
};

class Packet {
public:
    static constexpr size_t MAX_PLAINTEXT_SIZE  = 10 * 1024 * 1024;
    static constexpr size_t MAX_WIRE_SIZE       = MAX_PLAINTEXT_SIZE + 28;
    static constexpr size_t SESSION_KEY_SIZE    = CryptoContext::SESSION_KEY_SIZE;

    // Legacy magic for backward compat
    static constexpr uint32_t LEGACY_MAGIC = 0xDEADBEEF;

    // ── Constructors ──────────────────────────────────────────

    Packet();

    // Legacy: no session key, uses 0xDEADBEEF magic
    explicit Packet(uint16_t opcode, const std::vector<uint8_t>& payload);
    explicit Packet(uint16_t opcode, const std::string& string_payload);

    // Malleable: with session key and packet counter
    explicit Packet(uint16_t opcode, const std::string& string_payload,
                    const uint8_t* session_key, uint64_t packet_counter);

    ~Packet();
    Packet(const Packet&)            = delete;
    Packet& operator=(const Packet&) = delete;
    Packet(Packet&& other) noexcept = default;
    Packet& operator=(Packet&& other) noexcept = default;

    // ── Serialize ─────────────────────────────────────────────

    // If session_key was provided, produces malleable wire format.
    // Otherwise, produces legacy 0xDEADBEEF format.
    std::vector<uint8_t> serialize() const;

    // ── Deserialize (static) ──────────────────────────────────

    // Auto: tries malleable first, falls back to legacy.
    // If session_key is null, only tries legacy.
    static std::optional<Packet> deserialize(const std::vector<uint8_t>& raw_data,
                                              const uint8_t* session_key = nullptr,
                                              uint64_t packet_counter = 0);

    // ── Getters ───────────────────────────────────────────────

    [[nodiscard]] uint16_t getOpcode() const;
    [[nodiscard]] const std::vector<uint8_t>& getPayload() const;
    [[nodiscard]] size_t getWirePayloadSize() const;

private:
    Packet(const PacketHeader& header, std::vector<uint8_t> payload,
           size_t wire_payload_size);

    // Malleable internals
    static std::vector<uint8_t> buildMalleableMask(const uint8_t* session_key,
                                                    uint64_t packet_counter);

    bool m_use_malleable = false;
    uint8_t m_session_key[SESSION_KEY_SIZE]{};
    uint64_t m_packet_counter = 0;

    PacketHeader m_header;
    std::vector<uint8_t> m_payload;
    size_t m_wire_payload_size = 0;
};

} // namespace inferno
