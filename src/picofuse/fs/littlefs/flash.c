#include "littlefs.h"
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static const struct fs_volume_ops_t fs_lfs_flash_ops;

///////////////////////////////////////////////////////////////////////////////
// BLOCK DEVICE (flash-backed, via hw/flash.h's generic hw_block_t)
//
// hw_block_read()/hw_block_write() only ever transfer one whole block at a
// time - there's no partial-block offset/size, unlike mem.c's raw buffer
// access. Setting read_size == prog_size == cache_size == block_size below
// forces littlefs's own read/prog cache to always request a full block at
// off=0 (see lfs_bd_read/lfs_bd_prog in lfs.c), so that constraint is
// satisfied by construction and off/size can be ignored here.

static int _fs_lfs_flash_read(const struct lfs_config *c, lfs_block_t block,
                              lfs_off_t off, void *buffer, lfs_size_t size) {
  (void)off;
  (void)size;
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  hw_block_t *hwblock = (hw_block_t *)ctx->storage;
  return hw_block_read(hwblock, block, buffer) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int _fs_lfs_flash_prog(const struct lfs_config *c, lfs_block_t block,
                              lfs_off_t off, const void *buffer,
                              lfs_size_t size) {
  (void)off;
  (void)size;
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  hw_block_t *hwblock = (hw_block_t *)ctx->storage;
  return hw_block_write(hwblock, block, buffer) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int _fs_lfs_flash_erase(const struct lfs_config *c, lfs_block_t block) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)c->context;
  hw_block_t *hwblock = (hw_block_t *)ctx->storage;
  return hw_block_erase(hwblock, block) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int _fs_lfs_flash_sync(const struct lfs_config *c) {
  (void)c;
  return LFS_ERR_OK; // hw_block_write()/hw_block_erase() are synchronous
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

fs_volume_t *fs_vol_init_flash(size_t size) {
  hw_block_t *hwblock = hw_block_flash_init(size);
  if (hwblock == NULL) {
    return NULL;
  }

  size_t block_size = hw_block_size(hwblock);
  size_t block_count = hw_block_count(hwblock);
  if (block_size == 0 || block_count < 2) {
    hw_block_deinit(hwblock);
    return NULL; // littlefs needs at least 2 blocks for its metadata pair
  }

  fs_volume_t *volume = _fs_vol_alloc();
  if (volume == NULL) {
    hw_block_deinit(hwblock);
    return NULL;
  }

  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  memset(ctx, 0, sizeof(*ctx));
  ctx->storage = hwblock;
  ctx->storage_size = block_size * block_count;
  ctx->medium = FS_LFS_MEDIUM_FLASH;
  ctx->cfg = (struct lfs_config){
      .context = ctx,
      .read = _fs_lfs_flash_read,
      .prog = _fs_lfs_flash_prog,
      .erase = _fs_lfs_flash_erase,
      .sync = _fs_lfs_flash_sync,
      .read_size = (lfs_size_t)block_size,
      .prog_size = (lfs_size_t)block_size,
      .block_size = (lfs_size_t)block_size,
      .block_count = (lfs_size_t)block_count,
      .block_cycles = 500,
      .cache_size = (lfs_size_t)block_size,
      .lookahead_size = LFS_LOOKAHEAD_SIZE,
  };

  if (lfs_mount(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK) {
    if (lfs_format(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK ||
        lfs_mount(&ctx->lfs, &ctx->cfg) != LFS_ERR_OK) {
      hw_block_deinit(hwblock);
      _fs_vol_free(volume);
      return NULL;
    }
  }

  volume->ops = &fs_lfs_flash_ops;
  return volume;
}

static void _fs_lfs_flash_vol_deinit(fs_volume_t *volume) {
  fs_lfs_ctx_t *ctx = (fs_lfs_ctx_t *)volume->ctx;
  lfs_unmount(&ctx->lfs);
  hw_block_deinit((hw_block_t *)ctx->storage);
  memset(ctx, 0, sizeof(*ctx));
}

///////////////////////////////////////////////////////////////////////////////
// OPS TABLE
//
// Every method besides `deinit` is backend-agnostic - see ops.c.

static const struct fs_volume_ops_t fs_lfs_flash_ops = {
    .deinit = _fs_lfs_flash_vol_deinit,
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
