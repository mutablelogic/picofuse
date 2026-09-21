#include <picofuse/fs.h>

/** Stub implementation: no host directory-backed volumes on this platform. */
fs_volume_t *fs_vol_init_path(const char *path) {
  (void)path;
  return NULL;
}
