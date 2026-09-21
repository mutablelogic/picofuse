/**
 * @file fs/types.h
 * @brief Shared types and constants for the filesystem API.
 * @ingroup FileSystem
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

#define FS_PATH_MAX 255       ///< Maximum length of a path component or a
                               ///< full path accepted by the API
#define FS_PATH_SEPARATOR '/'     ///< Path separator character
#define FS_PATH_SEPARATOR_STR "/" ///< Path separator, as a string

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque filesystem volume handle.
 * @ingroup FileSystem
 */
typedef struct fs_volume_t fs_volume_t;

/**
 * @brief File and directory metadata.
 * @ingroup FileSystem
 */
typedef struct {
  fs_volume_t *volume;        ///< Owning volume (set by APIs)
  bool dir;                   ///< True if directory, false if regular file
  char name[FS_PATH_MAX + 1]; ///< Filename (never NULL after success)
                               ///< including trailing '\0'
  size_t size;                ///< Size in bytes (regular files only, else 0)
  size_t pos;                 ///< Current file position for read/write (files
                               ///< only)
  void *ctx;                  ///< Opaque context (do not modify). Backends
                               ///< store their live handle here directly
                               ///< (e.g. a POSIX fd) rather than pointing at
                               ///< pooled storage: fs_file_t is caller-owned
                               ///< and also doubles as the fs_vol_stat()
                               ///< result and fs_vol_readdir() iterator, so
                               ///< it stays a plain value type, not an
                               ///< opaque handle like fs_volume_t.
} fs_file_t;
