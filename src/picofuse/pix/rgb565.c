#include "private.h"
#include <string.h>

static inline uint16_t _pix_bitmap_rgb565_get_packed(pix_color_t color) {
  return (uint16_t)(((pix_color_r(color) >> 3) << 11) |
                    ((pix_color_g(color) >> 2) << 5) |
                    (pix_color_b(color) >> 3));
}

static inline pix_color_t _pix_bitmap_rgb565_get_unpacked(uint16_t packed) {
  uint8_t r = (uint8_t)(((packed >> 11) & 0x1F) << 3);
  uint8_t g = (uint8_t)(((packed >> 5) & 0x3F) << 2);
  uint8_t b = (uint8_t)((packed & 0x1F) << 3);
  return PIX_COLOR_RGB(r, g, b);
}

pix_color_t _pix_bitmap_rgb565_get_pixel(const pix_bitmap_t *bitmap,
                                         pix_point_t point) {
  const uint8_t *row =
      (const uint8_t *)bitmap->data + (size_t)point.y * bitmap->stride;
  uint16_t px = ((const uint16_t *)(const void *)row)[point.x];
  return _pix_bitmap_rgb565_get_unpacked(px);
}

void _pix_bitmap_rgb565_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                  pix_color_t color) {
  uint8_t *row = (uint8_t *)bitmap->data + (size_t)point.y * bitmap->stride;
  uint16_t *p = (uint16_t *)(void *)row + point.x;

  if (bitmap->op == PIX_BLEND) {
    uint8_t a = pix_color_a(color);
    if (a == 0) {
      return; // Fully transparent - destination unchanged.
    }
    if (a != 255) {
      // RGB565 stores no alpha of its own - treat the existing pixel as
      // fully opaque for the blend (_pix_bitmap_rgb565_get_unpacked()
      // does exactly that, via PIX_COLOR_RGB()).
      color = pix_color_blend(color, _pix_bitmap_rgb565_get_unpacked(*p));
    }
  }

  *p = _pix_bitmap_rgb565_get_packed(color);
}

void _pix_bitmap_rgb565_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                  pix_size_t size, pix_color_t color) {
  uint8_t a = pix_color_a(color);

  if (bitmap->op == PIX_BLEND && a == 0) {
    return; // Fully transparent - no-op.
  }

  // Either overwriting outright (PIX_SET), or blending a fully-opaque
  // color - mathematically identical to a plain overwrite either way, so
  // take the fast (and memset()-able) path below rather than the per-pixel
  // blend one further down.
  if (bitmap->op == PIX_SET || a == 255) {
    uint16_t packed = _pix_bitmap_rgb565_get_packed(color);
    uint8_t hi = (uint8_t)(packed >> 8);
    uint8_t lo = (uint8_t)packed;

    // The packed 16-bit value's own two bytes happen to be equal (true for
    // e.g. black/white/grays that round-trip cleanly through 5-6-5) -
    // every byte in the fill is then identical too, so memset() can fill a
    // whole row in one call instead of one pixel at a time.
    if (hi == lo) {
      for (uint16_t dy = 0; dy < size.h; dy++) {
        uint8_t *row = (uint8_t *)bitmap->data +
                       (size_t)(origin.y + dy) * bitmap->stride +
                       (size_t)origin.x * 2;
        memset(row, hi, (size_t)size.w * 2);
      }
      return;
    }

    for (uint16_t dy = 0; dy < size.h; dy++) {
      uint8_t *base = (uint8_t *)bitmap->data +
                      (size_t)(origin.y + dy) * bitmap->stride +
                      (size_t)origin.x * 2;
      uint16_t *row = (uint16_t *)(void *)base;
      for (uint16_t dx = 0; dx < size.w; dx++) {
        row[dx] = packed;
      }
    }
    return;
  }

  // PIX_BLEND with partial alpha - the destination varies per pixel, so
  // there's no bulk write; blend each one in place, one pass, no separate
  // read/write buffer.
  for (uint16_t dy = 0; dy < size.h; dy++) {
    uint8_t *base = (uint8_t *)bitmap->data +
                    (size_t)(origin.y + dy) * bitmap->stride +
                    (size_t)origin.x * 2;
    uint16_t *row = (uint16_t *)(void *)base;
    for (uint16_t dx = 0; dx < size.w; dx++) {
      pix_color_t blended =
          pix_color_blend(color, _pix_bitmap_rgb565_get_unpacked(row[dx]));
      row[dx] = _pix_bitmap_rgb565_get_packed(blended);
    }
  }
}
