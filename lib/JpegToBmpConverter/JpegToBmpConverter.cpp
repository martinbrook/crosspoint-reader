#include "JpegToBmpConverter.h"

#include <BmpWriter.h>
#include <ColorConversion.h>
#include <Dithering.h>
#include <HardwareSerial.h>
#include <SdFat.h>
#include <picojpeg.h>

#include <cstdio>
#include <cstring>

using namespace ImageUtils;

// Context structure for picojpeg callback
struct JpegReadContext {
  FsFile& file;
  uint8_t buffer[512];
  size_t bufferPos;
  size_t bufferFilled;
};

// ============================================================================
// IMAGE PROCESSING OPTIONS - Toggle these to test different configurations
// ============================================================================
constexpr bool USE_8BIT_OUTPUT = false;  // true: 8-bit grayscale (no quantization), false: 2-bit (4 levels)
// Dithering method selection (only one should be true, or all false for simple quantization):
constexpr bool USE_ATKINSON = true;          // Atkinson dithering (cleaner than F-S, less error diffusion)
constexpr bool USE_FLOYD_STEINBERG = false;  // Floyd-Steinberg error diffusion (can cause "worm" artifacts)
constexpr bool USE_NOISE_DITHERING = false;  // Hash-based noise dithering (good for downsampling)
// Pre-resize to target display size (CRITICAL: avoids dithering artifacts from post-downsampling)
constexpr bool USE_PRESCALE = true;     // true: scale image to target size before dithering
constexpr int TARGET_MAX_WIDTH = 480;   // Max width for cover images (portrait display width)
constexpr int TARGET_MAX_HEIGHT = 800;  // Max height for cover images (portrait display height)
// ============================================================================

// Simple quantization wrapper for backward compatibility with noise dithering
static inline uint8_t quantize(int gray, int x, int y) {
  if (USE_NOISE_DITHERING) {
    return quantizeNoise(gray, x, y);
  } else {
    return quantize2Bit(gray);
  }
}

// Callback function for picojpeg to read JPEG data
unsigned char JpegToBmpConverter::jpegReadCallback(unsigned char* pBuf, const unsigned char buf_size,
                                                   unsigned char* pBytes_actually_read, void* pCallback_data) {
  auto* context = static_cast<JpegReadContext*>(pCallback_data);

  if (!context || !context->file) {
    return PJPG_STREAM_READ_ERROR;
  }

  // Check if we need to refill our context buffer
  if (context->bufferPos >= context->bufferFilled) {
    context->bufferFilled = context->file.read(context->buffer, sizeof(context->buffer));
    context->bufferPos = 0;

    if (context->bufferFilled == 0) {
      // EOF or error
      *pBytes_actually_read = 0;
      return 0;  // Success (EOF is normal)
    }
  }

  // Copy available bytes to picojpeg's buffer
  const size_t available = context->bufferFilled - context->bufferPos;
  const size_t toRead = available < buf_size ? available : buf_size;

  memcpy(pBuf, context->buffer + context->bufferPos, toRead);
  context->bufferPos += toRead;
  *pBytes_actually_read = static_cast<unsigned char>(toRead);

  return 0;  // Success
}

// Core function: Convert JPEG file to 2-bit BMP
bool JpegToBmpConverter::jpegFileToBmpStream(FsFile& jpegFile, Print& bmpOut) {
  Serial.printf("[%lu] [JPG] Converting JPEG to BMP\n", millis());

  // Configure ImageUtils color processing
  colorConfig.useBrightness = true;
  colorConfig.brightnessBoost = 10;
  colorConfig.gammaCorrection = true;
  colorConfig.contrastFactor = 1.15f;

  // Setup context for picojpeg callback
  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};

  // Initialize picojpeg decoder
  pjpeg_image_info_t imageInfo;
  const unsigned char status = pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0);
  if (status != 0) {
    Serial.printf("[%lu] [JPG] JPEG decode init failed with error code: %d\n", millis(), status);
    return false;
  }

  Serial.printf("[%lu] [JPG] JPEG dimensions: %dx%d, components: %d, MCUs: %dx%d\n", millis(), imageInfo.m_width,
                imageInfo.m_height, imageInfo.m_comps, imageInfo.m_MCUSPerRow, imageInfo.m_MCUSPerCol);

  // Safety limits to prevent memory issues on ESP32
  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  constexpr int MAX_MCU_ROW_BYTES = 65536;

  if (imageInfo.m_width > MAX_IMAGE_WIDTH || imageInfo.m_height > MAX_IMAGE_HEIGHT) {
    Serial.printf("[%lu] [JPG] Image too large (%dx%d), max supported: %dx%d\n", millis(), imageInfo.m_width,
                  imageInfo.m_height, MAX_IMAGE_WIDTH, MAX_IMAGE_HEIGHT);
    return false;
  }

  // Calculate output dimensions (pre-scale to fit display exactly)
  int outWidth = imageInfo.m_width;
  int outHeight = imageInfo.m_height;
  // Use fixed-point scaling (16.16) for sub-pixel accuracy
  uint32_t scaleX_fp = 65536;  // 1.0 in 16.16 fixed point
  uint32_t scaleY_fp = 65536;
  bool needsScaling = false;

  if (USE_PRESCALE && (imageInfo.m_width > TARGET_MAX_WIDTH || imageInfo.m_height > TARGET_MAX_HEIGHT)) {
    // Calculate scale to fit within target dimensions while maintaining aspect ratio
    const float scaleToFitWidth = static_cast<float>(TARGET_MAX_WIDTH) / imageInfo.m_width;
    const float scaleToFitHeight = static_cast<float>(TARGET_MAX_HEIGHT) / imageInfo.m_height;
    const float scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;

    outWidth = static_cast<int>(imageInfo.m_width * scale);
    outHeight = static_cast<int>(imageInfo.m_height * scale);

    // Ensure at least 1 pixel
    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;

    // Calculate fixed-point scale factors (source pixels per output pixel)
    // scaleX_fp = (srcWidth << 16) / outWidth
    scaleX_fp = (static_cast<uint32_t>(imageInfo.m_width) << 16) / outWidth;
    scaleY_fp = (static_cast<uint32_t>(imageInfo.m_height) << 16) / outHeight;
    needsScaling = true;

    Serial.printf("[%lu] [JPG] Pre-scaling %dx%d -> %dx%d (fit to %dx%d)\n", millis(), imageInfo.m_width,
                  imageInfo.m_height, outWidth, outHeight, TARGET_MAX_WIDTH, TARGET_MAX_HEIGHT);
  }

  // Write BMP header with output dimensions
  int bytesPerRow;
  if (USE_8BIT_OUTPUT) {
    writeBmpHeader8Bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 3) / 4 * 4;
  } else {
    writeBmpHeader2Bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth * 2 + 31) / 32 * 4;
  }

  // Allocate row buffer
  auto* rowBuffer = static_cast<uint8_t*>(malloc(bytesPerRow));
  if (!rowBuffer) {
    Serial.printf("[%lu] [JPG] Failed to allocate row buffer\n", millis());
    return false;
  }

  // Allocate a buffer for one MCU row worth of grayscale pixels
  // This is the minimal memory needed for streaming conversion
  const int mcuPixelHeight = imageInfo.m_MCUHeight;
  const int mcuRowPixels = imageInfo.m_width * mcuPixelHeight;

  // Validate MCU row buffer size before allocation
  if (mcuRowPixels > MAX_MCU_ROW_BYTES) {
    Serial.printf("[%lu] [JPG] MCU row buffer too large (%d bytes), max: %d\n", millis(), mcuRowPixels,
                  MAX_MCU_ROW_BYTES);
    free(rowBuffer);
    return false;
  }

  auto* mcuRowBuffer = static_cast<uint8_t*>(malloc(mcuRowPixels));
  if (!mcuRowBuffer) {
    Serial.printf("[%lu] [JPG] Failed to allocate MCU row buffer (%d bytes)\n", millis(), mcuRowPixels);
    free(rowBuffer);
    return false;
  }

  // Create ditherer if enabled (only for 2-bit output)
  // Use OUTPUT dimensions for dithering (after prescaling)
  AtkinsonDitherer* atkinsonDitherer = nullptr;
  FloydSteinbergDitherer* fsDitherer = nullptr;
  if (!USE_8BIT_OUTPUT) {
    if (USE_ATKINSON) {
      atkinsonDitherer = new AtkinsonDitherer(outWidth);
    } else if (USE_FLOYD_STEINBERG) {
      fsDitherer = new FloydSteinbergDitherer(outWidth);
    }
  }

  // For scaling: accumulate source rows into scaled output rows
  // We need to track which source Y maps to which output Y
  // Using fixed-point: srcY_fp = outY * scaleY_fp (gives source Y in 16.16 format)
  uint32_t* rowAccum = nullptr;    // Accumulator for each output X (32-bit for larger sums)
  uint16_t* rowCount = nullptr;    // Count of source pixels accumulated per output X
  int currentOutY = 0;             // Current output row being accumulated
  uint32_t nextOutY_srcStart = 0;  // Source Y where next output row starts (16.16 fixed point)

  if (needsScaling) {
    rowAccum = new uint32_t[outWidth]();
    rowCount = new uint16_t[outWidth]();
    nextOutY_srcStart = scaleY_fp;  // First boundary is at scaleY_fp (source Y for outY=1)
  }

  // Process MCUs row-by-row and write to BMP as we go (top-down)
  const int mcuPixelWidth = imageInfo.m_MCUWidth;

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) {
    // Clear the MCU row buffer
    memset(mcuRowBuffer, 0, mcuRowPixels);

    // Decode one row of MCUs
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) {
      const unsigned char mcuStatus = pjpeg_decode_mcu();
      if (mcuStatus != 0) {
        if (mcuStatus == PJPG_NO_MORE_BLOCKS) {
          Serial.printf("[%lu] [JPG] Unexpected end of blocks at MCU (%d, %d)\n", millis(), mcuX, mcuY);
        } else {
          Serial.printf("[%lu] [JPG] JPEG decode MCU failed at (%d, %d) with error code: %d\n", millis(), mcuX, mcuY,
                        mcuStatus);
        }
        free(mcuRowBuffer);
        free(rowBuffer);
        return false;
      }

      // picojpeg stores MCU data in 8x8 blocks
      // Block layout: H2V2(16x16)=0,64,128,192 H2V1(16x8)=0,64 H1V2(8x16)=0,128
      for (int blockY = 0; blockY < mcuPixelHeight; blockY++) {
        for (int blockX = 0; blockX < mcuPixelWidth; blockX++) {
          const int pixelX = mcuX * mcuPixelWidth + blockX;
          if (pixelX >= imageInfo.m_width) continue;

          // Calculate proper block offset for picojpeg buffer
          const int blockCol = blockX / 8;
          const int blockRow = blockY / 8;
          const int localX = blockX % 8;
          const int localY = blockY % 8;
          const int blocksPerRow = mcuPixelWidth / 8;
          const int blockIndex = blockRow * blocksPerRow + blockCol;
          const int pixelOffset = blockIndex * 64 + localY * 8 + localX;

          uint8_t gray;
          if (imageInfo.m_comps == 1) {
            gray = imageInfo.m_pMCUBufR[pixelOffset];
          } else {
            const uint8_t r = imageInfo.m_pMCUBufR[pixelOffset];
            const uint8_t g = imageInfo.m_pMCUBufG[pixelOffset];
            const uint8_t b = imageInfo.m_pMCUBufB[pixelOffset];
            gray = (r * 25 + g * 50 + b * 25) / 100;
          }

          mcuRowBuffer[blockY * imageInfo.m_width + pixelX] = gray;
        }
      }
    }

    // Process source rows from this MCU row
    const int startRow = mcuY * mcuPixelHeight;
    const int endRow = (mcuY + 1) * mcuPixelHeight;

    for (int y = startRow; y < endRow && y < imageInfo.m_height; y++) {
      const int bufferY = y - startRow;

      if (!needsScaling) {
        // No scaling - direct output (1:1 mapping)
        memset(rowBuffer, 0, bytesPerRow);

        if (USE_8BIT_OUTPUT) {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = mcuRowBuffer[bufferY * imageInfo.m_width + x];
            rowBuffer[x] = adjustPixel(gray);
          }
        } else {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = mcuRowBuffer[bufferY * imageInfo.m_width + x];
            uint8_t twoBit;
            if (atkinsonDitherer) {
              twoBit = atkinsonDitherer->processPixel(gray, x);
            } else if (fsDitherer) {
              twoBit = fsDitherer->processPixel(gray, x, fsDitherer->isReverseRow());
            } else {
              twoBit = quantize(gray, x, y);
            }
            const int byteIndex = (x * 2) / 8;
            const int bitOffset = 6 - ((x * 2) % 8);
            rowBuffer[byteIndex] |= (twoBit << bitOffset);
          }
          if (atkinsonDitherer)
            atkinsonDitherer->nextRow();
          else if (fsDitherer)
            fsDitherer->nextRow();
        }
        bmpOut.write(rowBuffer, bytesPerRow);
      } else {
        // Fixed-point area averaging for exact fit scaling
        // For each output pixel X, accumulate source pixels that map to it
        // srcX range for outX: [outX * scaleX_fp >> 16, (outX+1) * scaleX_fp >> 16)
        const uint8_t* srcRow = mcuRowBuffer + bufferY * imageInfo.m_width;

        for (int outX = 0; outX < outWidth; outX++) {
          // Calculate source X range for this output pixel
          const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
          const int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;

          // Accumulate all source pixels in this range
          int sum = 0;
          int count = 0;
          for (int srcX = srcXStart; srcX < srcXEnd && srcX < imageInfo.m_width; srcX++) {
            sum += srcRow[srcX];
            count++;
          }

          // Handle edge case: if no pixels in range, use nearest
          if (count == 0 && srcXStart < imageInfo.m_width) {
            sum = srcRow[srcXStart];
            count = 1;
          }

          rowAccum[outX] += sum;
          rowCount[outX] += count;
        }

        // Check if we've crossed into the next output row
        // Current source Y in fixed point: y << 16
        const uint32_t srcY_fp = static_cast<uint32_t>(y + 1) << 16;

        // Output row when source Y crosses the boundary
        if (srcY_fp >= nextOutY_srcStart && currentOutY < outHeight) {
          memset(rowBuffer, 0, bytesPerRow);

          if (USE_8BIT_OUTPUT) {
            for (int x = 0; x < outWidth; x++) {
              const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
              rowBuffer[x] = adjustPixel(gray);
            }
          } else {
            for (int x = 0; x < outWidth; x++) {
              const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
              uint8_t twoBit;
              if (atkinsonDitherer) {
                twoBit = atkinsonDitherer->processPixel(gray, x);
              } else if (fsDitherer) {
                twoBit = fsDitherer->processPixel(gray, x, fsDitherer->isReverseRow());
              } else {
                twoBit = quantize(gray, x, currentOutY);
              }
              const int byteIndex = (x * 2) / 8;
              const int bitOffset = 6 - ((x * 2) % 8);
              rowBuffer[byteIndex] |= (twoBit << bitOffset);
            }
            if (atkinsonDitherer)
              atkinsonDitherer->nextRow();
            else if (fsDitherer)
              fsDitherer->nextRow();
          }

          bmpOut.write(rowBuffer, bytesPerRow);
          currentOutY++;

          // Reset accumulators for next output row
          memset(rowAccum, 0, outWidth * sizeof(uint32_t));
          memset(rowCount, 0, outWidth * sizeof(uint16_t));

          // Update boundary for next output row
          nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;
        }
      }
    }
  }

  // Clean up
  if (rowAccum) {
    delete[] rowAccum;
  }
  if (rowCount) {
    delete[] rowCount;
  }
  if (atkinsonDitherer) {
    delete atkinsonDitherer;
  }
  if (fsDitherer) {
    delete fsDitherer;
  }
  free(mcuRowBuffer);
  free(rowBuffer);

  Serial.printf("[%lu] [JPG] Successfully converted JPEG to BMP\n", millis());
  return true;
}
