#include "private.h"
#include <picofuse/sys/debugf.h>

// @todo No 1-bit packing implemented yet - bit order (MSB/LSB-first per
// byte) and the set_pixel threshold for mapping a real color to "on"/"off"
// both still need deciding.
pix_color_t _pix_bitmap_mono_get_pixel(const pix_bitmap_t *bitmap,
                                       pix_point_t point) {
  (void)bitmap;
  (void)point;
  sys_debugf("pix", "_pix_bitmap_mono_get_pixel: not yet implemented");
  return PIX_COLOR_NONE;
}

void _pix_bitmap_mono_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                pix_color_t color) {
  (void)bitmap;
  (void)point;
  (void)color;
  sys_debugf("pix", "_pix_bitmap_mono_set_pixel: not yet implemented");
}

void _pix_bitmap_mono_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                pix_size_t size, pix_color_t color) {
  (void)bitmap;
  (void)origin;
  (void)size;
  (void)color;
  sys_debugf("pix", "_pix_bitmap_mono_fill_rect: not yet implemented");
}
