#pragma once
#include <picofuse/fs.h>
#include <stddef.h>

// Size of the inline backend-private storage block embedded in every
// fs_volume_t (see `ctx` below). Must be large enough to hold the largest
// backend context in use (see the _Static_assert in littlefs.h).
#ifndef FS_VOLUME_CTX_SIZE
#define FS_VOLUME_CTX_SIZE 512u
#endif

// Per-volume method table.
struct fs_volume_ops_t {
  void (*deinit)(fs_volume_t *volume);
  size_t (*size)(fs_volume_t *volume, size_t *free);
  bool (*readdir)(fs_volume_t *volume, const char *path, fs_file_t *iterator);
  fs_file_t (*stat)(fs_volume_t *volume, const char *path);
  bool (*mkdir)(fs_volume_t *volume, const char *path);
  bool (*remove)(fs_volume_t *volume, const char *path);
  bool (*move)(fs_volume_t *volume, const char *old_path, const char *new_path);

  fs_file_t (*file_create)(fs_volume_t *volume, const char *path);
  fs_file_t (*file_open)(fs_volume_t *volume, const char *path, bool write);
  bool (*file_close)(fs_file_t *file);
  size_t (*file_read)(fs_file_t *file, void *buffer, size_t size);
  size_t (*file_write)(fs_file_t *file, const void *buffer, size_t size);
  bool (*file_seek)(fs_file_t *file, size_t offset);
};

// Concrete backing type for the opaque fs_volume_t handle.
struct fs_volume_t {
  const struct fs_volume_ops_t *ops; // Backend method table; NULL == free
                                     // slot (see ../any/pool.c).
  _Alignas(max_align_t) unsigned char ctx[FS_VOLUME_CTX_SIZE]; // Backend
                                                               // storage.
};

// Claim a free slot from the static volume pool, or NULL if none remain.
extern fs_volume_t *_fs_vol_alloc(void);

// Return a slot to the static volume pool. Safe to call with NULL.
extern void _fs_vol_free(fs_volume_t *volume);
