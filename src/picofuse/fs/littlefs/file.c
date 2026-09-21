#include "littlefs.h"
#include <picofuse/sys.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static const struct fs_volume_ops_t fs_lfs_file_ops;

///////////////////////////////////////////////////////////////////////////////
// BLOCK DEVICE (host image file, via stdio)
//
// Unlike flash.c's hw_block_t, a host FILE* supports arbitrary byte-level
// seeks, so read/prog can use the same small granularity as mem.c rather
// than being forced to whole-block transfers.

static int _fs_lfs_file_dev_read(const struct lfs_config *c, lfs_block_t block,
                                 lfs_off_t off, void *buffer,
                                 lfs_size_t size) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  FILE *f = (FILE *)ctx->storage;

  long offset = (long)block * (long)c->block_size + (long)off;
  if (fseek(f, offset, SEEK_SET) != 0) {
    return LFS_ERR_IO;
  }
  if (fread(buffer, 1, size, f) != size) {
    return LFS_ERR_IO;
  }
  return LFS_ERR_OK;
}

static int _fs_lfs_file_dev_prog(const struct lfs_config *c, lfs_block_t block,
                                 lfs_off_t off, const void *buffer,
                                 lfs_size_t size) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  FILE *f = (FILE *)ctx->storage;

  long offset = (long)block * (long)c->block_size + (long)off;
  if (fseek(f, offset, SEEK_SET) != 0) {
    return LFS_ERR_IO;
  }
  if (fwrite(buffer, 1, size, f) != size) {
    return LFS_ERR_IO;
  }
  return LFS_ERR_OK;
}

static int _fs_lfs_file_dev_erase(const struct lfs_config *c,
                                  lfs_block_t block) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  FILE *f = (FILE *)ctx->storage;

  long offset = (long)block * (long)c->block_size;
  if (fseek(f, offset, SEEK_SET) != 0) {
    return LFS_ERR_IO;
  }

  uint8_t erased[64];
  memset(erased, 0xFF, sizeof(erased));
  size_t remaining = c->block_size;
  while (remaining > 0) {
    size_t chunk = remaining < sizeof(erased) ? remaining : sizeof(erased);
    if (fwrite(erased, 1, chunk, f) != chunk) {
      return LFS_ERR_IO;
    }
    remaining -= chunk;
  }
  return LFS_ERR_OK;
}

static int _fs_lfs_file_dev_sync(const struct lfs_config *c) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  FILE *f = (FILE *)ctx->storage;
  return fflush(f) == 0 ? LFS_ERR_OK : LFS_ERR_IO;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

fs_volume_t *fs_vol_init_file(const char *path, size_t size) {
  if (path == NULL || path[0] == '\0') {
    return NULL;
  }

  size_t requested_blocks = (size + LFS_BLOCK_SIZE - 1) / LFS_BLOCK_SIZE;
  if (requested_blocks < 2) {
    requested_blocks = 2; // littlefs needs at least 2 blocks for its
                          // superblock metadata pair
  }
  size_t requested_size = requested_blocks * LFS_BLOCK_SIZE;

  // Open for read/write without truncating existing content; only create a
  // fresh (empty) file if one doesn't already exist at `path`.
  FILE *f = fopen(path, "r+b");
  if (f == NULL) {
    f = fopen(path, "w+b");
    if (f == NULL) {
      return NULL;
    }
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long current_size = ftell(f);
  if (current_size < 0) {
    fclose(f);
    return NULL;
  }

  // Expand a too-small (or freshly created, empty) file, filling the new
  // region with the erased pattern; never shrink an existing, larger image.
  size_t file_size = (size_t)current_size;
  if (file_size < requested_size) {
    uint8_t erased[64];
    memset(erased, 0xFF, sizeof(erased));
    size_t remaining = requested_size - file_size;
    while (remaining > 0) {
      size_t chunk = remaining < sizeof(erased) ? remaining : sizeof(erased);
      if (fwrite(erased, 1, chunk, f) != chunk) {
        fclose(f);
        return NULL;
      }
      remaining -= chunk;
    }
    file_size = requested_size;
  }

  size_t block_count = file_size / LFS_BLOCK_SIZE;
  if (block_count < 2) {
    fclose(f);
    return NULL;
  }

  fs_volume_t *volume = _fs_vol_alloc();
  if (volume == NULL) {
    fclose(f);
    return NULL;
  }

  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  memset(ctx, 0, sizeof(*ctx));
  ctx->storage = f;
  ctx->storage_size = block_count * LFS_BLOCK_SIZE;
  ctx->medium = FS_LFS_MEDIUM_FILE;
  ctx->cfg = (struct lfs_config){
      .context = ctx,
      .read = _fs_lfs_file_dev_read,
      .prog = _fs_lfs_file_dev_prog,
      .erase = _fs_lfs_file_dev_erase,
      .sync = _fs_lfs_file_dev_sync,
      .read_size = LFS_READ_SIZE,
      .prog_size = LFS_PROG_SIZE,
      .block_size = LFS_BLOCK_SIZE,
      .block_count = (lfs_size_t)block_count,
      .block_cycles = 500,
      .cache_size = LFS_CACHE_SIZE,
      .lookahead_size = LFS_LOOKAHEAD_SIZE,
  };

  if (lfs_mount(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK) {
    if (lfs_format(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK ||
        lfs_mount(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK) {
      fclose(f);
      _fs_vol_free(volume);
      return NULL;
    }
  }

  volume->ops = &fs_lfs_file_ops;
  return volume;
}

static void _fs_lfs_file_vol_deinit(fs_volume_t *volume) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  lfs_unmount(&ctx->lfs);
  fclose((FILE *)ctx->storage);
  memset(ctx, 0, sizeof(*ctx));
}

///////////////////////////////////////////////////////////////////////////////
// OPS TABLE
//
// Every method besides `deinit` is backend-agnostic - see ops.c.

static const struct fs_volume_ops_t fs_lfs_file_ops = {
    .deinit = _fs_lfs_file_vol_deinit,
    .size = _fs_lfs_vol_size,
    .readdir = _fs_lfs_vol_readdir,
    .stat = _fs_lfs_vol_stat,
    .mkdir = _fs_lfs_vol_mkdir,
    .remove = _fs_lfs_vol_remove,
    .move = _fs_lfs_vol_move,
    .file_create = _fs_lfs_file_create,
    .file_open = _fs_lfs_file_open,
    .file_close = _fs_lfs_file_close,
    .file_read = _fs_lfs_file_read,
    .file_write = _fs_lfs_file_write,
    .file_seek = _fs_lfs_file_seek,
};
