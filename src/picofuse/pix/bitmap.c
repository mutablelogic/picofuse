#include "private.h"

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static inline bool _pix_bitmap_in_bounds(const pix_bitmap_t *bitmap,
                                         pix_point_t point) {
  return bitmap != NULL && point.x >= 0 && point.y >= 0 &&
         (uint16_t)point.x < bitmap->size.w &&
         (uint16_t)point.y < bitmap->size.h;
}

// General (non-axis-aligned) case for pix_bitmap_draw_line() - a plain
// integer Bresenham walk, one pix_bitmap_set_pixel() call per point. There's
// no bulk-write shortcut here the way fill_rect() has memset(): a diagonal
// line's pixels aren't contiguous in memory, so this is already about as
// fast as the direct-memory path gets - set_pixel() itself already carries
// the bounds-check/ops-dispatch/blend logic, no need to duplicate any of it
// here.
static void _pix_bitmap_draw_line(pix_bitmap_t *bitmap, pix_point_t a,
                                  pix_point_t b, pix_color_t color) {
  int32_t x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
  int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int32_t sx = x0 < x1 ? 1 : -1;
  int32_t dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
  int32_t sy = y0 < y1 ? 1 : -1;
  int32_t error = dx + dy;

  for (;;) {
    pix_bitmap_set_pixel(bitmap, (pix_point_t){.x = (int16_t)x0, .y = (int16_t)y0},
                         color);
    if (x0 == x1 && y0 == y1) {
      return;
    }
    int32_t e2 = 2 * error;
    if (e2 >= dy) {
      error += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      error += dx;
      y0 += sy;
    }
  }
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

void pix_bitmap_draw_line(pix_bitmap_t *bitmap, pix_point_t a, pix_point_t b,
                          pix_color_t color) {
  if (bitmap == NULL) {
    return;
  }
  if (bitmap->ops != NULL && bitmap->ops->draw_line != NULL) {
    bitmap->ops->draw_line(bitmap, a, b, color);
    return;
  }

  // Horizontal and vertical lines are each just a one-pixel-thick rect -
  // delegate to fill_rect() rather than re-deriving its memset()/blend
  // logic here. A single point (a == b) matches the horizontal case below
  // and draws correctly as a 1x1 fill.
  if (a.y == b.y) {
    int16_t x0 = a.x < b.x ? a.x : b.x;
    int16_t x1 = a.x < b.x ? b.x : a.x;
    pix_bitmap_fill_rect(bitmap, (pix_point_t){.x = x0, .y = a.y},
                         (pix_size_t){.w = (uint16_t)(x1 - x0 + 1), .h = 1},
                         color);
    return;
  }
  if (a.x == b.x) {
    int16_t y0 = a.y < b.y ? a.y : b.y;
    int16_t y1 = a.y < b.y ? b.y : a.y;
    pix_bitmap_fill_rect(bitmap, (pix_point_t){.x = a.x, .y = y0},
                         (pix_size_t){.w = 1, .h = (uint16_t)(y1 - y0 + 1)},
                         color);
    return;
  }

  _pix_bitmap_draw_line(bitmap, a, b, color);
}

void pix_bitmap_set_op(pix_bitmap_t *bitmap, pix_op_t op) {
  if (bitmap == NULL) {
    return;
  }
  bitmap->op = op;
}
