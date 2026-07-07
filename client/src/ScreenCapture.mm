#include "../include/ScreenCapture.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dlfcn.h>

namespace inferno { namespace capture {

CaptureResult captureScreen(uint32_t max_width, uint32_t max_height,
                             bool /*grayscale*/) {
    CaptureResult result;

    @autoreleasepool {
        // Dynamically resolve CGDisplayCreateImage (obsoleted in macOS 15
        // but still available at runtime on all versions)
        static auto func = (CGImageRef(*)(CGDirectDisplayID))dlsym(
            RTLD_DEFAULT, "CGDisplayCreateImage");

        if (!func) {
            result.error_msg = "CGDisplayCreateImage not available";
            return result;
        }

        CGImageRef image = func(kCGDirectMainDisplay);
        if (!image) {
            result.error_msg = "CGDisplayCreateImage failed";
            return result;
        }

        size_t src_w = CGImageGetWidth(image);
        size_t src_h = CGImageGetHeight(image);

        // Downscale if needed
        size_t out_w = src_w;
        size_t out_h = src_h;
        if (max_width > 0 && max_height > 0 &&
            (out_w > max_width || out_h > max_height)) {
            float scale = std::min(static_cast<float>(max_width) / out_w,
                                   static_cast<float>(max_height) / out_h);
            out_w = std::max(static_cast<size_t>(1),
                             static_cast<size_t>(out_w * scale));
            out_h = std::max(static_cast<size_t>(1),
                             static_cast<size_t>(out_h * scale));
        }

        CGImageRef finalImage = image;
        if (out_w != src_w || out_h != src_h) {
            // Downscale via a bitmap context
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(
                nullptr, out_w, out_h, 8, out_w * 4, cs,
                kCGImageAlphaPremultipliedFirst);
            if (ctx) {
                CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
                CGContextDrawImage(ctx, CGRectMake(0, 0, out_w, out_h), image);
                finalImage = CGBitmapContextCreateImage(ctx);
                CGContextRelease(ctx);
            }
            CGColorSpaceRelease(cs);
            if (!finalImage) finalImage = image;
        }

        // Convert to JPEG
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithCGImage:finalImage];
        NSDictionary* props = @{ NSImageCompressionFactor: @0.85 };
        NSData* jpeg = [rep representationUsingType:NSBitmapImageFileTypeJPEG
                                         properties:props];

        if (finalImage != image) CGImageRelease(finalImage);
        CGImageRelease(image);

        if (!jpeg || jpeg.length == 0) {
            result.error_msg = "JPEG encoding failed";
            return result;
        }

        result.jpeg_data.resize(jpeg.length);
        std::memcpy(result.jpeg_data.data(), jpeg.bytes, jpeg.length);
        result.success = true;
        result.width = static_cast<uint32_t>(out_w);
        result.height = static_cast<uint32_t>(out_h);
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
