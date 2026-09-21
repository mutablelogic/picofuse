#include "posix.h"
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// FD <-> ctx
//
// fs_file_t.ctx just needs to carry a POSIX file descriptor across calls;
// unlike a volume, a file has no fixed-size inline storage to cast into, so
// the fd is stashed directly in the pointer. Stored as (fd + 1) so a valid
// fd 0 never collides with ctx == NULL ("no file open").

static int _fs_posix_fd(const fs_file_t *file) {
  return (int)(intptr_t)file->ctx - 1;
}

static void _fs_posix_set_fd(fs_file_t *file, int fd) {
  file->ctx = (void *)(intptr_t)(fd + 1);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

fs_file_t _fs_posix_file_create(fs_volume_t *volume, const char *path) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
  fs_file_t result = {0};

  char existing[PATH_MAX];
  char target[PATH_MAX];
  const char *real_path;
  if (_fs_posix_resolve(ctx, path, existing)) {
    struct stat st;
    if (stat(existing, &st) != 0 || !S_ISREG(st.st_mode)) {
      return result; // exists but isn't a regular file
    }
    real_path = existing;
  } else if (_fs_posix_resolve_new(ctx, path, target)) {
    real_path = target;
  } else {
    return result;
  }

  int fd = open(real_path, O_CREAT | O_TRUNC | O_RDWR, 0644);
  if (fd < 0) {
    return result;
  }

  result.volume = volume;
  result.dir = false;
  result.size = 0;
  result.pos = 0;
  _fs_posix_basename(real_path, result.name);
  _fs_posix_set_fd(&result, fd);
  return result;
}

fs_file_t _fs_posix_file_open(fs_volume_t *volume, const char *path,
                              bool write) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
  fs_file_t result = {0};

  char resolved[PATH_MAX];
  if (!_fs_posix_resolve(ctx, path, resolved)) {
    return result;
  }

  struct stat st;
  if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
    return result; // must already exist as a regular file
  }

  int fd = open(resolved, write ? O_RDWR : O_RDONLY);
  if (fd < 0) {
    return result;
  }

  result.volume = volume;
  result.dir = false;
  result.size = (size_t)st.st_size;
  result.pos = 0;
  _fs_posix_basename(resolved, result.name);
  _fs_posix_set_fd(&result, fd);
  return result;
}

bool _fs_posix_file_close(fs_file_t *file) {
  if (file == NULL || file->ctx == NULL) {
    return false;
  }
  int fd = _fs_posix_fd(file);
  file->ctx = NULL;
  return close(fd) == 0;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

size_t _fs_posix_file_read(fs_file_t *file, void *buffer, size_t size) {
  if (file == NULL || file->ctx == NULL || buffer == NULL || size == 0) {
    return 0;
  }
  ssize_t n = read(_fs_posix_fd(file), buffer, size);
  if (n <= 0) {
    return 0;
  }
  file->pos += (size_t)n;
  return (size_t)n;
}

size_t _fs_posix_file_write(fs_file_t *file, const void *buffer,
                            size_t size) {
  if (file == NULL || file->ctx == NULL || buffer == NULL || size == 0) {
    return 0;
  }
  ssize_t n = write(_fs_posix_fd(file), buffer, size);
  if (n <= 0) {
    return 0;
  }
  file->pos += (size_t)n;
  if (file->pos > file->size) {
    file->size = file->pos;
  }
  return (size_t)n;
}

bool _fs_posix_file_seek(fs_file_t *file, size_t offset) {
  if (file == NULL || file->ctx == NULL) {
    return false;
  }
  if (lseek(_fs_posix_fd(file), (off_t)offset, SEEK_SET) < 0) {
    return false;
  }
  file->pos = offset;
  return true;
}
