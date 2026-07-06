#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#ifdef _WIN32

#include "../common/include/Opcodes.hpp"
#include "../common/include/Packet.hpp"

// ── Camera Capture Tests ────────────────────────────────────────
// The actual camera capture requires a physical camera device and
// cannot run in CI. These tests validate the protocol integration
// only (packet round-trip).

void test_camera_packet_roundtrip() {
    // Build a fake SCREENSHOT_REQ with subtype=2 (camera)
    std::string subtype_payload(1, static_cast<char>(2));
    inferno::Packet req(
        static_cast<uint16_t>(inferno::Opcode::SCREENSHOT_REQ),
        subtype_payload);
    auto req_data = req.serialize();
    auto parsed_req = inferno::Packet::deserialize(req_data);
    if (!parsed_req.has_value()) {
        std::fprintf(stderr, "[FAIL] test_camera_packet_roundtrip: "
                             "SCREENSHOT_REQ deserialize failed\n");
        std::exit(1);
    }
    auto& parsed_payload = parsed_req->getPayload();
    if (parsed_payload.empty() || parsed_payload[0] != 2) {
        std::fprintf(stderr, "[FAIL] test_camera_packet_roundtrip: "
                             "subtype byte not preserved\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_camera_packet_roundtrip\n");
}

void test_camera_packet_subtype_screenshot() {
    // Verify subtype=1 is correctly passed as screenshot
    std::string subtype_payload(1, static_cast<char>(1));
    inferno::Packet req(
        static_cast<uint16_t>(inferno::Opcode::SCREENSHOT_REQ),
        subtype_payload);
    auto data = req.serialize();
    auto parsed = inferno::Packet::deserialize(data);
    if (!parsed.has_value() || parsed->getPayload().empty() ||
        parsed->getPayload()[0] != 1) {
        std::fprintf(stderr, "[FAIL] test_camera_packet_subtype_screenshot: "
                             "screenshot subtype not preserved\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_camera_packet_subtype_screenshot\n");
}

#endif // _WIN32
