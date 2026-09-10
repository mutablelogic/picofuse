/**
 * @file color.h
 * @brief RGBA color type and helpers.
 * @ingroup Pixel
 */
#pragma once
#include <stdint.h>

/**
 * @brief Color value type for pixel operations.
 * @ingroup Pixel
 * @details Encoded as 0xRRGGBBAA (red in the most-significant byte, alpha in
 * the least-significant byte). This is independent of any particular
 * storage layout (e.g. a bitmap's own pixel format) - it's how a single
 * color is passed around, not how a buffer of pixels is packed in memory.
 */
typedef uint32_t pix_color_t;

/**
 * @def PIX_COLOR_NONE
 * @ingroup Pixel
 * @brief Fully transparent (all channels zero) - a "no color" sentinel,
 * distinct from the opaque @ref PIX_COLOR_BLACK.
 */
#define PIX_COLOR_NONE 0x00000000u

/**
 * @def PIX_COLOR_RED
 * @def PIX_COLOR_GREEN
 * @def PIX_COLOR_BLUE
 * @def PIX_COLOR_WHITE
 * @def PIX_COLOR_BLACK
 * @ingroup Pixel
 * @brief Common opaque colors (0xRRGGBBAA).
 */
#define PIX_COLOR_RED 0xFF0000FFu
#define PIX_COLOR_GREEN 0x00FF00FFu
#define PIX_COLOR_BLUE 0x0000FFFFu
#define PIX_COLOR_WHITE 0xFFFFFFFFu
#define PIX_COLOR_BLACK 0x000000FFu

/**
 * @brief Construct a pix_color_t from separate R/G/B/A components.
 * @ingroup Pixel
 */
#define PIX_COLOR_RGBA(r, g, b, a)                                            \
  ((pix_color_t)(((uint32_t)(uint8_t)(r) << 24) |                            \
                ((uint32_t)(uint8_t)(g) << 16) |                            \
                ((uint32_t)(uint8_t)(b) << 8) |                              \
                (uint32_t)(uint8_t)(a)))

/**
 * @brief Construct a fully-opaque pix_color_t from R/G/B components.
 * @ingroup Pixel
 */
#define PIX_COLOR_RGB(r, g, b) PIX_COLOR_RGBA(r, g, b, 0xFFu)

/**
 * @brief Extract the red channel.
 * @ingroup Pixel
 */
static inline uint8_t pix_color_r(pix_color_t color) {
  return (uint8_t)(color >> 24);
}

/**
 * @brief Extract the green channel.
 * @ingroup Pixel
 */
static inline uint8_t pix_color_g(pix_color_t color) {
  return (uint8_t)(color >> 16);
}

/**
 * @brief Extract the blue channel.
 * @ingroup Pixel
 */
static inline uint8_t pix_color_b(pix_color_t color) {
  return (uint8_t)(color >> 8);
}

/**
 * @brief Extract the alpha channel.
 * @ingroup Pixel
 */
static inline uint8_t pix_color_a(pix_color_t color) {
  return (uint8_t)color;
}

/**
 * @brief Alpha-composite @p src over @p dst ("over" compositing).
 * @ingroup Pixel
 * @param src The color being drawn.
 * @param dst The color already there.
 * @return The blended color. Exactly @p src when @p src is fully opaque,
 * exactly @p dst when @p src is fully transparent - callers on a hot path
 * may want to special-case those rather than call this.
 */
static inline pix_color_t pix_color_blend(pix_color_t src, pix_color_t dst) {
  uint16_t sa = pix_color_a(src);
  uint16_t ia = 255u - sa;
  uint8_t r = (uint8_t)((pix_color_r(src) * sa + pix_color_r(dst) * ia + 127u) / 255u);
  uint8_t g = (uint8_t)((pix_color_g(src) * sa + pix_color_g(dst) * ia + 127u) / 255u);
  uint8_t b = (uint8_t)((pix_color_b(src) * sa + pix_color_b(dst) * ia + 127u) / 255u);
  uint8_t a = (uint8_t)(sa + (pix_color_a(dst) * ia + 127u) / 255u);
  return PIX_COLOR_RGBA(r, g, b, a);
}
