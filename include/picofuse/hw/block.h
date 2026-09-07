/**
 * @file block.h
 * @brief Generic block device interface.
 * @defgroup Block Block I/O
 * @ingroup Hardware
 *
 * A block-oriented read/write/erase abstraction: storage is addressed as
 * a fixed number of equally-sized blocks (@ref hw_block_count,
 * @ref hw_block_size), rather than a flat byte stream - the natural shape
 * for media that can only be written a block at a time and, for some
 * backends, must be explicitly erased before it can be rewritten at all
 * (@ref hw_block_erase). This module only provides that raw block
 * transport; it's meant to sit underneath higher-level systems that need
 * one - a filesystem - not to be used as a storage format in its own
 * right.
 *
 * @ref hw_block_t is a generic handle - @ref hw_block_read,
 * @ref hw_block_write, @ref hw_block_erase and @ref hw_block_deinit all
 * dispatch through a backend-supplied `hw_block_callbacks_t` (see
 * `src/picofuse/hw/block/private.h`), so the same four calls work
 * regardless of what's actually backing the handle. `hw/flash.h`'s
 * @ref hw_block_flash_init is the only concrete backend today (on-chip
 * flash, reserved outside the running program's own image); the same
 * shape is intended for mass storage (SD/eMMC) and RAM-backed block
 * devices as those get built. Handles come from a small fixed-size pool
 * (@ref BLOCK_MAX_CAPACITY) with a fixed amount of backend-private state
 * embedded in each one (@ref BLOCK_CONTEXT_SIZE), not the heap.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

/**
 * @brief Maximum number of concurrently active block handles.
 * @ingroup Block
 *
 * Override by defining `BLOCK_MAX_CAPACITY` at compile time.
 */
#ifndef BLOCK_MAX_CAPACITY
#define BLOCK_MAX_CAPACITY 4u
#endif

/**
 * @brief Size in bytes of backend-private state embedded in each block handle.
 * @ingroup Block
 *
 * Override by defining `BLOCK_CONTEXT_SIZE` at compile time.
 */
#ifndef BLOCK_CONTEXT_SIZE
#define BLOCK_CONTEXT_SIZE 32u
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct hw_block_t hw_block_t;

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Return the number of addressable blocks.
 * @ingroup Block
 * @param block Block handle.
 * @return Number of blocks, or `0` when invalid.
 */
size_t hw_block_count(const hw_block_t *block);

/**
 * @brief Return the block size in bytes.
 * @ingroup Block
 * @param block Block handle.
 * @return Block size in bytes, or `0` when invalid.
 */
size_t hw_block_size(const hw_block_t *block);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Read one block into @p dst.
 * @ingroup Block
 * @param block Block handle.
 * @param index Block index to read.
 * @param dst Destination buffer of at least @ref hw_block_size bytes.
 * @retval true Read succeeded.
 * @retval false Invalid arguments or backend failure.
 */
bool hw_block_read(const hw_block_t *block, size_t index, void *dst);

/**
 * @brief Erase one block.
 * @ingroup Block
 * @param block Block handle.
 * @param index Block index to erase.
 * @retval true Erase succeeded.
 * @retval false Invalid arguments or backend failure.
 */
bool hw_block_erase(hw_block_t *block, size_t index);

/**
 * @brief Write one block from @p src.
 * @ingroup Block
 * @param block Block handle.
 * @param index Block index to write.
 * @param src Source buffer of at least @ref hw_block_size bytes.
 * @retval true Write succeeded.
 * @retval false Invalid arguments or backend failure.
 */
bool hw_block_write(hw_block_t *block, size_t index, const void *src);

/**
 * @brief Deinitialize a block handle.
 * @ingroup Block
 * @param block Block handle.
 */
void hw_block_deinit(hw_block_t *block);

/** @} */