/**
 * @file bitmap.h
 * @brief Plain in-memory pixel bitmap descriptor.
 * @defgroup PixelBitmap Bitmap
 * @ingroup Pixel
 *
 * A bitmap is a plain, backend-agnostic pixel buffer descriptor - see
 * `pix_bitmap_t`'s own doc.
 */
#pragma once
#include "types.h"
#include <stddef.h>

/**
 * @brief Backend-specific bitmap operations - opaque to public API callers.
 * @ingroup PixelBitmap
 */
typedef struct pix_bitmap_ops_t pix_bitmap_ops_t;

/**
 * @brief Plain in-memory pixel bitmap descriptor.
 * @ingroup PixelBitmap
 * @details Describes a block of pixel memory; callers read and write
 * @ref data directly.
 */
typedef struct {
  void *data;       ///< Pointer to bitmap memory.
  pix_size_t size;  ///< Bitmap dimensions in pixels.
  size_t stride;    ///< Byte pitch between adjacent major-axis elements.
  pix_format_t fmt; ///< Pixel format used by @ref data.
  const pix_bitmap_ops_t *ops; ///< Backend-specific operations, or `NULL` -
                              ///< see @ref pix_bitmap_ops_t.
  pix_op_t op; ///< Compositing mode for pix_bitmap_set_pixel()/
              ///< pix_bitmap_fill_rect() - `PIX_BLEND` by default. Sticky
              ///< until changed - see pix_bitmap_set_op().
} pix_bitmap_t;

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Read a single pixel's color.
 * @ingroup PixelBitmap
 * @param bitmap The bitmap to read from.
 * @param point The pixel coordinates to read.
 * @return The pixel's color, or `PIX_COLOR_NONE` if @p bitmap is invalid,
 * @p point is out of bounds, or @p bitmap's format isn't supported.
 */
pix_color_t pix_bitmap_get_pixel(const pix_bitmap_t *bitmap, pix_point_t point);

/**
 * @brief Write a single pixel's color.
 * @ingroup PixelBitmap
 * @param bitmap The bitmap to write to.
 * @param point The pixel coordinates to write.
 * @param color The color to write - composited according to
 * @p bitmap's own `op` (see pix_bitmap_t::op), which defaults to
 * `PIX_BLEND`; a format with no alpha channel of its own treats its
 * existing pixel as fully opaque for that blend.
 *
 * A no-op if @p bitmap is invalid, @p point is out of bounds, or
 * @p bitmap's format isn't supported.
 */
void pix_bitmap_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                          pix_color_t color);

/**
 * @brief Fill a rectangle with a single color.
 * @ingroup PixelBitmap
 * @param bitmap The bitmap to write to.
 * @param origin The rectangle's top-left corner.
 * @param size The rectangle's dimensions.
 * @param color The color to fill with - see pix_bitmap_set_pixel()'s own
 * doc on compositing.
 *
 * Silently clips to @p bitmap's own bounds - a no-op if @p bitmap is
 * invalid.
 */
void pix_bitmap_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                          pix_size_t size, pix_color_t color);

/**
 * @brief Change a bitmap's compositing mode.
 * @ingroup PixelBitmap
 * @param bitmap The bitmap to change. A no-op if invalid.
 * @param op The new mode - see pix_bitmap_t::op.
 */
void pix_bitmap_set_op(pix_bitmap_t *bitmap, pix_op_t op);

/** @} */
