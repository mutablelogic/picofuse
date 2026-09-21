/**
 * @file fs/volume.h
 * @brief Filesystem volume lifecycle management.
 * @defgroup FileSystemVolume Volume Management
 * @ingroup FileSystem
 *
 * Functions to create, mount and finalize filesystem volumes backed by RAM,
 * a host image file, or an existing host directory.
 */
#pragma once
#include "types.h"
#include <picofuse/sys/arena.h>
#include <stdbool.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

/**
 * @brief Maximum number of volumes that may be mounted concurrently.
 * @ingroup FileSystemVolume
 *
 * fs_volume_t instances are drawn from a fixed-size static pool rather than
 * the heap; define this before including this header to change its size.
 */
#ifndef FS_VOLUME_MAX
#define FS_VOLUME_MAX 4u
#endif

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Volume Management
 * @{ */

/**
 * @brief Create a new volatile (RAM) filesystem volume.
 * @ingroup FileSystemVolume
 *
 * @param arena Arena to allocate the volume's storage from, or NULL to use
 * the default heap allocator.
 * @param size Requested minimum size in bytes (rounded up to block geometry;
 * always at least two blocks, littlefs's minimum for its own metadata).
 * @return Pointer to mounted volume on success, NULL on failure.
 *
 * Notes:
 *  - Contents are lost when fs_vol_deinit() is called or the process exits.
 *  - The real capacity may be larger than requested due to block rounding.
 */
extern fs_volume_t *fs_vol_init_memory(sys_mem_arena_t *arena, size_t size);

/**
 * @brief Open or create a host file–backed persistent volume.
 * @ingroup FileSystemVolume
 *
 * Attempts to mount the filesystem stored in @p path. If mounting fails, a
 * format is performed and a fresh filesystem created. If the file is shorter
 * than @p size it may be expanded (filling new space with erased pattern
 * 0xFF).
 *
 * @param path Host path to the image file (created if absent).
 * @param size Minimum size in bytes; ignored if existing file is larger.
 * @return Mounted volume pointer, or NULL on error.
 *
 * @note Not available on embedded (Flash-backed) builds; returns NULL there.
 */
extern fs_volume_t *fs_vol_init_file(const char *path, size_t size);

/**
 * @brief Open or create a non-volatile flash-backed persistent volume.
 * @ingroup FileSystemVolume
 *
 * Attempts to mount the filesystem stored in flash memory. The location of
 * the volume in flash memory is implementation-specific. If mounting fails,
 * a format is performed and a fresh filesystem created.
 *
 * @param size Requested size in bytes (rounded down to flash erase-size
 * granularity). The backing region is allocated relative to this value on
 * every call, so remounting a volume formatted by a previous call requires
 * passing the same size again - a different size allocates a different
 * region and will not find the earlier filesystem.
 * @return Mounted volume pointer, or NULL on error.
 *
 * @note Not available on host (Linux/macOS) builds; returns NULL there.
 */
extern fs_volume_t *fs_vol_init_flash(size_t size);

/**
 * @brief Mount a volume rooted at an existing host directory.
 * @ingroup FileSystemVolume
 *
 * Provides a filesystem view backed directly by a directory on the host
 * (Linux/macOS) filesystem rather than a littlefs image, so files are read
 * and written straight through to the underlying host OS.
 *
 * @param path Host directory to use as the volume root (must already exist).
 * @return Mounted volume pointer, or NULL on error (for example if @p path
 * does not exist or is not a directory).
 *
 * @note Not available on embedded (Flash-backed) builds; returns NULL there.
 */
extern fs_volume_t *fs_vol_init_path(const char *path);

/**
 * @brief Unmount and release all resources for a volume.
 * @ingroup FileSystemVolume
 *
 * @param volume Volume returned from an init call (may be NULL).
 *
 * Safe to call with NULL (no effect). After return the pointer is invalid.
 */
extern void fs_vol_deinit(fs_volume_t *volume);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Return total addressable size of a mounted volume.
 * @ingroup FileSystemVolume
 *
 * @param volume Volume handle.
 * @param free Pointer to size_t to receive approximate free space in bytes
 * (may be NULL).
 * @return Size in bytes, or 0 if @p volume is NULL/invalid.
 */
extern size_t fs_vol_size(fs_volume_t *volume, size_t *free);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Iterate directory entries.
 * @ingroup FileSystemVolume
 *
 * @param volume Mounted volume.
 * @param path Directory path ("/" for root). NULL treated as root.
 * @param iterator In/out. Provide zeroed struct to start; do NOT alter ctx.
 * @return true if an entry was produced (fields populated), false when done.
 *
 * On first call, iterator must be zeroed to start iteration.
 * Successive calls return the next entry until no more are available, at
 * which point false is returned and iterator->name[0] is set to '\0'. The
 * iterator state is allocated internally and freed when iteration
 * ends or an error occurs.
 *
 * @note The order of entries is not specified and may vary between calls.
 * @note Hidden entries (name begins with '.', including "." and "..") are
 * not returned.
 * @note The iterator must be repeatedly called until it returns false to free
 * internal resources.
 */
extern bool fs_vol_readdir(fs_volume_t *volume, const char *path,
                           fs_file_t *iterator);

/**
 * @brief Lookup file or directory metadata for a path.
 * @ingroup FileSystemVolume
 *
 * @param volume Mounted volume.
 * @param path Absolute path within volume (NULL/empty -> root).
 * @return Populated fs_file_t. If not found, name[0] == '\0'.
 */
extern fs_file_t fs_vol_stat(fs_volume_t *volume, const char *path);

/**
 * @brief Create a directory.
 * @ingroup FileSystemVolume
 *
 * @param volume Mounted volume.
 * @param path New directory path (parents must already exist).
 * @return true on success or if directory already exists; false on error.
 */
extern bool fs_vol_mkdir(fs_volume_t *volume, const char *path);

/**
 * @brief Remove a file or (empty) directory.
 * @ingroup FileSystemVolume
 *
 * @param volume Mounted volume.
 * @param path Path to remove.
 * @return true on success; false if not found or directory not empty.
 */
extern bool fs_vol_remove(fs_volume_t *volume, const char *path);

/**
 * @brief Move or rename a file/directory.
 * @ingroup FileSystemVolume
 *
 * @param volume Mounted volume.
 * @param old_path Existing path.
 * @param new_path Destination path (must not already exist).
 * @return true on success, false on error (missing source, conflict, etc.).
 */
extern bool fs_vol_move(fs_volume_t *volume, const char *old_path,
                        const char *new_path);

/** @} */
