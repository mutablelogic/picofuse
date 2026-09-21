#include "../private.h"

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

fs_file_t fs_file_create(fs_volume_t *volume, const char *path) {
  if (volume == NULL || volume->ops == NULL ||
      volume->ops->file_create == NULL) {
    return (fs_file_t){0};
  }
  return volume->ops->file_create(volume, path);
}

fs_file_t fs_file_open(fs_volume_t *volume, const char *path, bool write) {
  if (volume == NULL || volume->ops == NULL ||
      volume->ops->file_open == NULL) {
    return (fs_file_t){0};
  }
  return volume->ops->file_open(volume, path, write);
}

bool fs_file_close(fs_file_t *file) {
  if (file == NULL || file->volume == NULL || file->volume->ops == NULL ||
      file->volume->ops->file_close == NULL) {
    return false;
  }
  return file->volume->ops->file_close(file);
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

size_t fs_file_read(fs_file_t *file, void *buffer, size_t size) {
  if (file == NULL || file->volume == NULL || file->volume->ops == NULL ||
      file->volume->ops->file_read == NULL) {
    return 0;
  }
  return file->volume->ops->file_read(file, buffer, size);
}

size_t fs_file_write(fs_file_t *file, const void *buffer, size_t size) {
  if (file == NULL || file->volume == NULL || file->volume->ops == NULL ||
      file->volume->ops->file_write == NULL) {
    return 0;
  }
  return file->volume->ops->file_write(file, buffer, size);
}

bool fs_file_seek(fs_file_t *file, size_t offset) {
  if (file == NULL || file->volume == NULL || file->volume->ops == NULL ||
      file->volume->ops->file_seek == NULL) {
    return false;
  }
  return file->volume->ops->file_seek(file, offset);
}
