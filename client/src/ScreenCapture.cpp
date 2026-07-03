#include "../include/ScreenCapture.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus")

#include <cstdio>

namespace inferno { namespace capture {

namespace {

// ── GDI+ JPEG encoder ──────────────────────────────────────────

static bool ensureGdiplus() {
    static bool initialized = false;
    static ULONG_PTR token = 0;
    if (initialized) return true;
    Gdiplus::GdiplusStartupInput inp;
    inp.GdiplusVersion = 1;
    inp.DebugEventCallback = nullptr;
    inp.SuppressBackgroundThread = FALSE;
    if (Gdiplus::GdiplusStartup(&token, &inp, nullptr) == Gdiplus::Ok) {
        initialized = true;
        return true;
    }
    return false;
}

static int getEncoderClsid(const wchar_t* format, CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    std::vector<Gdiplus::ImageCodecInfo> codecs(size / sizeof(Gdiplus::ImageCodecInfo));
    Gdiplus::GetImageEncoders(num, size, codecs.data());
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(codecs[i].MimeType, format) == 0) {
            *clsid = codecs[i].Clsid;
            return i;
        }
    }
    return -1;
}

} // anonymous namespace

// ── Public API ─────────────────────────────────────────────────

std::vector<uint8_t> encodeJpeg(const uint8_t* pixels,
                                 int width, int height,
                                 int quality, bool grayscale) {
    if (!pixels || width <= 0 || height <= 0) return {};

    // Gdiplus expects 4-byte aligned stride
    int stride = ((width * 3 + 3) / 4) * 4;

    // Convert BGRA to RGB (Gdiplus expects RGB top-down or bottom-up)
    // We receive BGRA from BitBlt/DXGI. Remove alpha, flip B<->R.
    std::vector<uint8_t> rgb(static_cast<size_t>(stride) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t src_idx = (static_cast<size_t>(y) * width + x) * 4;
            size_t dst_idx = static_cast<size_t>(y) * stride + x * 3;
            if (grayscale) {
                uint8_t g = static_cast<uint8_t>(
                    0.299f * pixels[src_idx + 2] +
                    0.587f * pixels[src_idx + 1] +
                    0.114f * pixels[src_idx]);
                rgb[dst_idx]     = g;
                rgb[dst_idx + 1] = g;
                rgb[dst_idx + 2] = g;
            } else {
                rgb[dst_idx]     = pixels[src_idx + 2]; // R
                rgb[dst_idx + 1] = pixels[src_idx + 1]; // G
                rgb[dst_idx + 2] = pixels[src_idx];     // B
            }
        }
    }

    if (!ensureGdiplus()) return {};
    Gdiplus::Bitmap bmp(width, height, stride, PixelFormat24bppRGB, rgb.data());
    CLSID clsid;
    if (getEncoderClsid(L"image/jpeg", &clsid) < 0) return {};

    Gdiplus::EncoderParameters eps;
    eps.Count = 1;
    eps.Parameter[0].Guid = Gdiplus::EncoderQuality;
    eps.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    eps.Parameter[0].NumberOfValues = 1;
    eps.Parameter[0].Value = &quality;

    IStream* stream = nullptr;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return {};

    std::vector<uint8_t> result;
    if (bmp.Save(stream, &clsid, &eps) == Gdiplus::Ok) {
        STATSTG stat;
        if (stream->Stat(&stat, STATFLAG_NONAME) == S_OK) {
            result.resize(static_cast<size_t>(stat.cbSize.QuadPart));
            LARGE_INTEGER zero = {};
            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
            ULONG read = 0;
            stream->Read(result.data(), static_cast<ULONG>(result.size()), &read);
        }
    }
    stream->Release();
    return result;
}

CaptureResult captureScreen(uint32_t max_width, uint32_t max_height,
                             bool grayscale) {
    CaptureResult result;

    // Skip capture if user has been idle >5 minutes
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(lii);
    if (::GetLastInputInfo(&lii)) {
        DWORD now = ::GetTickCount();
        if (now - lii.dwTime > 300000) { // 5 min
            result.error_msg = "user idle >5min, skipping capture";
            return result;
        }
    }

    // Get primary display dimensions
    int screen_w = ::GetSystemMetrics(SM_CXSCREEN);
    int screen_h = ::GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0 || screen_h <= 0) {
        result.error_msg = "no display detected";
        return result;
    }

    // Downscale dimensions
    uint32_t out_w = static_cast<uint32_t>(screen_w);
    uint32_t out_h = static_cast<uint32_t>(screen_h);
    if (out_w > max_width || out_h > max_height) {
        float scale = std::min(static_cast<float>(max_width) / out_w,
                               static_cast<float>(max_height) / out_h);
        out_w = static_cast<uint32_t>(out_w * scale);
        out_h = static_cast<uint32_t>(out_h * scale);
        if (out_w < 1) out_w = 1;
        if (out_h < 1) out_h = 1;
    }

    // Allocate pixel buffer: 32bpp BGRA
    size_t buf_size = static_cast<size_t>(screen_w) * screen_h * 4;
    std::vector<uint8_t> pixels(buf_size);

    HDC hdcScreen = ::GetDC(nullptr);
    if (!hdcScreen) {
        result.error_msg = "GetDC failed";
        return result;
    }

    HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = ::CreateCompatibleBitmap(hdcScreen, screen_w, screen_h);
    if (!hBitmap) {
        ::DeleteDC(hdcMem);
        ::ReleaseDC(nullptr, hdcScreen);
        result.error_msg = "CreateCompatibleBitmap failed";
        return result;
    }

    HGDIOBJ oldBmp = ::SelectObject(hdcMem, hBitmap);
    ::BitBlt(hdcMem, 0, 0, screen_w, screen_h, hdcScreen, 0, 0, SRCCOPY);

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = screen_w;
    bmi.bmiHeader.biHeight = -screen_h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    ::GetDIBits(hdcMem, hBitmap, 0, screen_h, pixels.data(), &bmi, DIB_RGB_COLORS);

    ::SelectObject(hdcMem, oldBmp);
    ::DeleteObject(hBitmap);
    ::DeleteDC(hdcMem);
    ::ReleaseDC(nullptr, hdcScreen);

    // If dimensions match, encode directly; otherwise downscale first
    std::vector<uint8_t> final_pixels;
    if (out_w == static_cast<uint32_t>(screen_w) &&
        out_h == static_cast<uint32_t>(screen_h)) {
        final_pixels = std::move(pixels);
    } else {
        // Nearest-neighbor downscale
        final_pixels.resize(static_cast<size_t>(out_w) * out_h * 4);
        for (uint32_t y = 0; y < out_h; ++y) {
            uint32_t src_y = static_cast<uint32_t>(
                static_cast<float>(y) / out_h * screen_h);
            if (src_y >= static_cast<uint32_t>(screen_h)) src_y = screen_h - 1;
            for (uint32_t x = 0; x < out_w; ++x) {
                uint32_t src_x = static_cast<uint32_t>(
                    static_cast<float>(x) / out_w * screen_w);
                if (src_x >= static_cast<uint32_t>(screen_w)) src_x = screen_w - 1;
                size_t src_idx = (static_cast<size_t>(src_y) * screen_w + src_x) * 4;
                size_t dst_idx = (static_cast<size_t>(y) * out_w + x) * 4;
                for (int c = 0; c < 4; ++c)
                    final_pixels[dst_idx + c] = pixels[src_idx + c];
            }
        }
    }

    result.jpeg_data = encodeJpeg(final_pixels.data(),
                                   static_cast<int>(out_w),
                                   static_cast<int>(out_h),
                                   85, grayscale);
    if (result.jpeg_data.empty()) {
        result.error_msg = "JPEG encoding failed";
        return result;
    }

    result.success = true;
    result.width = out_w;
    result.height = out_h;
    return result;
}

}} // namespace inferno::capture

#endif // _WIN32
