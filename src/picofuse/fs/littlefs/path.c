#include "littlefs.h"
#include <string.h>

const char *_fs_lfs_path(const char *path) {
  return (path == NULL || path[0] == '\0') ? FS_PATH_SEPARATOR_STR : path;
}

bool _fs_lfs_is_root(const char *path) {
  return path != NULL && path[0] == FS_PATH_SEPARATOR && path[1] == '\0';
}

bool _fs_lfs_resolve(fs_lfs_ctx_t *ctx, const char *path,
                     char out[static FS_PATH_MAX + 1]) {
  path = _fs_lfs_path(path);
  if (path[0] != FS_PATH_SEPARATOR) {
    return false; // volume paths are always root-relative
  }

  size_t out_len = 0;
  out[0] = '\0';

  const char *p = path;
  while (*p != '\0') {
    while (*p == FS_PATH_SEPARATOR) {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    const char *start = p;
    while (*p != '\0' && *p != FS_PATH_SEPARATOR) {
      p++;
    }
    size_t len = (size_t)(p - start);

    if (len == 1 && start[0] == '.') {
      continue; // "." -> no-op
    }
    if (len == 2 && start[0] == '.' && start[1] == '.') {
      if (out_len == 0) {
        return false; // climbing above the volume root
      }
      size_t i = out_len;
      while (out[i - 1] != FS_PATH_SEPARATOR) {
        i--;
      }
      out_len = i - 1;
      out[out_len] = '\0';
      continue;
    }

    if (out_len + 1 + len >= FS_PATH_MAX + 1) {
      return false; // canonical path too long
    }
    out[out_len] = FS_PATH_SEPARATOR;
    memcpy(out + out_len + 1, start, len);
    out_len += 1 + len;
    out[out_len] = '\0';

    // Every component but the last must already exist as a directory,
    // mirroring realpath()'s requirement that intermediate components
    // exist even if a later ".." would cancel them out.
    const char *rest = p;
    while (*rest == FS_PATH_SEPARATOR) {
      rest++;
    }
    if (*rest != '\0') {
      struct lfs_info info;
      if (lfs_stat(&ctx->lfs, out, &info) != LFS_ERR_OK ||
          info.type != LFS_TYPE_DIR) {
        return false;
      }
    }
  }

  if (out_len == 0) {
    out[0] = FS_PATH_SEPARATOR;
    out[1] = '\0';
  }
  return true;
}

bool _fs_lfs_resolve_new(fs_lfs_ctx_t *ctx, const char *path,
                         char out[static FS_PATH_MAX + 1]) {
  if (path == NULL || path[0] != FS_PATH_SEPARATOR || path[1] == '\0') {
    return false; // must be an absolute, non-root volume path
  }

  const char *leaf = strrchr(path, FS_PATH_SEPARATOR) + 1;
  if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
    return false;
  }

  char parent_path[FS_PATH_MAX + 1];
  size_t parent_len = (size_t)(leaf - path);
  if (parent_len == 1) {
    // Leaf lives directly under the volume root.
    parent_path[0] = FS_PATH_SEPARATOR;
    parent_path[1] = '\0';
  } else {
    if (parent_len - 1 >= sizeof(parent_path)) {
      return false;
    }
    memcpy(parent_path, path, parent_len - 1);
    parent_path[parent_len - 1] = '\0';
  }

  char parent_resolved[FS_PATH_MAX + 1];
  if (!_fs_lfs_resolve(ctx, parent_path, parent_resolved)) {
    return false;
  }
  // _fs_lfs_resolve only validates *intermediate* components; the parent
  // itself must also already exist as a directory.
  struct lfs_info info;
  if (lfs_stat(&ctx->lfs, parent_resolved, &info) != LFS_ERR_OK ||
      info.type != LFS_TYPE_DIR) {
    return false;
  }

  size_t parent_resolved_len = strlen(parent_resolved);
  size_t leaf_len = strlen(leaf);
  bool parent_is_root = parent_resolved_len == 1;
  size_t total = parent_resolved_len + (parent_is_root ? 0 : 1) + leaf_len;
  if (total >= FS_PATH_MAX + 1) {
    return false;
  }

  memcpy(out, parent_resolved, parent_resolved_len);
  size_t pos = parent_resolved_len;
  if (!parent_is_root) {
    out[pos++] = FS_PATH_SEPARATOR;
  }
  memcpy(out + pos, leaf, leaf_len);
  pos += leaf_len;
  out[pos] = '\0';
  return true;
}

void _fs_lfs_basename(const char *resolved, char out[static FS_PATH_MAX + 1]) {
  const char *base = strrchr(resolved, FS_PATH_SEPARATOR);
  base = (base != NULL && base[1] != '\0') ? base + 1 : FS_PATH_SEPARATOR_STR;
  size_t len = strlen(base);
  if (len > FS_PATH_MAX) {
    len = FS_PATH_MAX;
  }
  memcpy(out, base, len);
  out[len] = '\0';
}
