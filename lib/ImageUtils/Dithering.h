#pragma once

#include <cstdint>
#include <cstring>

// ============================================================================
// Error Diffusion Dithering Algorithms
// ============================================================================
// Provides AtkinsonDitherer and FloydSteinbergDitherer classes for
// converting grayscale images to 2-bit (4-level) output with error diffusion.

namespace ImageUtils {

// Base ditherer interface
class Ditherer {
 public:
  virtual ~Ditherer() = default;

  // Process a single pixel and return quantized 2-bit value (0-3)
  // gray: input grayscale value (0-255)
  // x: horizontal position in image
  // returns: quantized value (0-3)
  virtual uint8_t processPixel(int gray, int x) = 0;

  // Advance to next scanline
  virtual void nextRow() = 0;

  // Reset for a new image
  virtual void reset() = 0;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer : public Ditherer {
 public:
  explicit AtkinsonDitherer(int width);
  ~AtkinsonDitherer() override;

  uint8_t processPixel(int gray, int x) override;
  void nextRow() override;
  void reset() override;

 private:
  int width;
  int16_t* errorRow0;  // Current row
  int16_t* errorRow1;  // Next row
  int16_t* errorRow2;  // Row after next
};

// Floyd-Steinberg error diffusion dithering with serpentine scanning
// Serpentine scanning alternates direction each row to reduce "worm" artifacts
// Error distribution pattern (left-to-right):
//       X   7/16
// 3/16 5/16 1/16
// Error distribution pattern (right-to-left, mirrored):
// 1/16 5/16 3/16
//      7/16  X
class FloydSteinbergDitherer : public Ditherer {
 public:
  explicit FloydSteinbergDitherer(int width);
  ~FloydSteinbergDitherer() override;

  // Process a single pixel with optional reverse direction for serpentine
  uint8_t processPixel(int gray, int x) override;
  uint8_t processPixel(int gray, int x, bool reverseDirection);

  void nextRow() override;
  void reset() override;

  // Check if current row should be processed in reverse (for serpentine)
  bool isReverseRow() const;

 private:
  int width;
  int rowCount;
  int16_t* errorCurRow;
  int16_t* errorNextRow;
};

}  // namespace ImageUtils
