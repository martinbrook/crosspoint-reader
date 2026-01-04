#include "Dithering.h"

#include "ColorConversion.h"

namespace ImageUtils {

// ============================================================================
// AtkinsonDitherer Implementation
// ============================================================================

AtkinsonDitherer::AtkinsonDitherer(int width) : width(width) {
  errorRow0 = new int16_t[width + 4]();  // Current row
  errorRow1 = new int16_t[width + 4]();  // Next row
  errorRow2 = new int16_t[width + 4]();  // Row after next
}

AtkinsonDitherer::~AtkinsonDitherer() {
  delete[] errorRow0;
  delete[] errorRow1;
  delete[] errorRow2;
}

uint8_t AtkinsonDitherer::processPixel(int gray, int x) {
  // Apply brightness/contrast/gamma adjustments
  gray = adjustPixel(gray);

  // Add accumulated error
  int adjusted = gray + errorRow0[x + 2];
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  // Quantize to 4 levels
  uint8_t quantized;
  int quantizedValue;
  if (adjusted < 43) {
    quantized = 0;
    quantizedValue = 0;
  } else if (adjusted < 128) {
    quantized = 1;
    quantizedValue = 85;
  } else if (adjusted < 213) {
    quantized = 2;
    quantizedValue = 170;
  } else {
    quantized = 3;
    quantizedValue = 255;
  }

  // Calculate error (only distribute 6/8 = 75%)
  int error = (adjusted - quantizedValue) >> 3;  // error/8

  // Distribute 1/8 to each of 6 neighbors
  errorRow0[x + 3] += error;  // Right
  errorRow0[x + 4] += error;  // Right+1
  errorRow1[x + 1] += error;  // Bottom-left
  errorRow1[x + 2] += error;  // Bottom
  errorRow1[x + 3] += error;  // Bottom-right
  errorRow2[x + 2] += error;  // Two rows down

  return quantized;
}

void AtkinsonDitherer::nextRow() {
  int16_t* temp = errorRow0;
  errorRow0 = errorRow1;
  errorRow1 = errorRow2;
  errorRow2 = temp;
  memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
}

void AtkinsonDitherer::reset() {
  memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
  memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
  memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
}

// ============================================================================
// FloydSteinbergDitherer Implementation
// ============================================================================

FloydSteinbergDitherer::FloydSteinbergDitherer(int width) : width(width), rowCount(0) {
  errorCurRow = new int16_t[width + 2]();  // +2 for boundary handling
  errorNextRow = new int16_t[width + 2]();
}

FloydSteinbergDitherer::~FloydSteinbergDitherer() {
  delete[] errorCurRow;
  delete[] errorNextRow;
}

uint8_t FloydSteinbergDitherer::processPixel(int gray, int x) {
  return processPixel(gray, x, isReverseRow());
}

uint8_t FloydSteinbergDitherer::processPixel(int gray, int x, bool reverseDirection) {
  // Add accumulated error to this pixel
  int adjusted = gray + errorCurRow[x + 1];

  // Clamp to valid range
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  // Quantize to 4 levels (0, 85, 170, 255)
  uint8_t quantized;
  int quantizedValue;
  if (adjusted < 43) {
    quantized = 0;
    quantizedValue = 0;
  } else if (adjusted < 128) {
    quantized = 1;
    quantizedValue = 85;
  } else if (adjusted < 213) {
    quantized = 2;
    quantizedValue = 170;
  } else {
    quantized = 3;
    quantizedValue = 255;
  }

  // Calculate error
  int error = adjusted - quantizedValue;

  // Distribute error to neighbors (serpentine: direction-aware)
  if (!reverseDirection) {
    // Left to right: standard distribution
    // Right: 7/16
    errorCurRow[x + 2] += (error * 7) >> 4;
    // Bottom-left: 3/16
    errorNextRow[x] += (error * 3) >> 4;
    // Bottom: 5/16
    errorNextRow[x + 1] += (error * 5) >> 4;
    // Bottom-right: 1/16
    errorNextRow[x + 2] += (error) >> 4;
  } else {
    // Right to left: mirrored distribution
    // Left: 7/16
    errorCurRow[x] += (error * 7) >> 4;
    // Bottom-right: 3/16
    errorNextRow[x + 2] += (error * 3) >> 4;
    // Bottom: 5/16
    errorNextRow[x + 1] += (error * 5) >> 4;
    // Bottom-left: 1/16
    errorNextRow[x] += (error) >> 4;
  }

  return quantized;
}

void FloydSteinbergDitherer::nextRow() {
  // Swap buffers
  int16_t* temp = errorCurRow;
  errorCurRow = errorNextRow;
  errorNextRow = temp;
  // Clear the next row buffer
  memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
  rowCount++;
}

bool FloydSteinbergDitherer::isReverseRow() const { return (rowCount & 1) != 0; }

void FloydSteinbergDitherer::reset() {
  memset(errorCurRow, 0, (width + 2) * sizeof(int16_t));
  memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
  rowCount = 0;
}

}  // namespace ImageUtils
