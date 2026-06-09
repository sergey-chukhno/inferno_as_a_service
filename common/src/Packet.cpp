#include "../include/Packet.hpp"
#include "../include/CryptoContext.hpp"
#include <cstring>
#include <iostream>
#include <optional>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace inferno {

static constexpr uint32_t MAGIC = 0xDEADBEEF;

// ── Constructors ──────────────────────────────────────────────

Packet::Packet() {
    m_header.magic         = MAGIC;
    m_header.opcode        = 0;
    m_header.payload_size  = 0;
}

Packet::Packet(uint16_t opcode, const std::vector<uint8_t>& payload)
    : m_payload(payload) {
    m_header.magic         = MAGIC;
    m_header.opcode        = opcode;
    m_header.payload_size  = 0;  // computed in serialize()
}

Packet::Packet(uint16_t opcode, const std::string& string_payload)
    : m_payload(string_payload.begin(), string_payload.end()) {
    m_header.magic         = MAGIC;
    m_header.opcode        = opcode;
    m_header.payload_size  = 0;
}

Packet::Packet(const PacketHeader& header, std::vector<uint8_t> payload, size_t wire_payload_size)
    : m_header(header), m_payload(std::move(payload)), m_wire_payload_size(wire_payload_size) {}

Packet::~Packet() = default;

// ── Serialize ─────────────────────────────────────────────────

std::vector<uint8_t> Packet::serialize() const {
    const auto& ctx = CryptoContext::instance();
    static bool warned = false;

    // Build opcode AAD (2 bytes, network order) — authenticates the command
    // type so an attacker cannot tamper with it in transit. payload_size is
    // excluded from AAD because it's not known until after encryption.
    const uint8_t aad[2] = {
        static_cast<uint8_t>((m_header.opcode >> 8) & 0xFF),
        static_cast<uint8_t>(m_header.opcode & 0xFF)
    };
    std::vector<uint8_t> aad_vec(aad, aad + sizeof(aad));

    // Encrypt payload with opcode as AAD
    std::vector<uint8_t> wire_payload;
    if (ctx.isInitialized()) {
        wire_payload = ctx.encrypt(m_payload, aad_vec);
        if (wire_payload.empty()) {
            std::cerr << "[Packet] Encryption failed.\n";
            wire_payload = m_payload;
        }
    } else {
        if (!warned) {
            std::cerr << "[Packet] CryptoContext not initialized — sending cleartext.\n";
            warned = true;
        }
        wire_payload = m_payload;
    }

    if (wire_payload.size() > MAX_WIRE_SIZE) {
        std::cerr << "[Packet] Payload exceeds maximum size.\n";
        return {};
    }

    PacketHeader net_header;
    net_header.magic         = htonl(m_header.magic);
    net_header.opcode        = htons(m_header.opcode);
    net_header.payload_size  = htonl(static_cast<uint32_t>(wire_payload.size()));

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&net_header);
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(PacketHeader) + wire_payload.size());
    buffer.insert(buffer.end(), hdr, hdr + sizeof(PacketHeader));
    buffer.insert(buffer.end(), wire_payload.begin(), wire_payload.end());
    return buffer;
}

// ── Deserialize ───────────────────────────────────────────────

std::optional<Packet> Packet::deserialize(const std::vector<uint8_t>& raw_data) {
    if (raw_data.size() < sizeof(PacketHeader)) {
        return std::nullopt;
    }

    PacketHeader header;
    std::memcpy(&header, raw_data.data(), sizeof(PacketHeader));

    header.magic         = ntohl(header.magic);
    header.opcode        = ntohs(header.opcode);
    header.payload_size  = ntohl(header.payload_size);

    if (header.magic != MAGIC) {
        return std::nullopt;
    }

    if (header.payload_size > MAX_WIRE_SIZE) {
        return std::nullopt;
    }

    if (raw_data.size() < sizeof(PacketHeader) + header.payload_size) {
        return std::nullopt;
    }

    size_t wire_payload_size = header.payload_size;

    std::vector<uint8_t> wire_payload(
        raw_data.begin() + sizeof(PacketHeader),
        raw_data.begin() + sizeof(PacketHeader) + wire_payload_size);

    // Decrypt
    const auto& ctx = CryptoContext::instance();
    std::vector<uint8_t> plaintext;

    if (ctx.isInitialized() && wire_payload.size() >= CryptoContext::OVERHEAD) {
        // AAD: the opcode bytes (same as during serialize)
        const uint8_t aad[2] = {
            static_cast<uint8_t>((header.opcode >> 8) & 0xFF),
            static_cast<uint8_t>(header.opcode & 0xFF)
        };
        std::vector<uint8_t> aad_vec(aad, aad + 2);
        auto decrypted = ctx.decrypt(wire_payload, aad_vec);
        if (!decrypted.has_value()) {
            std::cerr << "[Packet] Decryption failed (tampered or wrong key).\n";
            return std::nullopt;
        }
        plaintext = std::move(*decrypted);
    } else {
        // Either not initialized, or payload is too small to be encrypted.
        // Treat as plaintext.
        plaintext = std::move(wire_payload);
    }

    if (plaintext.size() > MAX_PLAINTEXT_SIZE) {
        return std::nullopt;
    }

    return Packet(header, std::move(plaintext), wire_payload_size);
}

// ── Getters ───────────────────────────────────────────────────

uint16_t Packet::getOpcode() const {
    return m_header.opcode;
}

const std::vector<uint8_t>& Packet::getPayload() const {
    return m_payload;
}

size_t Packet::getWirePayloadSize() const {
    return m_wire_payload_size;
}

} // namespace inferno
