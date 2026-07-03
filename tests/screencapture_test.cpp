#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#ifdef _WIN32

#include "../client/include/ScreenCapture.hpp"
#include "../common/include/Opcodes.hpp"
#include "../common/include/Packet.hpp"

// ── ScreenCapture Unit Tests ────────────────────────────────────

void test_encode_jpeg_rgb() {
    // Create a small 2x2 RGB pattern
    std::vector<uint8_t> pixels(2 * 2 * 4, 0);
    // Red pixel (BGRA)
    pixels[0] = 0;   // B
    pixels[1] = 0;   // G
    pixels[2] = 255; // R
    pixels[3] = 255; // A
    // Green pixel
    pixels[4] = 0; pixels[5] = 255; pixels[6] = 0; pixels[7] = 255;
    // Blue pixel
    pixels[8] = 255; pixels[9] = 0; pixels[10] = 0; pixels[11] = 255;
    // White pixel
    pixels[12] = 255; pixels[13] = 255; pixels[14] = 255; pixels[15] = 255;

    auto jpeg = inferno::capture::encodeJpeg(pixels.data(), 2, 2, 85, false);
    if (jpeg.empty()) {
        std::fprintf(stderr, "[FAIL] test_encode_jpeg_rgb: empty output\n");
        std::exit(1);
    }
    // JPEG files start with 0xFF 0xD8 (SOI marker)
    if (jpeg.size() < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
        std::fprintf(stderr, "[FAIL] test_encode_jpeg_rgb: "
                             "no JPEG SOI marker (0x%02x%02x)\n",
                     jpeg.size() >= 1 ? jpeg[0] : 0,
                     jpeg.size() >= 2 ? jpeg[1] : 0);
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_encode_jpeg_rgb (%zu bytes)\n", jpeg.size());
}

void test_encode_jpeg_grayscale_smaller() {
    // Use a red/green checkerboard — in color, each tile has different RGB;
    // in grayscale they collapse to similar values, reducing entropy.
    std::vector<uint8_t> pixels(32 * 32 * 4, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            size_t off = (static_cast<size_t>(y) * 32 + x) * 4;
            if ((x / 4 + y / 4) % 2 == 0) {
                pixels[off] = 0; pixels[off+1] = 0;       // B=0, G=0
                pixels[off+2] = 255; pixels[off+3] = 255; // R=255, A=255
            } else {
                pixels[off] = 0; pixels[off+1] = 255;     // B=0, G=255
                pixels[off+2] = 0; pixels[off+3] = 255;   // R=0, A=255
            }
        }
    }

    auto color = inferno::capture::encodeJpeg(pixels.data(), 32, 32, 85, false);
    auto gray  = inferno::capture::encodeJpeg(pixels.data(), 32, 32, 85, true);

    if (color.empty() || gray.empty()) {
        std::fprintf(stderr, "[FAIL] test_encode_jpeg_grayscale_smaller: "
                             "encode failed (color=%zu, gray=%zu)\n",
                     color.size(), gray.size());
        std::exit(1);
    }
    if (gray.size() >= color.size()) {
        std::fprintf(stderr, "[FAIL] test_encode_jpeg_grayscale_smaller: "
                             "grayscale (%zu) not smaller than color (%zu)\n",
                     gray.size(), color.size());
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_encode_jpeg_grayscale_smaller "
                         "(color=%zu, gray=%zu)\n", color.size(), gray.size());
}

void test_screenshot_packet_roundtrip() {
    // Verify SCREENSHOT_REQ serializes correctly
    inferno::Packet req(static_cast<uint16_t>(inferno::Opcode::SCREENSHOT_REQ), "");
    auto req_data = req.serialize();
    auto parsed_req = inferno::Packet::deserialize(req_data);
    if (!parsed_req.has_value()) {
        std::fprintf(stderr, "[FAIL] test_screenshot_packet_roundtrip: "
                             "SCREENSHOT_REQ deserialize failed\n");
        std::exit(1);
    }
    if (parsed_req->getOpcode() != static_cast<uint16_t>(inferno::Opcode::SCREENSHOT_REQ)) {
        std::fprintf(stderr, "[FAIL] test_screenshot_packet_roundtrip: "
                             "SCREENSHOT_REQ opcode mismatch\n");
        std::exit(1);
    }

    // Build a fake SCREENSHOT_RES payload
    std::vector<uint8_t> payload;
    // status = 0 (success)
    payload.push_back(0); payload.push_back(0);
    // width = 1920
    payload.push_back(0); payload.push_back(0); payload.push_back(7); payload.push_back(128);
    // height = 1080
    payload.push_back(0); payload.push_back(0); payload.push_back(4); payload.push_back(56);
    // size = 4
    payload.push_back(0); payload.push_back(0); payload.push_back(0); payload.push_back(4);
    // data = 0xDEADBEEF
    payload.push_back(0xDE); payload.push_back(0xAD); payload.push_back(0xBE); payload.push_back(0xEF);

    std::string payload_str(payload.begin(), payload.end());
    inferno::Packet res(static_cast<uint16_t>(inferno::Opcode::SCREENSHOT_RES), payload_str);
    auto res_data = res.serialize();
    auto parsed_res = inferno::Packet::deserialize(res_data);
    if (!parsed_res.has_value()) {
        std::fprintf(stderr, "[FAIL] test_screenshot_packet_roundtrip: "
                             "SCREENSHOT_RES deserialize failed\n");
        std::exit(1);
    }
    if (parsed_res->getOpcode() != static_cast<uint16_t>(inferno::Opcode::SCREENSHOT_RES)) {
        std::fprintf(stderr, "[FAIL] test_screenshot_packet_roundtrip: "
                             "SCREENSHOT_RES opcode mismatch\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_screenshot_packet_roundtrip\n");
}

#endif // _WIN32
