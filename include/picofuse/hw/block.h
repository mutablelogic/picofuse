/**
 * @file block.h
 * @brief Generic block device interface.
 * @defgroup Block Block I/O
 * @ingroup Hardware
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
 * @param index Block index to erase.
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