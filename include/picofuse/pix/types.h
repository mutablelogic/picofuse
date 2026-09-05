/**
 * @file types.h
 * @brief Common pixel types and structures.
 * @ingroup Pixel
 *
 * Shared type definitions used across the pixel library.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "color.h"

/**
 * @brief Point structure representing X,Y coordinates.
 * @ingroup Pixel
 */
typedef struct {
  int16_t x; ///< X coordinate
  int16_t y; ///< Y coordinate
} pix_point_t;

/**
 * @brief Size structure representing width and height dimensions.
 * @ingroup Pixel
 */
typedef struct {
  uint16_t w; ///< Width in pixels
  uint16_t h; ///< Height in pixels
} pix_size_t;

/**
 * @brief Pixel format enumeration defining color depth and layout.
 * @ingroup Pixel
 */
typedef enum {
  PIX_FMT_RGBA32, ///< 32-bit RGBA format with alpha channel
  PIX_FMT_RGB888, ///< 24-bit RGB format without alpha channel
  PIX_FMT_RGB565, ///< 16-bit RGB format without alpha channel
  PIX_FMT_MONO,   ///< Monochrome format (1-bit per pixel)
} pix_format_t;

/**
 * @brief Pixel operation types for drawing operations.
 * @ingroup Pixel
 */
typedef enum {
  PIX_SET ///< Set pixel operation
} pix_op_t;

/**
 * @brief Plain in-memory pixel bitmap descriptor.
 * @ingroup Pixel
 * @details Describes a block of raw pixel memory with no backing device.
 * Unlike @ref pix_frame_t, a bitmap has no ctx and no lock/unlock/clear/
 * set/copy methods; callers read and write @ref data directly.
 */
typedef struct {
  void *data;       ///< Pointer to bitmap memory.
  pix_size_t size;  ///< Bitmap dimensions in pixels.
  size_t stride;    ///< Byte pitch between adjacent major-axis elements.
  pix_format_t fmt; ///< Pixel format used by @ref data.
} pix_bitmap_t;
