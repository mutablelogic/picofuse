/**
 * @file types.h
 * @brief Common pixel types and structures.
 * @ingroup Pixel
 *
 * Shared type definitions used across the pixel library.
 */
#pragma once
#include "color.h"
#include <stdbool.h>
#include <stdint.h>

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
 * @brief Compositing mode for drawing operations.
 * @ingroup Pixel
 */
typedef enum {
  PIX_SET,   ///< Overwrite the destination outright, ignoring its previous
             ///< color and the drawn color's own alpha.
  PIX_BLEND, ///< Alpha-composite the drawn color over the destination's
             ///< existing color ("src over dst") - see @ref pix_color_blend.
} pix_op_t;

