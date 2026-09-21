#include <picofuse/fs.h>

/** Stub implementation: no host file-backed storage on this platform. */
fs_volume_t *fs_vol_init_file(const char *path, size_t size) {
  (void)path;
  (void)size;
  return NULL;
}
