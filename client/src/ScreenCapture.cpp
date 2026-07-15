#include "../include/ScreenCapture.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>      // IStream for Gdiplus
#include <gdiplus.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#ifndef IID_PPV_ARGS
#define IID_PPV_ARGS(ppType) __uuidof(**(ppType)), reinterpret_cast<void**>(static_cast<ppType**>(ppType))
#endif

#include <cstdio>
#include <cstring>

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

// ── DXGI Desktop Duplication ───────────────────────────────────

static bool captureViaDxgi(std::vector<uint8_t>& pixels,
                            int& out_w, int& out_h) {
    HMODULE d3d11 = ::LoadLibraryA("d3d11.dll");
    HMODULE dxgi = ::LoadLibraryA("dxgi.dll");
    if (!d3d11 || !dxgi) return false;

    auto pD3D11CreateDevice =
        reinterpret_cast<HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE,
                                          HMODULE, UINT,
                                          const D3D_FEATURE_LEVEL*, UINT,
                                          UINT, ID3D11Device**,
                                          D3D_FEATURE_LEVEL*,
                                          ID3D11DeviceContext**)>(
            ::GetProcAddress(d3d11, "D3D11CreateDevice"));
    auto pCreateDXGIFactory1 =
        reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(
            ::GetProcAddress(dxgi, "CreateDXGIFactory1"));

    if (!pD3D11CreateDevice || !pCreateDXGIFactory1) return false;

    IDXGIFactory1* factory = nullptr;
    if (FAILED(pCreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    IDXGIAdapter1* adapter = nullptr;
    if (FAILED(factory->EnumAdapters1(0, &adapter))) {
        factory->Release();
        return false;
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    if (FAILED(pD3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN,
                                   nullptr, 0, levels, 3,
                                   D3D11_SDK_VERSION, &device,
                                   nullptr, &context))) {
        adapter->Release();
        factory->Release();
        return false;
    }

    IDXGIOutput* output = nullptr;
    if (FAILED(adapter->EnumOutputs(0, &output))) {
        context->Release(); device->Release();
        adapter->Release(); factory->Release();
        return false;
    }

    IDXGIOutput1* output1 = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output1))) ||
        FAILED(output1->DuplicateOutput(device, &duplication))) {
        if (output1) output1->Release();
        output->Release();
        context->Release(); device->Release();
        adapter->Release(); factory->Release();
        return false;
    }
    output1->Release();
    output->Release();

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    IDXGIResource* desktopResource = nullptr;
    HRESULT hr = duplication->AcquireNextFrame(100, &frameInfo,
                                                &desktopResource);
    if (FAILED(hr) || !desktopResource) {
        duplication->Release();
        context->Release(); device->Release();
        adapter->Release(); factory->Release();
        return false;
    }

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(desktopResource->QueryInterface(IID_PPV_ARGS(&texture)))) {
        desktopResource->Release();
        duplication->ReleaseFrame();
        duplication->Release();
        context->Release(); device->Release();
        adapter->Release(); factory->Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
        texture->Release(); desktopResource->Release();
        duplication->ReleaseFrame(); duplication->Release();
        context->Release(); device->Release();
        adapter->Release(); factory->Release();
        return false;
    }

    context->CopyResource(staging, texture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        out_w = static_cast<int>(desc.Width);
        out_h = static_cast<int>(desc.Height);
        pixels.resize(static_cast<size_t>(out_w) * out_h * 4);
        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
        for (int y = 0; y < out_h; ++y) {
            std::memcpy(&pixels[static_cast<size_t>(y) * out_w * 4],
                        src + static_cast<size_t>(y) * mapped.RowPitch,
                        static_cast<size_t>(out_w) * 4);
        }
        context->Unmap(staging, 0);
    }

    staging->Release(); texture->Release();
    desktopResource->Release();
    duplication->ReleaseFrame(); duplication->Release();
    context->Release(); device->Release();
    adapter->Release(); factory->Release();

    return !pixels.empty();
}

// ── BitBlt fallback ────────────────────────────────────────────

static bool captureViaBitBlt(std::vector<uint8_t>& pixels,
                              int& out_w, int& out_h) {
    out_w = ::GetSystemMetrics(SM_CXSCREEN);
    out_h = ::GetSystemMetrics(SM_CYSCREEN);
    if (out_w <= 0 || out_h <= 0) return false;

    pixels.resize(static_cast<size_t>(out_w) * out_h * 4);

    HDC hdcScreen = ::GetDC(nullptr);
    if (!hdcScreen) return false;

    HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = ::CreateCompatibleBitmap(hdcScreen, out_w, out_h);
    if (!hBitmap) {
        ::DeleteDC(hdcMem);
        ::ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    HGDIOBJ oldBmp = ::SelectObject(hdcMem, hBitmap);
    ::BitBlt(hdcMem, 0, 0, out_w, out_h, hdcScreen, 0, 0, SRCCOPY);

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = out_w;
    bmi.bmiHeader.biHeight = -out_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    ::GetDIBits(hdcMem, hBitmap, 0, out_h, pixels.data(), &bmi,
                DIB_RGB_COLORS);

    ::SelectObject(hdcMem, oldBmp);
    ::DeleteObject(hBitmap);
    ::DeleteDC(hdcMem);
    ::ReleaseDC(nullptr, hdcScreen);
    return true;
}

} // anonymous namespace

// ── Public API ─────────────────────────────────────────────────

std::vector<uint8_t> encodeJpeg(const uint8_t* pixels,
                                 int width, int height,
                                 int quality, bool grayscale) {
    if (!pixels || width <= 0 || height <= 0) return {};

    size_t stride = ((static_cast<size_t>(width) * 3 + 3) / 4) * 4;
    size_t rgb_size = stride * static_cast<size_t>(height);
    std::vector<uint8_t> rgb(rgb_size);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t src_idx = (static_cast<size_t>(y) * width + x) * 4;
            size_t dst_idx = static_cast<size_t>(y) * stride + x * 3;
            if (grayscale) {
                uint8_t g = static_cast<uint8_t>(
                    0.299f * pixels[src_idx + 2] +
                    0.587f * pixels[src_idx + 1] +
                    0.114f * pixels[src_idx] + 0.5f);
                rgb[dst_idx]     = g;
                rgb[dst_idx + 1] = g;
                rgb[dst_idx + 2] = g;
            } else {
                rgb[dst_idx]     = pixels[src_idx + 2];
                rgb[dst_idx + 1] = pixels[src_idx + 1];
                rgb[dst_idx + 2] = pixels[src_idx];
            }
        }
    }

    if (!ensureGdiplus()) return {};
    Gdiplus::Bitmap bmp(width, height, static_cast<int>(stride),
                        PixelFormat24bppRGB, rgb.data());
    CLSID clsid;
    if (getEncoderClsid(L"image/jpeg", &clsid) < 0) return {};

    Gdiplus::EncoderParameters eps;
    eps.Count = 1;
    eps.Parameter[0].Guid = Gdiplus::EncoderQuality;
    eps.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    eps.Parameter[0].NumberOfValues = 1;
    eps.Parameter[0].Value = &quality;

    IStream* stream = nullptr;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK)
        return {};

    std::vector<uint8_t> result;
    if (bmp.Save(stream, &clsid, &eps) == Gdiplus::Ok) {
        STATSTG stat;
        if (stream->Stat(&stat, STATFLAG_NONAME) == S_OK) {
            result.resize(static_cast<size_t>(stat.cbSize.QuadPart));
            LARGE_INTEGER zero = {};
            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
            ULONG read = 0;
            stream->Read(result.data(),
                         static_cast<ULONG>(result.size()), &read);
        }
    }
    stream->Release();
    return result;
}

CaptureResult captureScreen(uint32_t max_width, uint32_t max_height,
                             bool grayscale) {
    CaptureResult result;

    // Skip capture if user >5 min idle.
    // Both lii.dwTime and GetTickCount() are DWORD (32-bit).
    // Unsigned DWORD wrapping is well-defined — the subtraction
    // yields the correct elapsed time even across a 49.7-day wrap.
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(lii);
    if (::GetLastInputInfo(&lii)) {
        DWORD now = ::GetTickCount();
        if (now - lii.dwTime > 300000) {
            result.error_msg = "user idle >5min, skipping capture";
            return result;
        }
    }

    // Capture raw BGRA pixels — try DXGI first, fall back to BitBlt
    int screen_w = 0, screen_h = 0;
    std::vector<uint8_t> pixels;
    if (!captureViaDxgi(pixels, screen_w, screen_h)) {
        if (!captureViaBitBlt(pixels, screen_w, screen_h)) {
            result.error_msg = "both DXGI and BitBlt capture failed";
            return result;
        }
    }

    // Compute output dimensions (downscale if needed)
    uint32_t out_w = static_cast<uint32_t>(screen_w);
    uint32_t out_h = static_cast<uint32_t>(screen_h);
    if (max_width > 0 && max_height > 0 &&
        (out_w > max_width || out_h > max_height)) {
        float scale = std::min(static_cast<float>(max_width) /
                               static_cast<float>(out_w),
                               static_cast<float>(max_height) /
                               static_cast<float>(out_h));
        out_w = static_cast<uint32_t>(static_cast<float>(out_w) * scale);
        out_h = static_cast<uint32_t>(static_cast<float>(out_h) * scale);
        if (out_w < 1) out_w = 1;
        if (out_h < 1) out_h = 1;
    }

    // Downscale if needed (nearest-neighbor)
    std::vector<uint8_t> final_pixels;
    if (out_w == static_cast<uint32_t>(screen_w) &&
        out_h == static_cast<uint32_t>(screen_h)) {
        final_pixels = std::move(pixels);
    } else {
        final_pixels.resize(static_cast<size_t>(out_w) * out_h * 4);
        for (uint32_t y = 0; y < out_h; ++y) {
            uint32_t src_y = std::min(
                static_cast<uint32_t>(static_cast<float>(y) / out_h *
                                      screen_h),
                static_cast<uint32_t>(screen_h - 1));
            for (uint32_t x = 0; x < out_w; ++x) {
                uint32_t src_x = std::min(
                    static_cast<uint32_t>(static_cast<float>(x) / out_w *
                                          screen_w),
                    static_cast<uint32_t>(screen_w - 1));
                size_t src_off = (static_cast<size_t>(src_y) * screen_w +
                                  src_x) * 4;
                size_t dst_off = (static_cast<size_t>(y) * out_w + x) * 4;
                for (int c = 0; c < 4; ++c)
                    final_pixels[dst_off + c] = pixels[src_off + c];
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

#elif !defined(__APPLE__) // Linux stub

namespace inferno { namespace capture {

CaptureResult captureScreen(uint32_t, uint32_t, bool) {
    CaptureResult result;
    result.error_msg = "screenshot not supported on this platform";
    return result;
}

std::vector<uint8_t> encodeJpeg(const uint8_t*, int, int, int, bool) {
    return {};
}

}} // namespace inferno::capture

#endif
