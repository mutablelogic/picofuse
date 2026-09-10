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

// bitmap->data plus a computed byte offset isn't guaranteed 2-byte aligned
// (an arbitrary bitmap->stride, or an odd point.x, can each land on an odd
// address) - a raw uint16_t* cast and dereference on a misaligned address
// is undefined behavior in C, and can genuinely fault on some embedded
// targets (Cortex-M0 notably, which is what RP2040 - this project's own
// primary target - uses). memcpy() sidesteps the alignment requirement
// entirely; compilers already optimize a fixed 2-byte memcpy() down to
// whatever load/store the target can actually do (an unaligned one, or the
// correct byte-wise sequence where that's not available), so this costs
// nothing on platforms where the naive cast would've been fine anyway.
static inline uint16_t _pix_bitmap_rgb565_load(const uint8_t *p) {
  uint16_t packed;
  memcpy(&packed, p, sizeof(packed));
  return packed;
}

static inline void _pix_bitmap_rgb565_store(uint8_t *p, uint16_t packed) {
  memcpy(p, &packed, sizeof(packed));
}

pix_color_t _pix_bitmap_rgb565_get_pixel(const pix_bitmap_t *bitmap,
                                         pix_point_t point) {
  const uint8_t *p = (const uint8_t *)bitmap->data +
                     (size_t)point.y * bitmap->stride + (size_t)point.x * 2;
  return _pix_bitmap_rgb565_get_unpacked(_pix_bitmap_rgb565_load(p));
}

void _pix_bitmap_rgb565_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                  pix_color_t color) {
  uint8_t *p = (uint8_t *)bitmap->data + (size_t)point.y * bitmap->stride +
              (size_t)point.x * 2;

  if (bitmap->op == PIX_BLEND) {
    uint8_t a = pix_color_a(color);
    if (a == 0) {
      return; // Fully transparent - destination unchanged.
    }
    if (a != 255) {
      // RGB565 stores no alpha of its own - treat the existing pixel as
      // fully opaque for the blend (_pix_bitmap_rgb565_get_unpacked()
      // does exactly that, via PIX_COLOR_RGB()).
      pix_color_t dst = _pix_bitmap_rgb565_get_unpacked(_pix_bitmap_rgb565_load(p));
      color = pix_color_blend(color, dst);
    }
  }

  _pix_bitmap_rgb565_store(p, _pix_bitmap_rgb565_get_packed(color));
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
    // whole row in one call instead of one pixel at a time. memset() is
    // byte-wise, so it has no alignment requirement to worry about either.
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
      uint8_t *p = (uint8_t *)bitmap->data +
                  (size_t)(origin.y + dy) * bitmap->stride +
                  (size_t)origin.x * 2;
      for (uint16_t dx = 0; dx < size.w; dx++) {
        _pix_bitmap_rgb565_store(p, packed);
        p += 2;
      }
    }
    return;
  }

  // PIX_BLEND with partial alpha - the destination varies per pixel, so
  // there's no bulk write; blend each one in place, one pass, no separate
  // read/write buffer.
  for (uint16_t dy = 0; dy < size.h; dy++) {
    uint8_t *p = (uint8_t *)bitmap->data +
                (size_t)(origin.y + dy) * bitmap->stride +
                (size_t)origin.x * 2;
    for (uint16_t dx = 0; dx < size.w; dx++) {
      pix_color_t dst = _pix_bitmap_rgb565_get_unpacked(_pix_bitmap_rgb565_load(p));
      pix_color_t blended = pix_color_blend(color, dst);
      _pix_bitmap_rgb565_store(p, _pix_bitmap_rgb565_get_packed(blended));
      p += 2;
    }
  }
}
