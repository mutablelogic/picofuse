#include "../private.h"

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

void fs_vol_deinit(fs_volume_t *volume) {
  if (volume == NULL) {
    return;
  }
  if (volume->ops != NULL && volume->ops->deinit != NULL) {
    volume->ops->deinit(volume);
  }
  _fs_vol_free(volume);
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

size_t fs_vol_size(fs_volume_t *volume, size_t *free) {
  if (volume == NULL || volume->ops == NULL || volume->ops->size == NULL) {
    if (free != NULL) {
      *free = 0;
    }
    return 0;
  }
  return volume->ops->size(volume, free);
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool fs_vol_readdir(fs_volume_t *volume, const char *path,
                    fs_file_t *iterator) {
  if (volume == NULL || volume->ops == NULL || volume->ops->readdir == NULL) {
    return false;
  }
  return volume->ops->readdir(volume, path, iterator);
}

fs_file_t fs_vol_stat(fs_volume_t *volume, const char *path) {
  if (volume == NULL || volume->ops == NULL || volume->ops->stat == NULL) {
    return (fs_file_t){0};
  }
  return volume->ops->stat(volume, path);
}

bool fs_vol_mkdir(fs_volume_t *volume, const char *path) {
  if (volume == NULL || volume->ops == NULL || volume->ops->mkdir == NULL) {
    return false;
  }
  return volume->ops->mkdir(volume, path);
}

bool fs_vol_remove(fs_volume_t *volume, const char *path) {
  if (volume == NULL || volume->ops == NULL || volume->ops->remove == NULL) {
    return false;
  }
  return volume->ops->remove(volume, path);
}

bool fs_vol_move(fs_volume_t *volume, const char *old_path,
                 const char *new_path) {
  if (volume == NULL || volume->ops == NULL || volume->ops->move == NULL) {
    return false;
  }
  return volume->ops->move(volume, old_path, new_path);
}
