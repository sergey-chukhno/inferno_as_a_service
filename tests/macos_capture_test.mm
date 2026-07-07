#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dlfcn.h>

// ── macOS Screenshot Capture Tests ─────────────────────────────
// Tests the CGImage→JPEG pipeline without requiring a real display.

void test_cgimage_to_jpeg() {
    @autoreleasepool {
        // Create a small red bitmap context (8x8, 32bpp)
        size_t w = 8, h = 8;
        size_t bpr = w * 4;
        std::vector<uint8_t> pixels(bpr * h, 0);
        // Fill with pure red (BGRA: B=0, G=0, R=255, A=255)
        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) {
                size_t off = y * bpr + x * 4;
                pixels[off]     = 0;   // B
                pixels[off + 1] = 0;   // G
                pixels[off + 2] = 255; // R
                pixels[off + 3] = 255; // A
            }
        }

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(
            pixels.data(), w, h, 8, bpr, cs,
            kCGImageAlphaPremultipliedFirst);
        CGColorSpaceRelease(cs);

        if (!ctx) {
            std::fprintf(stderr, "[FAIL] test_cgimage_to_jpeg: "
                                 "CGBitmapContextCreate failed\n");
            std::exit(1);
        }

        CGImageRef cgImage = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);

        if (!cgImage) {
            std::fprintf(stderr, "[FAIL] test_cgimage_to_jpeg: "
                                 "CGBitmapContextCreateImage failed\n");
            std::exit(1);
        }

        // Encode to JPEG using same path as captureScreen()
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithCGImage:cgImage];
        NSDictionary* props = @{ NSImageCompressionFactor: @0.85 };
        NSData* jpeg = [rep representationUsingType:NSBitmapImageFileTypeJPEG
                                         properties:props];

        CGImageRelease(cgImage);

        if (!jpeg || jpeg.length == 0) {
            std::fprintf(stderr, "[FAIL] test_cgimage_to_jpeg: "
                                 "JPEG encoding returned empty\n");
            std::exit(1);
        }

        // Verify JPEG SOI marker (0xFF 0xD8)
        const uint8_t* bytes = static_cast<const uint8_t*>(jpeg.bytes);
        if (jpeg.length < 2 || bytes[0] != 0xFF || bytes[1] != 0xD8) {
            std::fprintf(stderr, "[FAIL] test_cgimage_to_jpeg: "
                                 "no JPEG SOI marker (0x%02x%02x)\n",
                         bytes[0], bytes[1]);
            std::exit(1);
        }

        std::fprintf(stdout, "[PASS] test_cgimage_to_jpeg (%zu bytes)\n",
                     static_cast<size_t>(jpeg.length));
    }
}

void test_cgdisplay_create_image_resolved() {
    // Verify CGDisplayCreateImage can be dynamically resolved
    typedef CGImageRef (*CGDisplayCreateImageFunc)(CGDirectDisplayID);
    CGDisplayCreateImageFunc func =
        (CGDisplayCreateImageFunc)dlsym(RTLD_DEFAULT,
                                         "CGDisplayCreateImage");
    if (!func) {
        std::fprintf(stderr, "[FAIL] test_cgdisplay_create_image_resolved: "
                             "dlsym failed\n");
        std::exit(1);
    }
    std::fprintf(stdout, "[PASS] test_cgdisplay_create_image_resolved\n");
}

#endif // __APPLE__

int main() {
#ifdef __APPLE__
    test_cgimage_to_jpeg();
    test_cgdisplay_create_image_resolved();
    std::fprintf(stdout, "[PASS] All macOS capture tests passed\n");
#else
    std::fprintf(stdout, "[SKIP] macOS capture tests are Apple-only\n");
#endif
    return 0;
}
