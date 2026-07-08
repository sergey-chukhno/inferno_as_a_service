#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../common/include/Packet.hpp"
#include "../common/include/CryptoContext.hpp"

static const uint8_t kTestKey[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

void test_hmac_consistency() {
    // Verify HMAC produces the same output for the same input
    std::vector<uint8_t> key(kTestKey, kTestKey + 16);
    std::vector<uint8_t> data(8, 0); // counter = 0

    auto result1 = inferno::CryptoContext::hmacSha256(key, data);
    auto result2 = inferno::CryptoContext::hmacSha256(key, data);

    if (result1.size() != 32 || result2.size() != 32) {
        std::fprintf(stderr, "[FAIL] test_hmac_consistency: "
                             "unexpected size %zu / %zu\n",
                     result1.size(), result2.size());
        std::exit(1);
    }

    for (int i = 0; i < 32; ++i) {
        if (result1[i] != result2[i]) {
            std::fprintf(stderr, "[FAIL] test_hmac_consistency: "
                                 "mismatch at byte %d\n", i);
            std::exit(1);
        }
    }

    // Verify different counter produces different output
    std::vector<uint8_t> data2(8, 0); data2[7] = 1; // counter = 1
    auto result3 = inferno::CryptoContext::hmacSha256(key, data2);
    bool all_same = true;
    for (int i = 0; i < 32; ++i) {
        if (result1[i] != result3[i]) { all_same = false; break; }
    }
    if (all_same) {
        std::fprintf(stderr, "[FAIL] test_hmac_consistency: "
                             "different counter produced same HMAC\n");
        std::exit(1);
    }

    std::fprintf(stdout, "[PASS] test_hmac_consistency\n");
}

void test_malleable_header_only_roundtrip() {
    // Test with an empty payload (bypasses encryption entirely)
    for (uint64_t counter = 0; counter < 30; ++counter) {
        inferno::Packet pkt(
            static_cast<uint16_t>(inferno::Opcode::PING),
            "", kTestKey, counter);

        auto wire = pkt.serialize();
        if (wire.empty()) {
            std::fprintf(stderr, "[FAIL] test_malleable_header_only: "
                                 "serialize failed at counter %llu\n",
                         static_cast<unsigned long long>(counter));
            std::exit(1);
        }

        auto parsed = inferno::Packet::deserialize(wire, kTestKey, counter);
        if (!parsed.has_value()) {
            std::fprintf(stderr, "[FAIL] test_malleable_header_only: "
                                 "deserialize failed at counter %llu\n",
                         static_cast<unsigned long long>(counter));
            std::exit(1);
        }

        if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::PING)) {
            std::fprintf(stderr, "[FAIL] test_malleable_header_only: "
                                 "opcode mismatch at counter %llu\n",
                         static_cast<unsigned long long>(counter));
            std::exit(1);
        }
    }
    std::fprintf(stdout, "[PASS] test_malleable_header_only_roundtrip\n");
}

void test_malleable_all_variants_roundtrip() {
    inferno::CryptoContext::instance().initDefault();

    for (uint64_t counter = 0; counter < 30; ++counter) {
        std::string payload = "test_payload_" + std::to_string(counter);
        inferno::Packet pkt(
            static_cast<uint16_t>(inferno::Opcode::PING),
            payload, kTestKey, counter);

        auto wire = pkt.serialize();
        if (wire.empty()) {
            std::fprintf(stderr, "[FAIL] test_malleable_all_variants: "
                                 "serialize failed at counter %llu\n",
                         static_cast<unsigned long long>(counter));
            std::exit(1);
        }

        auto parsed = inferno::Packet::deserialize(wire, kTestKey, counter);
        if (!parsed.has_value()) {
            std::fprintf(stderr, "[FAIL] test_malleable_all_variants: "
                                 "deserialize failed at counter %llu\n",
                         static_cast<unsigned long long>(counter));
            std::exit(1);
        }

        if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::PING)) {
            std::fprintf(stderr, "[FAIL] test_malleable_all_variants: "
                                 "opcode mismatch at counter %llu\n",
                         static_cast<unsigned long long>(counter));
            std::exit(1);
        }

        auto parsed_payload = parsed->getPayload();
        std::string parsed_str(parsed_payload.begin(), parsed_payload.end());
        if (parsed_str != payload) {
            std::fprintf(stderr, "[FAIL] test_malleable_all_variants: "
                                 "payload mismatch at counter %llu\n",
                         static_cast<unsigned long long>(counter));
            std::exit(1);
        }
    }
    std::fprintf(stdout, "[PASS] test_malleable_all_variants_roundtrip\n");
}

void test_malleable_no_static_magic() {
    std::vector<uint32_t> first_words;
    for (uint64_t counter = 0; counter < 100; ++counter) {
        inferno::Packet pkt(
            static_cast<uint16_t>(inferno::Opcode::PING),
            "data", kTestKey, counter);
        auto wire = pkt.serialize();
        if (wire.size() < 4) {
            std::fprintf(stderr, "[FAIL] test_malleable_no_static_magic: "
                                 "wire too small\n");
            std::exit(1);
        }
        uint32_t word = (static_cast<uint32_t>(wire[0]) << 24) |
                        (static_cast<uint32_t>(wire[1]) << 16) |
                        (static_cast<uint32_t>(wire[2]) << 8)  |
                         static_cast<uint32_t>(wire[3]);
        first_words.push_back(word);
    }
    for (size_t i = 1; i < first_words.size(); ++i) {
        if (first_words[i] == first_words[i-1]) {
            std::fprintf(stderr, "[FAIL] test_malleable_no_static_magic: "
                                 "packet %zu has same first 4 bytes as %zu\n",
                         i, i - 1);
            std::exit(1);
        }
    }
    std::fprintf(stdout, "[PASS] test_malleable_no_static_magic\n");
}

void test_malleable_wrong_key_rejected() {
    inferno::CryptoContext::instance().initDefault();

    const uint8_t wrong_key[16] = {
        0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
        0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0
    };

    inferno::Packet pkt(
        static_cast<uint16_t>(inferno::Opcode::PING),
        "secret", kTestKey, 0);
    auto wire = pkt.serialize();
    auto parsed = inferno::Packet::deserialize(wire, wrong_key, 0);
    if (parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_malleable_wrong_key_rejected: "
                             "deserialized with wrong key\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_malleable_wrong_key_rejected\n");
}

void test_malleable_legacy_fallback() {
    inferno::Packet pkt(static_cast<uint16_t>(inferno::Opcode::PING),
                         "legacy_data");
    auto wire = pkt.serialize();
    auto parsed = inferno::Packet::deserialize(wire, nullptr, 0);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_malleable_legacy_fallback: "
                             "legacy deserialize failed\n");
        std::exit(1);
    }
    if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::PING)) {
        std::fprintf(stderr, "[FAIL] test_malleable_legacy_fallback: "
                             "opcode mismatch\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_malleable_legacy_fallback\n");
}

int main() {
    test_hmac_consistency();
    test_malleable_header_only_roundtrip();
    test_malleable_all_variants_roundtrip();
    test_malleable_no_static_magic();
    test_malleable_wrong_key_rejected();
    test_malleable_legacy_fallback();
    std::fprintf(stdout, "[PASS] All malleable packet tests passed\n");
    return 0;
}
