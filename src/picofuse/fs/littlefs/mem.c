#include "littlefs.h"
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static const struct fs_volume_ops_t fs_lfs_mem_ops;

///////////////////////////////////////////////////////////////////////////////
// BLOCK DEVICE (in-memory storage)

static int _fs_lfs_mem_read(const struct lfs_config *c, lfs_block_t block,
                            lfs_off_t off, void *buffer, lfs_size_t size) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  const uint8_t *src = (const uint8_t *)ctx->storage +
                       (size_t)block * c->block_size + off;
  memcpy(buffer, src, size);
  return LFS_ERR_OK;
}

static int _fs_lfs_mem_prog(const struct lfs_config *c, lfs_block_t block,
                            lfs_off_t off, const void *buffer,
                            lfs_size_t size) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  uint8_t *dst = (uint8_t *)ctx->storage + (size_t)block * c->block_size + off;
  memcpy(dst, buffer, size);
  return LFS_ERR_OK;
}

static int _fs_lfs_mem_erase(const struct lfs_config *c, lfs_block_t block) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  uint8_t *dst = (uint8_t *)ctx->storage + (size_t)block * c->block_size;
  memset(dst, 0xFF, c->block_size);
  return LFS_ERR_OK;
}

static int _fs_lfs_mem_sync(const struct lfs_config *c) {
  (void)c;
  return LFS_ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

fs_volume_t *fs_vol_init_memory(sys_mem_arena_t *arena, size_t size) {
  if (arena != NULL) {
    return NULL; // arena-backed memory volumes are not yet supported
  }

  size_t block_count = (size + LFS_BLOCK_SIZE - 1) / LFS_BLOCK_SIZE;
  if (block_count < 2) {
    block_count = 2; // littlefs needs at least 2 blocks for its superblock
                      // metadata pair - fewer and lfs_format() fails outright
  }
  size_t storage_size = block_count * LFS_BLOCK_SIZE;

  void *storage = sys_malloc(storage_size);
  if (storage == NULL) {
    return NULL;
  }

  fs_volume_t *volume = _fs_vol_alloc();
  if (volume == NULL) {
    sys_free(storage);
    return NULL;
  }

  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  memset(ctx, 0, sizeof(*ctx));
  ctx->storage = storage;
  ctx->storage_size = storage_size;
  ctx->medium = FS_LFS_MEDIUM_MEMORY;
  ctx->cfg = (struct lfs_config){
      .context = ctx,
      .read = _fs_lfs_mem_read,
      .prog = _fs_lfs_mem_prog,
      .erase = _fs_lfs_mem_erase,
      .sync = _fs_lfs_mem_sync,
      .read_size = LFS_READ_SIZE,
      .prog_size = LFS_PROG_SIZE,
      .block_size = LFS_BLOCK_SIZE,
      .block_count = (lfs_size_t)block_count,
      .block_cycles = 500,
      .cache_size = LFS_CACHE_SIZE,
      .lookahead_size = LFS_LOOKAHEAD_SIZE,
  };

  // Always format rather than trying to mount first: sys_malloc() gives no
  // guarantee the buffer is genuinely fresh - sys_free() reuses buffers via
  // the arena's free list, so a same-sized allocation can come back holding
  // a *previous* volume's still-valid littlefs image, and lfs_mount() would
  // silently "succeed" on it instead of returning the empty volume this API
  // promises.
  if (lfs_format(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK ||
      lfs_mount(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK) {
    sys_free(storage);
    _fs_vol_free(volume);
    return NULL;
  }

  volume->ops = &fs_lfs_mem_ops;
  return volume;
}

static void _fs_lfs_mem_vol_deinit(fs_volume_t *volume) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  lfs_unmount(&ctx->lfs);
  sys_free(ctx->storage);
  memset(ctx, 0, sizeof(*ctx));
}

///////////////////////////////////////////////////////////////////////////////
// OPS TABLE
//
// Every method besides `deinit` is backend-agnostic - see ops.c.

static const struct fs_volume_ops_t fs_lfs_mem_ops = {
    .deinit = _fs_lfs_mem_vol_deinit,
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
