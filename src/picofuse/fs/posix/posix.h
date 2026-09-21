#pragma once
#include "../private.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

// Backend-private data for a POSIX directory-backed volume, hung off
// fs_volume_t.ctx (see ../private.h). fs_vol_init_path() rejects any
// resolved root path longer than FS_PATH_MAX (see picofuse/fs/types.h).
typedef struct {
  char root[FS_PATH_MAX + 1]; // Canonical absolute root, no trailing '/'
  size_t root_len;            // strlen(root), cached
} fs_posix_ctx_t;

_Static_assert(sizeof(fs_posix_ctx_t) <= FS_VOLUME_CTX_SIZE,
               "fs_posix_ctx_t exceeds FS_VOLUME_CTX_SIZE");

///////////////////////////////////////////////////////////////////////////////
// PATH CONFINEMENT (see path.c)
//
// A volume never chroot(2)s into its root - that call is process-wide,
// normally privileged, and irreversible, so it would confine every other
// volume/file access in the process too. Instead these resolve the
// caller's volume-relative path against the stored root and reject
// anything that canonicalizes outside it (symlink escapes included).
// Shared by volume.c and file.c so the confinement logic exists in exactly
// one place.

// True if canonical, absolute `resolved` lies within ctx->root (or equals
// it).
extern bool _fs_posix_confined(const fs_posix_ctx_t *ctx,
                               const char *resolved);

// Join a volume-relative `path` ("/" for root, "/sub/dir" otherwise) onto
// ctx->root without requiring the target to exist yet.
extern bool _fs_posix_join(const fs_posix_ctx_t *ctx, const char *path,
                           char out[static PATH_MAX]);

// Resolve a volume-relative `path` that must already exist, verifying the
// result stays within ctx->root. False if missing, unresolvable, or it
// escapes root.
extern bool _fs_posix_resolve(const fs_posix_ctx_t *ctx, const char *path,
                              char out[static PATH_MAX]);

// Resolve the (confined, existing) parent directory of a not-yet-existing
// `path`, and append its final component verbatim. Used for targets such
// as a new directory (mkdir) or a new file (create). The leaf must be a
// single path component; "." and ".." are rejected.
extern bool _fs_posix_resolve_new(const fs_posix_ctx_t *ctx, const char *path,
                                  char out[static PATH_MAX]);

// Extract the final path component of a canonical, confined path into a
// caller-provided buffer (size FS_PATH_MAX + 1), truncating if necessary.
extern void _fs_posix_basename(const char *resolved,
                               char out[static FS_PATH_MAX + 1]);

///////////////////////////////////////////////////////////////////////////////
// FILE OPS (see file.c)
//
// Referenced by the fs_posix_ops table in volume.c.

extern fs_file_t _fs_posix_file_create(fs_volume_t *volume, const char *path);
extern fs_file_t _fs_posix_file_open(fs_volume_t *volume, const char *path,
                                     bool write);
extern bool _fs_posix_file_close(fs_file_t *file);
extern size_t _fs_posix_file_read(fs_file_t *file, void *buffer, size_t size);
extern size_t _fs_posix_file_write(fs_file_t *file, const void *buffer,
                                   size_t size);
extern bool _fs_posix_file_seek(fs_file_t *file, size_t offset);
