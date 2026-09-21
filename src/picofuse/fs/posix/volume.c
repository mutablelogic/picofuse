#include "posix.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static const struct fs_volume_ops_t fs_posix_ops;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

fs_volume_t *fs_vol_init_path(const char *path) {
  if (path == NULL) {
    return NULL;
  }

  struct stat st;
  if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
    return NULL;
  }

  // realpath() requires a destination buffer of at least PATH_MAX bytes
  // when non-NULL is passed; our own, smaller cap is enforced below.
  char resolved[PATH_MAX];
  if (realpath(path, resolved) == NULL) {
    return NULL;
  }

  size_t len = strlen(resolved);
  if (len == 0 || len > FS_PATH_MAX) {
    return NULL;
  }

  fs_volume_t *volume = _fs_vol_alloc();
  if (volume == NULL) {
    return NULL;
  }

  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
  memcpy(ctx->root, resolved, len + 1);
  ctx->root_len = len;

  volume->ops = &fs_posix_ops;
  return volume;
}

static void _fs_posix_vol_deinit(fs_volume_t *volume) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
  memset(ctx, 0, sizeof(*ctx));
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

static size_t _fs_posix_vol_size(fs_volume_t *volume, size_t *free) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;

  struct statvfs vfs;
  if (statvfs(ctx->root, &vfs) != 0) {
    if (free != NULL) {
      *free = 0;
    }
    return 0;
  }

  if (free != NULL) {
    *free = (size_t)vfs.f_bavail * (size_t)vfs.f_frsize;
  }
  return (size_t)vfs.f_blocks * (size_t)vfs.f_frsize;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

static bool _fs_posix_vol_readdir(fs_volume_t *volume, const char *path,
                                  fs_file_t *iterator) {
  if (iterator == NULL) {
    return false;
  }

  DIR *dir = (DIR *)iterator->ctx;
  if (dir == NULL) {
    fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
    char resolved[PATH_MAX];
    if (!_fs_posix_resolve(ctx, path, resolved)) {
      return false;
    }
    dir = opendir(resolved);
    if (dir == NULL) {
      return false;
    }
    iterator->volume = volume;
    iterator->ctx = dir;
  }

  for (;;) {
    struct dirent *entry = readdir(dir);
    if (entry == NULL) {
      closedir(dir);
      iterator->ctx = NULL;
      iterator->name[0] = '\0';
      return false;
    }
    if (entry->d_name[0] == '.') {
      continue; // skip hidden entries, including "." and ".."
    }

    size_t name_len = strlen(entry->d_name);
    if (name_len > FS_PATH_MAX) {
      continue; // can't represent this entry, skip it
    }

    struct stat st;
    bool have_stat = fstatat(dirfd(dir), entry->d_name, &st, 0) == 0;
    memcpy(iterator->name, entry->d_name, name_len + 1);
    iterator->dir = have_stat ? S_ISDIR(st.st_mode) : entry->d_type == DT_DIR;
    iterator->size =
        (have_stat && S_ISREG(st.st_mode)) ? (size_t)st.st_size : 0;
    iterator->pos = 0;
    return true;
  }
}

static fs_file_t _fs_posix_vol_stat(fs_volume_t *volume, const char *path) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
  fs_file_t result = {0};
  char resolved[PATH_MAX];

  if (!_fs_posix_resolve(ctx, path, resolved)) {
    return result; // not found (or, if it exists, escapes root)
  }

  struct stat st;
  if (stat(resolved, &st) != 0) {
    return result;
  }

  result.volume = volume;
  result.dir = S_ISDIR(st.st_mode);
  result.size = S_ISREG(st.st_mode) ? (size_t)st.st_size : 0;
  result.pos = 0;
  result.ctx = NULL;
  _fs_posix_basename(resolved, result.name);
  return result;
}

static bool _fs_posix_vol_mkdir(fs_volume_t *volume, const char *path) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
  char existing[PATH_MAX];
  if (_fs_posix_resolve(ctx, path, existing)) {
    struct stat st;
    return stat(existing, &st) == 0 && S_ISDIR(st.st_mode);
  }

  char target[PATH_MAX];
  if (!_fs_posix_resolve_new(ctx, path, target)) {
    return false;
  }
  return mkdir(target, 0777) == 0;
}

static bool _fs_posix_vol_remove(fs_volume_t *volume, const char *path) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;
  char resolved[PATH_MAX];
  if (!_fs_posix_resolve(ctx, path, resolved)) {
    return false;
  }
  if (strcmp(resolved, ctx->root) == 0) {
    return false; // never remove the volume root itself
  }

  struct stat st;
  if (stat(resolved, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode) ? rmdir(resolved) == 0 : unlink(resolved) == 0;
}

static bool _fs_posix_vol_move(fs_volume_t *volume, const char *old_path,
                               const char *new_path) {
  fs_posix_ctx_t *ctx = (fs_posix_ctx_t *)volume->ctx;

  char old_resolved[PATH_MAX];
  if (!_fs_posix_resolve(ctx, old_path, old_resolved)) {
    return false;
  }
  if (strcmp(old_resolved, ctx->root) == 0) {
    return false; // never move the volume root itself
  }

  // The destination may already exist - matching rename(2)/littlefs
  // semantics, rename() itself atomically replaces a same-type target
  // (rejecting on a type mismatch, or a non-empty destination directory)
  // - or it may not, in which case only its parent must exist.
  char new_target[PATH_MAX];
  if (!_fs_posix_resolve(ctx, new_path, new_target) &&
      !_fs_posix_resolve_new(ctx, new_path, new_target)) {
    return false;
  }
  if (strcmp(new_target, ctx->root) == 0) {
    return false; // never overwrite the volume root itself
  }

  return rename(old_resolved, new_target) == 0;
}

///////////////////////////////////////////////////////////////////////////////
// OPS TABLE

static const struct fs_volume_ops_t fs_posix_ops = {
    .deinit = _fs_posix_vol_deinit,
    .size = _fs_posix_vol_size,
    .readdir = _fs_posix_vol_readdir,
    .stat = _fs_posix_vol_stat,
    .mkdir = _fs_posix_vol_mkdir,
    .remove = _fs_posix_vol_remove,
    .move = _fs_posix_vol_move,
    .file_create = _fs_posix_file_create,
    .file_open = _fs_posix_file_open,
    .file_close = _fs_posix_file_close,
    .file_read = _fs_posix_file_read,
    .file_write = _fs_posix_file_write,
    .file_seek = _fs_posix_file_seek,
};
