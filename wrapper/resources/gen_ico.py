#!/usr/bin/env python3
"""Generate a minimal PDF icon (.ico) for the wrapper."""

import struct
import sys

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "pdf.ico"
    size = 32

    # BMP pixel data: 32x32 RGBA (top-down, no compression)
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            # Simple red/white PDF-like icon
            if 4 <= x <= 28 and 4 <= y <= 28:
                if x == 4 or x == 28 or y == 4 or y == 28:
                    r, g, b, a = 200, 30, 30, 255
                elif y <= 12:
                    r, g, b, a = 220, 50, 50, 255
                elif y <= 16:
                    r, g, b, a = 255, 255, 255, 255
                else:
                    r, g, b, a = 235, 235, 235, 255
            else:
                r, g, b, a = 0, 0, 0, 0

            pixels.extend([b, g, r, a])

    # BMP info header (40 bytes)
    bih = struct.pack(
        "<IiiHHIIiiII",
        40,           # bih_size
        size,         # width
        size * 2,     # height (double for ICO XOR + AND)
        1,            # planes
        32,           # bpp
        0,            # compression (BI_RGB)
        len(pixels) * 2,  # image_size (XOR + AND)
        2835, 2835,    # hres, vres
        0, 0           # colors_used, important
    )

    # XOR mask = the pixels
    xor_mask = bytes(pixels)
    # AND mask = 1-bit transparency (0 = opaque, 1 = transparent)
    and_row_size = ((size + 31) // 32) * 4
    and_mask = bytearray()
    for y in range(size):
        row_bits = 0
        for x in range(size):
            # Transparent where alpha < 128
            if pixels[(y * size + x) * 4 + 3] < 128:
                row_bits |= (1 << (7 - (x % 8)))
            if x % 8 == 7 or x == size - 1:
                and_mask.append(row_bits & 0xFF)
                row_bits = 0
        # Pad row to 4 bytes
        while len(and_mask) % 4 != 0:
            and_mask.append(0)

    image_data = xor_mask + bytes(and_mask)
    image_size = len(image_data)
    data_offset = 6 + 16   # header + 1 entry

    with open(out_path, "wb") as f:
        # ICO header
        f.write(struct.pack("<HHH", 0, 1, 1))
        # Directory entry
        f.write(struct.pack(
            "<BBBBHHII",
            size if size < 256 else 0,
            size if size < 256 else 0,
            0,           # colors
            0,           # reserved
            1,           # planes
            32,          # bpp
            image_size,
            data_offset
        ))
        # BMP data
        f.write(bih)
        f.write(image_data)

    print(f"Generated {out_path} ({image_size + data_offset} bytes)")

if __name__ == "__main__":
    main()
