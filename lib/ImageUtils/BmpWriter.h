#pragma once

#include <Print.h>

#include <cstdint>

// ============================================================================
// BMP File Format Writer
// ============================================================================
// Utilities for writing BMP file headers and pixel data in various formats.
// Supports 2-bit (4-level grayscale) and 8-bit (256-level grayscale) BMPs.

namespace ImageUtils {

// Write a 16-bit little-endian value
void write16(Print& out, uint16_t value);

// Write a 32-bit little-endian value
void write32(Print& out, uint32_t value);

// Write a 32-bit signed little-endian value
void write32Signed(Print& out, int32_t value);

// Write BMP file header with 2-bit color depth (4 colors)
// Creates top-down bitmap with 4-color grayscale palette:
// - Color 0: Black (0, 0, 0)
// - Color 1: Dark gray (85, 85, 85)
// - Color 2: Light gray (170, 170, 170)
// - Color 3: White (255, 255, 255)
void writeBmpHeader2Bit(Print& bmpOut, int width, int height);

// Write BMP file header with 8-bit color depth (256 colors)
// Creates top-down bitmap with 256-level grayscale palette
void writeBmpHeader8Bit(Print& bmpOut, int width, int height);

// Get the number of padding bytes needed for a BMP row
// BMP rows must be aligned to 4-byte boundaries
int getBmpRowPadding(int width, int bitsPerPixel);

// Write a single row of 2-bit pixels to BMP file
// pixels: array of quantized values (0-3)
// width: number of pixels in the row
// Handles packing 4 pixels per byte and row padding
void write2BitRow(Print& out, const uint8_t* pixels, int width);

}  // namespace ImageUtils
