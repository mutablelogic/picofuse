#include "private.h"
#include <string.h>

pix_color_t _pix_bitmap_rgba32_get_pixel(const pix_bitmap_t *bitmap,
                                         pix_point_t point) {
  const uint8_t *p = (const uint8_t *)bitmap->data +
                     (size_t)point.y * bitmap->stride + (size_t)point.x * 4;
  return PIX_COLOR_RGBA(p[0], p[1], p[2], p[3]);
}

void _pix_bitmap_rgba32_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                  pix_color_t color) {
  uint8_t *p = (uint8_t *)bitmap->data + (size_t)point.y * bitmap->stride +
              (size_t)point.x * 4;
  p[0] = pix_color_r(color);
  p[1] = pix_color_g(color);
  p[2] = pix_color_b(color);
  p[3] = pix_color_a(color);
}

void _pix_bitmap_rgba32_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                  pix_size_t size, pix_color_t color) {
  uint8_t r = pix_color_r(color);
  uint8_t g = pix_color_g(color);
  uint8_t b = pix_color_b(color);
  uint8_t a = pix_color_a(color);

  // All four channels the same byte value - every byte in the fill is
  // identical, so memset() can fill a whole row (and every row) in one call
  // each, rather than writing one pixel at a time.
  if (r == g && g == b && b == a) {
    for (uint16_t dy = 0; dy < size.h; dy++) {
      uint8_t *row = (uint8_t *)bitmap->data +
                     (size_t)(origin.y + dy) * bitmap->stride +
                     (size_t)origin.x * 4;
      memset(row, r, (size_t)size.w * 4);
    }
    return;
  }

  for (uint16_t dy = 0; dy < size.h; dy++) {
    uint8_t *p = (uint8_t *)bitmap->data +
                (size_t)(origin.y + dy) * bitmap->stride +
                (size_t)origin.x * 4;
    for (uint16_t dx = 0; dx < size.w; dx++) {
      p[0] = r;
      p[1] = g;
      p[2] = b;
      p[3] = a;
      p += 4;
    }
  }
}
