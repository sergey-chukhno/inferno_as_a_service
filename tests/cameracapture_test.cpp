#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#ifdef _WIN32

#include "../common/include/Opcodes.hpp"
#include "../common/include/Packet.hpp"
#include "../client/include/CameraCapture.hpp"

// ── Camera Capture Tests ────────────────────────────────────────
// The actual camera capture requires a physical camera device and
// cannot run in CI. These tests validate the format conversion and
// protocol integration (packet round-trip).

void test_nv12_to_bgra_gray() {
    // 4x2 NV12: all Y=128, U=V=128 (neutral gray)
    const uint8_t y_plane[] = { 128, 128, 128, 128,
                                128, 128, 128, 128 };
    const uint8_t uv_plane[] = { 128, 128, 128, 128 };

    std::vector<uint8_t> bgra;
    inferno::capture::nv12ToBgra(y_plane, uv_plane, 4, 2, 4, 4, bgra);

    if (bgra.size() != 4 * 2 * 4) {
        std::fprintf(stderr, "[FAIL] test_nv12_to_bgra_gray: "
                             "expected %zu bytes, got %zu\n",
                     4 * 2 * 4, bgra.size());
        std::exit(1);
    }

    // All pixels should be neutral gray (~128)
    for (size_t i = 0; i < bgra.size(); i += 4) {
        // BGRA format: B at i, G at i+1, R at i+2, A at i+3
        if (bgra[i+3] != 255) {
            std::fprintf(stderr, "[FAIL] test_nv12_to_bgra_gray: "
                                 "alpha channel not 255 at pixel %zu\n", i/4);
            std::exit(1);
        }
        // With Y=128, U=V=128, R=G=B should all be close to 128
        // (small rounding differences possible with float math)
        int avg = (static_cast<int>(bgra[i]) + bgra[i+1] + bgra[i+2]) / 3;
        if (avg < 126 || avg > 130) {
            std::fprintf(stderr, "[FAIL] test_nv12_to_bgra_gray: "
                                 "pixel %zu avg=%d (expected ~128)\n", i/4, avg);
            std::exit(1);
        }
    }
    std::fprintf(stdout, "[PASS] test_nv12_to_bgra_gray\n");
}

void test_nv12_to_bgra_red() {
    // NV12 encoding of red: Y≈76, V≈240 (R-Y), U≈128
    // Known values for Rec.601 red: Y=76, U=128, V=240
    const uint8_t y_plane[] = { 76, 76, 76, 76,
                                76, 76, 76, 76 };
    const uint8_t uv_plane[] = { 128, 240, 128, 240 };

    std::vector<uint8_t> bgra;
    inferno::capture::nv12ToBgra(y_plane, uv_plane, 4, 2, 4, 4, bgra);

    if (bgra.size() < 4) {
        std::fprintf(stderr, "[FAIL] test_nv12_to_bgra_red: "
                             "output too small (%zu)\n", bgra.size());
        std::exit(1);
    }

    // Red pixel: R channel should be significantly higher than B and G
    uint8_t r = bgra[2]; // R at offset 2
    uint8_t g = bgra[1]; // G at offset 1
    uint8_t b = bgra[0]; // B at offset 0
    if (r <= g || r <= b) {
        std::fprintf(stderr, "[FAIL] test_nv12_to_bgra_red: "
                             "R(%u) not dominant over G(%u)/B(%u)\n",
                     r, g, b);
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_nv12_to_bgra_red "
                         "(R=%u, G=%u, B=%u)\n", r, g, b);
}

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
