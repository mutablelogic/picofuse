#include "private.h"
#include <string.h>

pix_color_t _pix_bitmap_rgb888_get_pixel(const pix_bitmap_t *bitmap,
                                         pix_point_t point) {
  const uint8_t *p = (const uint8_t *)bitmap->data +
                     (size_t)point.y * bitmap->stride + (size_t)point.x * 3;
  return PIX_COLOR_RGB(p[0], p[1], p[2]);
}

void _pix_bitmap_rgb888_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                  pix_color_t color) {
  uint8_t *p = (uint8_t *)bitmap->data + (size_t)point.y * bitmap->stride +
              (size_t)point.x * 3;
  p[0] = pix_color_r(color);
  p[1] = pix_color_g(color);
  p[2] = pix_color_b(color);
}

void _pix_bitmap_rgb888_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                  pix_size_t size, pix_color_t color) {
  uint8_t r = pix_color_r(color);
  uint8_t g = pix_color_g(color);
  uint8_t b = pix_color_b(color);

  // A grayscale color (R==G==B) - every byte in the fill is identical, so
  // memset() can fill a whole row in one call instead of one pixel at a
  // time.
  if (r == g && g == b) {
    for (uint16_t dy = 0; dy < size.h; dy++) {
      uint8_t *row = (uint8_t *)bitmap->data +
                     (size_t)(origin.y + dy) * bitmap->stride +
                     (size_t)origin.x * 3;
      memset(row, r, (size_t)size.w * 3);
    }
    return;
  }

  for (uint16_t dy = 0; dy < size.h; dy++) {
    uint8_t *p = (uint8_t *)bitmap->data +
                (size_t)(origin.y + dy) * bitmap->stride +
                (size_t)origin.x * 3;
    for (uint16_t dx = 0; dx < size.w; dx++) {
      p[0] = r;
      p[1] = g;
      p[2] = b;
      p += 3;
    }
  }
}
