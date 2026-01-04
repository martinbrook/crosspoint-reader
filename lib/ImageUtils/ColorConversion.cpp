#include "ColorConversion.h"

namespace ImageUtils {

// Global color configuration
ColorConfig colorConfig;

// Integer approximation of gamma correction (brightens midtones)
int applyGamma(int gray) {
  if (!colorConfig.gammaCorrection) return gray;
  // Fast integer square root approximation for gamma ~0.5 (brightening)
  // This brightens dark/mid tones while preserving highlights
  const int product = gray * 255;
  // Newton-Raphson integer sqrt (2 iterations for good accuracy)
  int x = gray;
  if (x > 0) {
    x = (x + product / x) >> 1;
    x = (x + product / x) >> 1;
  }
  return x > 255 ? 255 : x;
}

// Apply contrast adjustment around midpoint (128)
int applyContrast(int gray) {
  // Integer-based contrast: (gray - 128) * factor + 128
  // Using fixed-point: factor 1.15 ≈ 115/100
  const int factorNum = static_cast<int>(colorConfig.contrastFactor * 100);
  int adjusted = ((gray - 128) * factorNum) / 100 + 128;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  return adjusted;
}

// Combined brightness/contrast/gamma adjustment
int adjustPixel(int gray) {
  if (!colorConfig.useBrightness) return gray;

  // Order: contrast first, then brightness, then gamma
  gray = applyContrast(gray);
  gray += colorConfig.brightnessBoost;
  if (gray > 255) gray = 255;
  if (gray < 0) gray = 0;
  gray = applyGamma(gray);

  return gray;
}

// Simple quantization without dithering - just divide into 4 levels
uint8_t quantizeSimple(int gray) {
  gray = adjustPixel(gray);
  // Simple 2-bit quantization: 0-63=0, 64-127=1, 128-191=2, 192-255=3
  return static_cast<uint8_t>(gray >> 6);
}

// Hash-based noise dithering - survives downsampling without moiré artifacts
uint8_t quantizeNoise(int gray, int x, int y) {
  gray = adjustPixel(gray);

  // Generate noise threshold using integer hash (no regular pattern to alias)
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);  // 0-255

  // Map gray (0-255) to 4 levels with dithering
  const int scaled = gray * 3;

  if (scaled < 255) {
    return (scaled + threshold >= 255) ? 1 : 0;
  } else if (scaled < 510) {
    return ((scaled - 255) + threshold >= 255) ? 2 : 1;
  } else {
    return ((scaled - 510) + threshold >= 255) ? 3 : 2;
  }
}

// Main quantization function (without error diffusion dithering)
uint8_t quantize2Bit(int gray) {
  // For error diffusion dithering, use Ditherer classes instead
  return quantizeSimple(gray);
}

// Convert RGB to grayscale using weighted average
uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) {
  // Weighted average: green contributes most to perceived brightness
  // Formula: (R*25 + G*50 + B*25) / 100
  return static_cast<uint8_t>((r * 25 + g * 50 + b * 25) / 100);
}

// Blend foreground color with white background using alpha
uint8_t blendAlpha(uint8_t fg, uint8_t alpha) {
  // alpha * fg + (255 - alpha) * 255 / 255
  // Simplifies to: (alpha * fg + (255 - alpha) * 255) / 255
  const uint16_t result = (static_cast<uint16_t>(alpha) * fg + (255 - alpha) * 255) / 255;
  return static_cast<uint8_t>(result);
}

}  // namespace ImageUtils
