#include "posix.h"
#include <picofuse/sys.h>
#include <stdlib.h>
#include <string.h>

bool _fs_posix_confined(const fs_posix_ctx_t *ctx, const char *resolved) {
  if (memcmp(resolved, ctx->root, ctx->root_len) != 0) {
    return false;
  }
  char next = resolved[ctx->root_len];
  return next == '\0' || next == FS_PATH_SEPARATOR;
}

bool _fs_posix_join(const fs_posix_ctx_t *ctx, const char *path,
                    char out[static PATH_MAX]) {
  if (path == NULL || path[0] == '\0') {
    path = FS_PATH_SEPARATOR_STR;
  }
  if (path[0] != FS_PATH_SEPARATOR) {
    return false; // volume paths are always root-relative
  }
  size_t n = (path[1] == '\0')
                 ? sys_sprintf(out, PATH_MAX, "%s", ctx->root)
                 : sys_sprintf(out, PATH_MAX, "%s%s", ctx->root, path);
  return n > 0 && n < PATH_MAX;
}

bool _fs_posix_resolve(const fs_posix_ctx_t *ctx, const char *path,
                       char out[static PATH_MAX]) {
  char joined[PATH_MAX];
  if (!_fs_posix_join(ctx, path, joined)) {
    return false;
  }
  if (realpath(joined, out) == NULL) {
    return false;
  }
  return _fs_posix_confined(ctx, out);
}

bool _fs_posix_resolve_new(const fs_posix_ctx_t *ctx, const char *path,
                           char out[static PATH_MAX]) {
  if (path == NULL || path[0] != FS_PATH_SEPARATOR || path[1] == '\0') {
    return false; // must be an absolute, non-root volume path
  }

  const char *leaf = strrchr(path, FS_PATH_SEPARATOR) + 1;
  if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
    return false;
  }

  char parent_path[PATH_MAX];
  size_t parent_len = (size_t)(leaf - path);
  if (parent_len == 1) {
    // Leaf lives directly under the volume root.
    strcpy(parent_path, FS_PATH_SEPARATOR_STR);
  } else {
    if (parent_len >= sizeof(parent_path)) {
      return false;
    }
    memcpy(parent_path, path, parent_len - 1);
    parent_path[parent_len - 1] = '\0';
  }

  char parent_resolved[PATH_MAX];
  if (!_fs_posix_resolve(ctx, parent_path, parent_resolved)) {
    return false;
  }

  size_t n = sys_sprintf(out, PATH_MAX, "%s%c%s", parent_resolved,
                         FS_PATH_SEPARATOR, leaf);
  return n > 0 && n < PATH_MAX;
}

void _fs_posix_basename(const char *resolved, char out[static FS_PATH_MAX + 1]) {
  const char *base = strrchr(resolved, FS_PATH_SEPARATOR);
  base = (base != NULL && base[1] != '\0') ? base + 1 : FS_PATH_SEPARATOR_STR;
  size_t len = strlen(base);
  if (len > FS_PATH_MAX) {
    len = FS_PATH_MAX;
  }
  memcpy(out, base, len);
  out[len] = '\0';
}
