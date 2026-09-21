#include "../private.h"

// A slot is free iff its ops pointer is NULL. Every backend that
// successfully allocates a slot must set `ops` before returning the volume
// to the caller.
static fs_volume_t _fs_volume[FS_VOLUME_MAX] = {0};

fs_volume_t *_fs_vol_alloc(void) {
  for (size_t i = 0; i < FS_VOLUME_MAX; i++) {
    if (_fs_volume[i].ops == NULL) {
      return &_fs_volume[i];
    }
  }
  return NULL;
}

void _fs_vol_free(fs_volume_t *volume) {
  if (volume == NULL) {
    return;
  }
  ptrdiff_t idx = volume - _fs_volume;
  if (idx < 0 || (size_t)idx >= FS_VOLUME_MAX) {
    return;
  }
  volume->ops = NULL;
}
