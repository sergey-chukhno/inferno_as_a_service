#include "../include/ScreenCapture.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dlfcn.h>

namespace inferno { namespace capture {

CaptureResult captureScreen(uint32_t max_width, uint32_t max_height,
                             bool grayscale) {
    (void)max_width;
    (void)max_height;
    (void)grayscale;

    CaptureResult result;

    @autoreleasepool {
        // Dynamically resolve CGDisplayCreateImage (obsoleted in macOS 15
        // but still available at runtime on all versions)
        typedef CGImageRef (*CGDisplayCreateImageFunc)(CGDirectDisplayID);
        static CGDisplayCreateImageFunc func = nullptr;
        static dispatch_once_t once;
        dispatch_once(&once, ^{
            func = (CGDisplayCreateImageFunc)dlsym(
                RTLD_DEFAULT, "CGDisplayCreateImage");
        });

        if (!func) {
            result.error_msg = "CGDisplayCreateImage not available";
            return result;
        }

        CGImageRef image = func(kCGDirectMainDisplay);
        if (!image) {
            result.error_msg = "CGDisplayCreateImage failed";
            return result;
        }

        size_t w = CGImageGetWidth(image);
        size_t h = CGImageGetHeight(image);

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithCGImage:image];
        NSDictionary* props = @{
            NSImageCompressionFactor: @0.85
        };
        NSData* jpeg = [rep representationUsingType:NSBitmapImageFileTypeJPEG
                                         properties:props];

        if (!jpeg || jpeg.length == 0) {
            CGImageRelease(image);
            result.error_msg = "JPEG encoding failed";
            return result;
        }

        result.jpeg_data.resize(jpeg.length);
        std::memcpy(result.jpeg_data.data(), jpeg.bytes, jpeg.length);
        result.success = true;
        result.width = static_cast<uint32_t>(w);
        result.height = static_cast<uint32_t>(h);

        CGImageRelease(image);
    }

    return result;
}

// Not applicable on macOS — JPEG via NSBitmapImageRep
std::vector<uint8_t> encodeJpeg(const uint8_t*, int, int,
                                 int, bool) {
    return {};
}

}} // namespace inferno::capture

#endif // __APPLE__
