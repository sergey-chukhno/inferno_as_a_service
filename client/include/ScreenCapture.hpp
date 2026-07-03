#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace inferno { namespace capture {

struct CaptureResult {
    bool success = false;
    std::vector<uint8_t> jpeg_data;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string error_msg;
};

// Capture the primary display as a JPEG in memory.
// Primary: IDXGIOutputDuplication (D3D, GPU, multi-monitor)
// Fallback: BitBlt (GDI, CPU, single-monitor)
CaptureResult captureScreen(uint32_t max_width = 1280,
                            uint32_t max_height = 720,
                            bool grayscale = false);

// Encode raw BGRA pixels to JPEG via Gdiplus.
// Caller must GdiplusStartup before first call (done internally).
std::vector<uint8_t> encodeJpeg(const uint8_t* pixels,
                                int width, int height,
                                int quality = 85,
                                bool grayscale = false);

}} // namespace inferno::capture
