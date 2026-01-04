#include "BmpWriter.h"

namespace ImageUtils {

// Write a 16-bit little-endian value
void write16(Print& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

// Write a 32-bit little-endian value
void write32(Print& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

// Write a 32-bit signed little-endian value
void write32Signed(Print& out, const int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

// Write BMP file header with 2-bit color depth
void writeBmpHeader2Bit(Print& bmpOut, const int width, const int height) {
  // Calculate row padding (each row must be multiple of 4 bytes)
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;  // 2 bits per pixel, round up
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 70 + imageSize;  // 14 (file header) + 40 (DIB header) + 16 (palette) + image

  // BMP File Header (14 bytes)
  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);  // File size
  write32(bmpOut, 0);         // Reserved
  write32(bmpOut, 70);        // Offset to pixel data

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);  // Negative height = top-down bitmap
  write16(bmpOut, 1);              // Color planes
  write16(bmpOut, 2);              // Bits per pixel (2 bits)
  write32(bmpOut, 0);              // BI_RGB (no compression)
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);  // xPixelsPerMeter (72 DPI)
  write32(bmpOut, 2835);  // yPixelsPerMeter (72 DPI)
  write32(bmpOut, 4);     // colorsUsed
  write32(bmpOut, 4);     // colorsImportant

  // Color Palette (4 colors x 4 bytes = 16 bytes)
  // Format: Blue, Green, Red, Reserved (BGRA)
  uint8_t palette[16] = {
      0x00, 0x00, 0x00, 0x00,  // Color 0: Black
      0x55, 0x55, 0x55, 0x00,  // Color 1: Dark gray (85)
      0xAA, 0xAA, 0xAA, 0x00,  // Color 2: Light gray (170)
      0xFF, 0xFF, 0xFF, 0x00   // Color 3: White
  };
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

// Write BMP file header with 8-bit color depth
void writeBmpHeader8Bit(Print& bmpOut, const int width, const int height) {
  // Calculate row padding (each row must be multiple of 4 bytes)
  const int bytesPerRow = (width + 3) / 4 * 4;  // 8 bits per pixel, padded
  const int imageSize = bytesPerRow * height;
  const uint32_t paletteSize = 256 * 4;  // 256 colors * 4 bytes (BGRA)
  const uint32_t fileSize = 14 + 40 + paletteSize + imageSize;

  // BMP File Header (14 bytes)
  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);                      // Reserved
  write32(bmpOut, 14 + 40 + paletteSize);  // Offset to pixel data

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);  // Negative height = top-down bitmap
  write16(bmpOut, 1);              // Color planes
  write16(bmpOut, 8);              // Bits per pixel (8 bits)
  write32(bmpOut, 0);              // BI_RGB (no compression)
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);  // xPixelsPerMeter (72 DPI)
  write32(bmpOut, 2835);  // yPixelsPerMeter (72 DPI)
  write32(bmpOut, 256);   // colorsUsed
  write32(bmpOut, 256);   // colorsImportant

  // Color Palette (256 grayscale entries x 4 bytes = 1024 bytes)
  // Format: Blue, Green, Red, Reserved (BGRA)
  for (int i = 0; i < 256; i++) {
    bmpOut.write(static_cast<uint8_t>(i));  // Blue
    bmpOut.write(static_cast<uint8_t>(i));  // Green
    bmpOut.write(static_cast<uint8_t>(i));  // Red
    bmpOut.write(static_cast<uint8_t>(0));  // Reserved
  }
}

// Get the number of padding bytes needed for a BMP row
int getBmpRowPadding(int width, int bitsPerPixel) {
  const int bytesPerRow = (width * bitsPerPixel + 7) / 8;  // Round up to nearest byte
  const int paddedRow = (bytesPerRow + 3) / 4 * 4;         // Round up to multiple of 4
  return paddedRow - bytesPerRow;
}

// Write a single row of 2-bit pixels to BMP file
void write2BitRow(Print& out, const uint8_t* pixels, int width) {
  // Pack 4 pixels per byte (2 bits each)
  int x = 0;
  while (x < width) {
    uint8_t packed = 0;
    // Pack up to 4 pixels into one byte (MSB first)
    for (int shift = 6; shift >= 0 && x < width; shift -= 2, x++) {
      packed |= (pixels[x] & 0x03) << shift;
    }
    out.write(packed);
  }

  // Add row padding (BMP rows must be aligned to 4 bytes)
  const int padding = getBmpRowPadding(width, 2);
  for (int i = 0; i < padding; i++) {
    out.write(static_cast<uint8_t>(0));
  }
}

}  // namespace ImageUtils
