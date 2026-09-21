#include <picofuse/fs.h>

/** Stub implementation: no flash-backed storage on this platform. */
fs_volume_t *fs_vol_init_flash(size_t size) {
  (void)size;
  return NULL;
}
