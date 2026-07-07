#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "../common/include/Opcodes.hpp"
#include "../common/include/Packet.hpp"

// ── TCC Grant Packet Tests ─────────────────────────────────────
// Verifies TCC_GRANT / TCC_GRANT_RES opcodes serialize correctly.

void test_tcc_grant_packet_roundtrip() {
    std::string bundleId = "com.dbeaver.DBeaver";
    inferno::Packet req(
        static_cast<uint16_t>(inferno::Opcode::TCC_GRANT), bundleId);
    auto data = req.serialize();
    auto parsed = inferno::Packet::deserialize(data);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_tcc_grant_packet_roundtrip: "
                             "TCC_GRANT deserialize failed\n");
        std::exit(1);
    }
    if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::TCC_GRANT)) {
        std::fprintf(stderr, "[FAIL] test_tcc_grant_packet_roundtrip: "
                             "opcode mismatch\n");
        std::exit(1);
    }
    auto payload = parsed->getPayload();
    std::string parsedBundle(payload.begin(), payload.end());
    if (parsedBundle != bundleId) {
        std::fprintf(stderr, "[FAIL] test_tcc_grant_packet_roundtrip: "
                             "bundleId mismatch: '%s'\n",
                     parsedBundle.c_str());
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_tcc_grant_packet_roundtrip\n");
}

void test_tcc_grant_res_packet_roundtrip() {
    std::string payload = "com.dbeaver.DBeaver|1";
    inferno::Packet res(
        static_cast<uint16_t>(inferno::Opcode::TCC_GRANT_RES), payload);
    auto data = res.serialize();
    auto parsed = inferno::Packet::deserialize(data);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "[FAIL] test_tcc_grant_res_packet_roundtrip: "
                             "TCC_GRANT_RES deserialize failed\n");
        std::exit(1);
    }
    if (parsed->getOpcode() != static_cast<uint16_t>(inferno::Opcode::TCC_GRANT_RES)) {
        std::fprintf(stderr, "[FAIL] test_tcc_grant_res_packet_roundtrip: "
                             "opcode mismatch\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_tcc_grant_res_packet_roundtrip\n");
}
