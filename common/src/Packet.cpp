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

// ── Legacy magic ──────────────────────────────────────────────

static constexpr uint32_t MAGIC = 0xDEADBEEF;

// ── Malleable header layout variants ──────────────────────────
// Each variant maps the 3 fields (opcode:2, size:4, reserved:4 = 10 bytes)
// into the 10-byte MalleableHeader differently.
// Masks are applied byte-by-byte at the positions below.

enum MalleableVariant : uint8_t {
    VAR_0 = 0,  // size(4) | opcode(2) | reserved(4)
    VAR_1 = 1,  // opcode(2) | reserved(2) | size_hi(2) | size_lo(2)
    VAR_2 = 2,  // reserved(1) | size_hi(2) | opcode(2) | size_lo(2) | reserved(3)
    VAR_COUNT
};

struct VariantLayout {
    // For each byte position in the 10-byte header, what field does it hold?
    // 0 = reserved (unused; GCM authentication validates the variant)
    // 1 = opcode byte 0 (high)
    // 2 = opcode byte 1 (low)
    // 3 = size byte 0 (high)
    // 4 = size byte 1
    // 5 = size byte 2
    // 6 = size byte 3 (low)
    int fields[10];
};

static const VariantLayout kLayouts[3] = {
    // VAR_0: [size(4)] [opcode(2)] [reserved(4)]
    { { 3, 4, 5, 6, 1, 2, 0, 0, 0, 0 } },
    // VAR_1: [opcode(2)] [reserved(2)] [size_hi(2)] [size_lo(2)]
    { { 1, 2, 0, 0, 3, 4, 5, 6, 0, 0 } },
    // VAR_2: [res(1)] [size_hi(2)] [opcode(2)] [size_lo(2)] [res(3)]
    { { 0, 3, 4, 1, 2, 5, 6, 0, 0, 0 } }
};

// ── Constructors ──────────────────────────────────────────────

Packet::Packet() {
    m_header.magic        = MAGIC;
    m_header.opcode       = 0;
    m_header.payload_size = 0;
}

Packet::Packet(uint16_t opcode, const std::vector<uint8_t>& payload)
    : m_payload(payload) {
    m_header.magic        = MAGIC;
    m_header.opcode       = opcode;
    m_header.payload_size = 0;
}

Packet::Packet(uint16_t opcode, const std::string& string_payload)
    : m_payload(string_payload.begin(), string_payload.end()) {
    m_header.magic        = MAGIC;
    m_header.opcode       = opcode;
    m_header.payload_size = 0;
}

Packet::Packet(uint16_t opcode, const std::string& string_payload,
               const uint8_t* session_key, uint64_t packet_counter)
    : m_use_malleable(true), m_packet_counter(packet_counter),
      m_payload(string_payload.begin(), string_payload.end()) {
    std::memcpy(m_session_key, session_key, SESSION_KEY_SIZE);
    m_header.magic        = MAGIC;
    m_header.opcode       = opcode;
    m_header.payload_size = 0;
}

Packet::Packet(const PacketHeader& header, std::vector<uint8_t> payload,
               size_t wire_payload_size)
    : m_header(header), m_payload(std::move(payload)),
      m_wire_payload_size(wire_payload_size) {}

Packet::~Packet() = default;

// ── Malleable mask builder ────────────────────────────────────

std::vector<uint8_t> Packet::buildMalleableMask(const uint8_t* session_key,
                                                 uint64_t packet_counter) {
    // Derive 32 bytes of mask material: HMAC(session_key, counter)
    auto counter_bytes = std::vector<uint8_t>(sizeof(packet_counter));
    for (size_t i = 0; i < sizeof(packet_counter); ++i) {
        counter_bytes[i] = static_cast<uint8_t>(
            (packet_counter >> (56 - i * 8)) & 0xFF);
    }
    return CryptoContext::hmacSha256(session_key, SESSION_KEY_SIZE,
                                      counter_bytes.data(),
                                      counter_bytes.size());
}

// ── Serialize ─────────────────────────────────────────────────

std::vector<uint8_t> Packet::serialize() const {
    const auto& ctx = CryptoContext::instance();
    static bool warned = false;

    // AAD: opcode (2 bytes, network order)
    const uint8_t aad[2] = {
        static_cast<uint8_t>((m_header.opcode >> 8) & 0xFF),
        static_cast<uint8_t>(m_header.opcode & 0xFF)
    };
    std::vector<uint8_t> aad_vec(aad, aad + sizeof(aad));

    // Encrypt payload
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

    if (m_use_malleable) {
        // ── Malleable format ─────────────────────────────────
        auto mask = buildMalleableMask(m_session_key, m_packet_counter);
        if (mask.size() < 10) return {};

        MalleableVariant variant = static_cast<MalleableVariant>(mask[0] % VAR_COUNT);
        const auto& layout = kLayouts[variant];

        uint8_t opcode_bytes[2] = {
            static_cast<uint8_t>((m_header.opcode >> 8) & 0xFF),
            static_cast<uint8_t>(m_header.opcode & 0xFF)
        };
        uint8_t size_bytes[4] = {
            static_cast<uint8_t>((wire_payload.size() >> 24) & 0xFF),
            static_cast<uint8_t>((wire_payload.size() >> 16) & 0xFF),
            static_cast<uint8_t>((wire_payload.size() >> 8) & 0xFF),
            static_cast<uint8_t>(wire_payload.size() & 0xFF)
        };

        // Build plaintext header (reserved bytes stay 0)
        uint8_t plaintext[10] = {0};
        for (int i = 0; i < 10; ++i) {
            int f = layout.fields[i];
            if (f == 1) plaintext[i] = opcode_bytes[0];
            else if (f == 2) plaintext[i] = opcode_bytes[1];
            else if (f >= 3 && f <= 6) plaintext[i] = size_bytes[f - 3];
        }

        // XOR with mask
        MalleableHeader raw;
        for (int i = 0; i < 10; ++i) {
            raw.bytes[i] = plaintext[i] ^ mask[i];
        }

        // Build wire buffer: 10-byte malleable header + encrypted payload
        std::vector<uint8_t> buffer;
        buffer.reserve(10 + wire_payload.size());
        buffer.insert(buffer.end(), raw.bytes, raw.bytes + 10);
        buffer.insert(buffer.end(), wire_payload.begin(), wire_payload.end());
        return buffer;
    }

    // ── Legacy format ────────────────────────────────────────
    // Build header byte-by-byte in big-endian wire order
    // (avoid unaligned access on ARM from packed struct)
    std::vector<uint8_t> buffer;
    buffer.reserve(10 + wire_payload.size());
    // magic (4 bytes, big-endian)
    buffer.push_back(static_cast<uint8_t>((m_header.magic >> 24) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((m_header.magic >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((m_header.magic >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(m_header.magic & 0xFF));
    // opcode (2 bytes, big-endian)
    buffer.push_back(static_cast<uint8_t>((m_header.opcode >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(m_header.opcode & 0xFF));
    // payload_size (4 bytes, big-endian)
    uint32_t sz = static_cast<uint32_t>(wire_payload.size());
    buffer.push_back(static_cast<uint8_t>((sz >> 24) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((sz >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((sz >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(sz & 0xFF));
    buffer.insert(buffer.end(), wire_payload.begin(), wire_payload.end());
    return buffer;
}

// ── Deserialize ───────────────────────────────────────────────

std::optional<Packet> Packet::deserialize(const std::vector<uint8_t>& raw_data,
                                           const uint8_t* session_key,
                                           uint64_t packet_counter) {
    if (raw_data.size() < 10) return std::nullopt;

        if (session_key) {
            // Try malleable: try all 3 variants, attempt decrypt on each
            auto mask = buildMalleableMask(session_key, packet_counter);
            if (mask.size() >= 10) {
                MalleableHeader raw_header;
                std::memcpy(raw_header.bytes, raw_data.data(), 10);
                for (int v = 0; v < VAR_COUNT; ++v) {
                    const auto& layout = kLayouts[v];

                    // Unmask fields for this variant
                    uint8_t opcode_bytes[2] = {0, 0};
                    uint8_t size_bytes[4] = {0, 0, 0, 0};
                    for (int i = 0; i < 10; ++i) {
                        uint8_t p = raw_header.bytes[i] ^ mask[i];
                        int f = layout.fields[i];
                        if (f == 1) opcode_bytes[0] = p;
                        else if (f == 2) opcode_bytes[1] = p;
                        else if (f >= 3 && f <= 6) size_bytes[f - 3] = p;
                    }

                    uint16_t cand_opcode =
                        (static_cast<uint16_t>(opcode_bytes[0]) << 8) |
                         static_cast<uint16_t>(opcode_bytes[1]);
                    uint32_t cand_sz =
                        (static_cast<uint32_t>(size_bytes[0]) << 24) |
                        (static_cast<uint32_t>(size_bytes[1]) << 16) |
                        (static_cast<uint32_t>(size_bytes[2]) << 8)  |
                         static_cast<uint32_t>(size_bytes[3]);

                    if (cand_sz > MAX_WIRE_SIZE) continue;
                    if (raw_data.size() < 10 + cand_sz) continue;

                    std::vector<uint8_t> wire_payload(
                        raw_data.begin() + 10,
                        raw_data.begin() + 10 + cand_sz);

                    // Attempt decrypt — GCM auth tag confirms correct variant
                    const auto& ctx = CryptoContext::instance();
                    std::vector<uint8_t> plaintext;
                    bool decrypted_ok = false;

                    if (ctx.isInitialized()) {
                        // Must have at least GCM overhead to be valid
                        if (wire_payload.size() < CryptoContext::OVERHEAD) continue;
                        const uint8_t aad[2] = {
                            static_cast<uint8_t>((cand_opcode >> 8) & 0xFF),
                            static_cast<uint8_t>(cand_opcode & 0xFF)
                        };
                        std::vector<uint8_t> aad_vec(aad, aad + 2);
                        auto d = ctx.decrypt(wire_payload, aad_vec);
                        if (d.has_value()) {
                            plaintext = std::move(*d);
                            decrypted_ok = true;
                        }
                    } else {
                        // No crypto — accept first variant with matching wire_sz
                        if (cand_sz != raw_data.size() - 10) continue;
                        plaintext = std::move(wire_payload);
                        decrypted_ok = true;
                    }

                    if (decrypted_ok && plaintext.size() <= MAX_PLAINTEXT_SIZE) {
                        PacketHeader h;
                        h.magic = MAGIC;
                        h.opcode = cand_opcode;
                        h.payload_size = cand_sz;
                        return Packet(h, std::move(plaintext), cand_sz);
                    }
                }
            }
            // All malleable variants failed — return nullopt
            return std::nullopt;
        }

    // Legacy: must have at least sizeof(PacketHeader) bytes
    // (only reached when session_key is null — no malleable attempted)
    // Use byte-level reads to avoid unaligned access on ARM from packed struct.
    if (raw_data.size() < 10) return std::nullopt;

    uint32_t legacy_magic =
        (static_cast<uint32_t>(raw_data[0]) << 24) |
        (static_cast<uint32_t>(raw_data[1]) << 16) |
        (static_cast<uint32_t>(raw_data[2]) << 8)  |
         static_cast<uint32_t>(raw_data[3]);
    uint16_t legacy_opcode =
        (static_cast<uint16_t>(raw_data[4]) << 8) |
         static_cast<uint16_t>(raw_data[5]);
    uint32_t legacy_size =
        (static_cast<uint32_t>(raw_data[6]) << 24) |
        (static_cast<uint32_t>(raw_data[7]) << 16) |
        (static_cast<uint32_t>(raw_data[8]) << 8)  |
         static_cast<uint32_t>(raw_data[9]);

    if (legacy_magic != MAGIC) return std::nullopt;
    if (legacy_size > MAX_WIRE_SIZE) return std::nullopt;
    if (raw_data.size() < 10 + legacy_size) return std::nullopt;

    size_t wire_payload_size = legacy_size;
    std::vector<uint8_t> wire_payload(
        raw_data.begin() + 10,
        raw_data.begin() + 10 + wire_payload_size);

    const auto& ctx = CryptoContext::instance();
    std::vector<uint8_t> plaintext;
    if (ctx.isInitialized() && wire_payload.size() >= CryptoContext::OVERHEAD) {
        const uint8_t aad[2] = {
            static_cast<uint8_t>((legacy_opcode >> 8) & 0xFF),
            static_cast<uint8_t>(legacy_opcode & 0xFF)
        };
        std::vector<uint8_t> aad_vec(aad, aad + 2);
        auto decrypted = ctx.decrypt(wire_payload, aad_vec);
        if (!decrypted.has_value()) return std::nullopt;
        plaintext = std::move(*decrypted);
    } else {
        plaintext = std::move(wire_payload);
    }
    if (plaintext.size() > MAX_PLAINTEXT_SIZE) return std::nullopt;

    PacketHeader h;
    h.magic = MAGIC;
    h.opcode = legacy_opcode;
    h.payload_size = legacy_size;
    return Packet(h, std::move(plaintext), wire_payload_size);
}

// ── Getters ───────────────────────────────────────────────────

uint16_t Packet::getOpcode() const { return m_header.opcode; }
const std::vector<uint8_t>& Packet::getPayload() const { return m_payload; }
size_t Packet::getWirePayloadSize() const { return m_wire_payload_size; }

} // namespace inferno
