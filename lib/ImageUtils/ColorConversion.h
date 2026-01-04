#pragma once

#include <cstdint>

// ============================================================================
// Color Conversion and Quantization Utilities
// ============================================================================
// Shared utilities for image processing: brightness/contrast/gamma adjustments,
// grayscale conversion, and quantization to 2-bit (4-level) color depth.

namespace ImageUtils {

// Configuration for pixel adjustments
struct ColorConfig {
  bool useBrightness = true;      // Enable brightness/gamma adjustments
  int brightnessBoost = 10;       // Brightness offset (0-50)
  bool gammaCorrection = true;    // Gamma curve (brightens midtones)
  float contrastFactor = 1.15f;   // Contrast multiplier (1.0 = no change, >1 = more contrast)
};

// Global configuration (defaults match JPEG converter settings)
extern ColorConfig colorConfig;

// Integer approximation of gamma correction (brightens midtones)
// Uses curve: out = 255 * sqrt(in/255) ≈ sqrt(in * 255)
int applyGamma(int gray);

// Apply contrast adjustment around midpoint (128)
// factor > 1.0 increases contrast, < 1.0 decreases
int applyContrast(int gray);

// Combined brightness/contrast/gamma adjustment
// Order: contrast first, then brightness, then gamma
int adjustPixel(int gray);

// Simple quantization without dithering - divide into 4 levels
// Returns 0-3 for 2-bit color depth
uint8_t quantizeSimple(int gray);

// Hash-based noise dithering - survives downsampling without moiré artifacts
// Returns 0-3 for 2-bit color depth
uint8_t quantizeNoise(int gray, int x, int y);

// Main quantization function (without dithering)
// Returns 0-3 for 2-bit color depth
uint8_t quantize2Bit(int gray);

// Convert RGB to grayscale using weighted average
// Formula: (R*25 + G*50 + B*25) / 100
uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b);

// Blend foreground color with white background using alpha
// Formula: alpha * fg + (255 - alpha) * 255 / 255
// alpha: 0-255 (0=fully transparent, 255=fully opaque)
uint8_t blendAlpha(uint8_t fg, uint8_t alpha);

}  // namespace ImageUtils
