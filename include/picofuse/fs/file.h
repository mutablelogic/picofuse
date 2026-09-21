/**
 * @file fs/file.h
 * @brief File lifecycle and I/O operations.
 * @defgroup FileSystemFile File Operations
 * @ingroup FileSystem
 *
 * Functions to create, open, close, read, write and seek within files on a
 * mounted volume.
 */
#pragma once
#include "types.h"
#include <stdbool.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name File Management
 * @{ */

/**
 * @brief Create a new file or truncate an existing file to zero length.
 * @ingroup FileSystemFile
 *
 * @param volume Mounted volume.
 * @param path Path to create.
 * @return Opened read/write file, or zeroed struct on failure.
 */
extern fs_file_t fs_file_create(fs_volume_t *volume, const char *path);

/**
 * @brief Open an existing file for read/write.
 * @ingroup FileSystemFile
 *
 * @param volume Mounted volume.
 * @param path File path to open.
 * @param write True to open for writing, false for read-only.
 * @return Opened file, or zeroed struct on failure.
 */
extern fs_file_t fs_file_open(fs_volume_t *volume, const char *path,
                              bool write);

/**
 * @brief Close an opened file.
 * @ingroup FileSystemFile
 *
 * @param file File to close.
 * @return true on success, false on error.
 */
extern bool fs_file_close(fs_file_t *file);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Read data from an opened file.
 * @ingroup FileSystemFile
 *
 * @param file File to read from.
 * @param buffer Buffer to read data into.
 * @param size Number of bytes to read (cannot be zero).
 * @return Number of bytes read, or 0 on error.
 */
extern size_t fs_file_read(fs_file_t *file, void *buffer, size_t size);

/**
 * @brief Write data to an opened file.
 * @ingroup FileSystemFile
 *
 * @param file File to write to.
 * @param buffer Buffer to write data from.
 * @param size Number of bytes to write (cannot be zero).
 * @return Number of bytes written, or 0 on error.
 */
extern size_t fs_file_write(fs_file_t *file, const void *buffer, size_t size);

/**
 * @brief Seek to a position within an opened file.
 * @ingroup FileSystemFile
 *
 * @param file File to seek.
 * @param offset Offset in bytes from the beginning of the file.
 * @return true on success, false on error.
 */
extern bool fs_file_seek(fs_file_t *file, size_t offset);

/** @} */
