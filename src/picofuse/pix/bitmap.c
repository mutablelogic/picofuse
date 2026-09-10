#include "private.h"

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static inline bool _pix_bitmap_in_bounds(const pix_bitmap_t *bitmap,
                                         pix_point_t point) {
  return bitmap != NULL && point.x >= 0 && point.y >= 0 &&
         (uint16_t)point.x < bitmap->size.w &&
         (uint16_t)point.y < bitmap->size.h;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

pix_color_t pix_bitmap_get_pixel(const pix_bitmap_t *bitmap, pix_point_t point) {
  if (!_pix_bitmap_in_bounds(bitmap, point)) {
    return PIX_COLOR_NONE;
  }
  if (bitmap->ops != NULL && bitmap->ops->get_pixel != NULL) {
    return bitmap->ops->get_pixel(bitmap, point);
  }
  if (bitmap->data == NULL) {
    return PIX_COLOR_NONE;
  }
  switch (bitmap->fmt) {
  case PIX_FMT_RGBA32:
    return _pix_bitmap_rgba32_get_pixel(bitmap, point);
  case PIX_FMT_RGB888:
    return _pix_bitmap_rgb888_get_pixel(bitmap, point);
  case PIX_FMT_RGB565:
    return _pix_bitmap_rgb565_get_pixel(bitmap, point);
  case PIX_FMT_MONO:
    return _pix_bitmap_mono_get_pixel(bitmap, point);
  }
  return PIX_COLOR_NONE;
}

void pix_bitmap_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                          pix_color_t color) {
  if (!_pix_bitmap_in_bounds(bitmap, point)) {
    return;
  }
  if (bitmap->ops != NULL && bitmap->ops->set_pixel != NULL) {
    bitmap->ops->set_pixel(bitmap, point, color);
    return;
  }
  if (bitmap->data == NULL) {
    return;
  }
  switch (bitmap->fmt) {
  case PIX_FMT_RGBA32:
    _pix_bitmap_rgba32_set_pixel(bitmap, point, color);
    return;
  case PIX_FMT_RGB888:
    _pix_bitmap_rgb888_set_pixel(bitmap, point, color);
    return;
  case PIX_FMT_RGB565:
    _pix_bitmap_rgb565_set_pixel(bitmap, point, color);
    return;
  case PIX_FMT_MONO:
    _pix_bitmap_mono_set_pixel(bitmap, point, color);
    return;
  }
}

void pix_bitmap_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                          pix_size_t size, pix_color_t color) {
  if (bitmap == NULL) {
    return;
  }
  if (bitmap->ops != NULL && bitmap->ops->fill_rect != NULL) {
    bitmap->ops->fill_rect(bitmap, origin, size, color);
    return;
  }

  // Clip to bitmap bounds - the per-format fill_rect()s below trust
  // origin/size are already valid, unlike pix_bitmap_set_pixel() (which
  // bounds-checks every call on its own).
  int32_t x0 = origin.x < 0 ? 0 : origin.x;
  int32_t y0 = origin.y < 0 ? 0 : origin.y;
  int32_t x1 = origin.x + (int32_t)size.w;
  int32_t y1 = origin.y + (int32_t)size.h;
  if (x1 > bitmap->size.w) {
    x1 = bitmap->size.w;
  }
  if (y1 > bitmap->size.h) {
    y1 = bitmap->size.h;
  }
  if (x0 >= x1 || y0 >= y1) {
    return; // Empty, or entirely outside the bitmap.
  }
  origin = (pix_point_t){.x = (int16_t)x0, .y = (int16_t)y0};
  size = (pix_size_t){.w = (uint16_t)(x1 - x0), .h = (uint16_t)(y1 - y0)};

  if (bitmap->data == NULL) {
    // No raw memory to bulk-write - an ops-only bitmap with no
    // ops->fill_rect of its own. Fall back to one pix_bitmap_set_pixel()
    // call at a time, which still routes through ops->set_pixel if that's
    // implemented - see pix_bitmap_ops_t's own doc.
    for (uint16_t dy = 0; dy < size.h; dy++) {
      for (uint16_t dx = 0; dx < size.w; dx++) {
        pix_point_t point = {
            .x = (int16_t)(origin.x + dx),
            .y = (int16_t)(origin.y + dy),
        };
        pix_bitmap_set_pixel(bitmap, point, color);
      }
    }
    return;
  }

  switch (bitmap->fmt) {
  case PIX_FMT_RGBA32:
    _pix_bitmap_rgba32_fill_rect(bitmap, origin, size, color);
    return;
  case PIX_FMT_RGB888:
    _pix_bitmap_rgb888_fill_rect(bitmap, origin, size, color);
    return;
  case PIX_FMT_RGB565:
    _pix_bitmap_rgb565_fill_rect(bitmap, origin, size, color);
    return;
  case PIX_FMT_MONO:
    _pix_bitmap_mono_fill_rect(bitmap, origin, size, color);
    return;
  }
}
