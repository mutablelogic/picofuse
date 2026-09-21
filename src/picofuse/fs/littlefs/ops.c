#include "littlefs.h"
#include <picofuse/sys.h>
#include <string.h>

// Backend-agnostic littlefs volume/file operations, shared by every backend
// (mem.c, flash.c, ...). None of this touches `ctx->storage` directly - it
// only ever goes through ctx->lfs/ctx->cfg, so it's identical regardless of
// which block device backs the mounted filesystem. Each backend supplies
// its own block-device callbacks, fs_vol_init_*(), and `deinit` (since
// releasing `ctx->storage` is backend-specific - see littlefs.h).

// Handle for an open file, heap-allocated (via sys_malloc) and stashed in
// fs_file_t.ctx - unlike a POSIX fd, an lfs_file_t doesn't fit in a pointer.
typedef struct {
  lfs_file_t file;
  // lfs_file_write() LFS_ASSERT()s (aborting the process) rather than
  // failing gracefully when called on a file that wasn't opened with write
  // access - unlike POSIX write(), which just reports EBADF. Track it
  // ourselves so a read-only handle's write() can fail cleanly instead.
  bool writable;
} fs_lfs_file_ctx_t;

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

size_t _fs_lfs_vol_size(fs_volume_t *volume, size_t *free) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  size_t block_size = ctx->cfg.block_size;
  size_t block_count = ctx->cfg.block_count;

  if (free != NULL) {
    lfs_ssize_t used = lfs_fs_size(&ctx->lfs);
    *free = (used < 0 || (size_t)used > block_count)
                ? 0
                : (block_count - (size_t)used) * block_size;
  }
  return block_count * block_size;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool _fs_lfs_vol_readdir(fs_volume_t *volume, const char *path,
                         fs_file_t *iterator) {
  if (iterator == NULL) {
    return false;
  }
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;

  lfs_dir_t *dir = (lfs_dir_t *)iterator->ctx;
  if (dir == NULL) {
    char resolved[FS_PATH_MAX + 1];
    if (!_fs_lfs_resolve(ctx, path, resolved)) {
      return false;
    }
    dir = sys_malloc(sizeof(*dir));
    if (dir == NULL) {
      return false;
    }
    if (lfs_dir_open(&ctx->lfs, dir, resolved) != LFS_ERR_OK) {
      sys_free(dir);
      return false;
    }
    iterator->volume = volume;
    iterator->ctx = dir;
  }

  struct lfs_info info;
  for (;;) {
    int res = lfs_dir_read(&ctx->lfs, dir, &info);
    if (res <= 0) {
      lfs_dir_close(&ctx->lfs, dir);
      sys_free(dir);
      iterator->ctx = NULL;
      iterator->name[0] = '\0';
      return false;
    }
    if (info.name[0] == '.') {
      continue; // skip hidden entries, including "." and ".."
    }

    size_t name_len = strlen(info.name);
    if (name_len > FS_PATH_MAX) {
      continue; // can't represent this entry, skip it
    }

    memcpy(iterator->name, info.name, name_len + 1);
    iterator->dir = info.type == LFS_TYPE_DIR;
    iterator->size = info.type == LFS_TYPE_REG ? (size_t)info.size : 0;
    iterator->pos = 0;
    return true;
  }
}

fs_file_t _fs_lfs_vol_stat(fs_volume_t *volume, const char *path) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  fs_file_t result = {0};

  char resolved[FS_PATH_MAX + 1];
  if (!_fs_lfs_resolve(ctx, path, resolved)) {
    return result; // not found, or bad/escaping syntax
  }

  struct lfs_info info;
  if (lfs_stat(&ctx->lfs, resolved, &info) != LFS_ERR_OK) {
    return result; // not found
  }

  result.volume = volume;
  result.dir = info.type == LFS_TYPE_DIR;
  result.size = info.type == LFS_TYPE_REG ? (size_t)info.size : 0;
  result.pos = 0;
  result.ctx = NULL;
  _fs_lfs_basename(resolved, result.name);
  return result;
}

bool _fs_lfs_vol_mkdir(fs_volume_t *volume, const char *path) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;

  char resolved[FS_PATH_MAX + 1];
  if (_fs_lfs_resolve(ctx, path, resolved)) {
    struct lfs_info info;
    if (lfs_stat(&ctx->lfs, resolved, &info) == LFS_ERR_OK) {
      return info.type == LFS_TYPE_DIR;
    }
  }

  char target[FS_PATH_MAX + 1];
  if (!_fs_lfs_resolve_new(ctx, path, target)) {
    return false;
  }
  return lfs_mkdir(&ctx->lfs, target) == LFS_ERR_OK;
}

bool _fs_lfs_vol_remove(fs_volume_t *volume, const char *path) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;

  char resolved[FS_PATH_MAX + 1];
  if (!_fs_lfs_resolve(ctx, path, resolved)) {
    return false;
  }
  if (_fs_lfs_is_root(resolved)) {
    return false; // never remove the volume root itself
  }
  return lfs_remove(&ctx->lfs, resolved) == LFS_ERR_OK;
}

bool _fs_lfs_vol_move(fs_volume_t *volume, const char *old_path,
                      const char *new_path) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;

  char old_resolved[FS_PATH_MAX + 1];
  if (!_fs_lfs_resolve(ctx, old_path, old_resolved)) {
    return false;
  }
  if (_fs_lfs_is_root(old_resolved)) {
    return false; // never move the volume root itself
  }

  char new_target[FS_PATH_MAX + 1];
  if (!_fs_lfs_resolve(ctx, new_path, new_target) &&
      !_fs_lfs_resolve_new(ctx, new_path, new_target)) {
    return false;
  }
  if (_fs_lfs_is_root(new_target)) {
    return false; // never overwrite the volume root itself
  }

  return lfs_rename(&ctx->lfs, old_resolved, new_target) == LFS_ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// FILE LIFECYCLE

fs_file_t _fs_lfs_file_create(fs_volume_t *volume, const char *path) {
  fs_file_t result = {0};
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;

  char target[FS_PATH_MAX + 1];
  struct lfs_info info;
  if (_fs_lfs_resolve(ctx, path, target) &&
      lfs_stat(&ctx->lfs, target, &info) == LFS_ERR_OK) {
    if (info.type != LFS_TYPE_REG) {
      return result; // exists but isn't a regular file
    }
  } else if (!_fs_lfs_resolve_new(ctx, path, target)) {
    return result;
  }

  fs_lfs_file_ctx_t *fctx = sys_malloc(sizeof(*fctx));
  if (fctx == NULL) {
    return result;
  }

  int flags = LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC;
  if (lfs_file_open(&ctx->lfs, &fctx->file, target, flags) != LFS_ERR_OK) {
    sys_free(fctx);
    return result;
  }
  fctx->writable = true;

  result.volume = volume;
  result.dir = false;
  result.size = 0;
  result.pos = 0;
  _fs_lfs_basename(target, result.name);
  result.ctx = fctx;
  return result;
}

fs_file_t _fs_lfs_file_open(fs_volume_t *volume, const char *path,
                            bool write) {
  fs_file_t result = {0};
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;

  char resolved[FS_PATH_MAX + 1];
  if (!_fs_lfs_resolve(ctx, path, resolved)) {
    return result;
  }

  struct lfs_info info;
  if (lfs_stat(&ctx->lfs, resolved, &info) != LFS_ERR_OK ||
      info.type != LFS_TYPE_REG) {
    return result; // must already exist as a regular file
  }

  fs_lfs_file_ctx_t *fctx = sys_malloc(sizeof(*fctx));
  if (fctx == NULL) {
    return result;
  }

  int flags = write ? LFS_O_RDWR : LFS_O_RDONLY;
  if (lfs_file_open(&ctx->lfs, &fctx->file, resolved, flags) != LFS_ERR_OK) {
    sys_free(fctx);
    return result;
  }
  fctx->writable = write;

  result.volume = volume;
  result.dir = false;
  result.size = (size_t)info.size;
  result.pos = 0;
  _fs_lfs_basename(resolved, result.name);
  result.ctx = fctx;
  return result;
}

bool _fs_lfs_file_close(fs_file_t *file) {
  if (file == NULL || file->ctx == NULL || file->volume == NULL) {
    return false;
  }
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)file->volume->ctx;
  fs_lfs_file_ctx_t *fctx = (fs_lfs_file_ctx_t *)file->ctx;

  int res = lfs_file_close(&ctx->lfs, &fctx->file);
  sys_free(fctx);
  file->ctx = NULL;
  return res == LFS_ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// FILE METHODS

size_t _fs_lfs_file_read(fs_file_t *file, void *buffer, size_t size) {
  if (file == NULL || file->ctx == NULL || file->volume == NULL ||
      buffer == NULL || size == 0) {
    return 0;
  }
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)file->volume->ctx;
  fs_lfs_file_ctx_t *fctx = (fs_lfs_file_ctx_t *)file->ctx;

  lfs_ssize_t n = lfs_file_read(&ctx->lfs, &fctx->file, buffer,
                                (lfs_size_t)size);
  if (n <= 0) {
    return 0;
  }
  file->pos += (size_t)n;
  return (size_t)n;
}

size_t _fs_lfs_file_write(fs_file_t *file, const void *buffer, size_t size) {
  if (file == NULL || file->ctx == NULL || file->volume == NULL ||
      buffer == NULL || size == 0) {
    return 0;
  }
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)file->volume->ctx;
  fs_lfs_file_ctx_t *fctx = (fs_lfs_file_ctx_t *)file->ctx;
  if (!fctx->writable) {
    return 0; // lfs_file_write() would otherwise abort via LFS_ASSERT()
  }

  lfs_ssize_t n = lfs_file_write(&ctx->lfs, &fctx->file, buffer,
                                 (lfs_size_t)size);
  if (n <= 0) {
    return 0;
  }
  file->pos += (size_t)n;
  if (file->pos > file->size) {
    file->size = file->pos;
  }
  return (size_t)n;
}

bool _fs_lfs_file_seek(fs_file_t *file, size_t offset) {
  if (file == NULL || file->ctx == NULL || file->volume == NULL) {
    return false;
  }
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)file->volume->ctx;
  fs_lfs_file_ctx_t *fctx = (fs_lfs_file_ctx_t *)file->ctx;

  lfs_soff_t res =
      lfs_file_seek(&ctx->lfs, &fctx->file, (lfs_soff_t)offset, LFS_SEEK_SET);
  if (res < 0) {
    return false;
  }
  file->pos = (size_t)res;
  return true;
}
