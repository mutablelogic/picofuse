/**
 * @file flash.h
 * @brief Flash storage helpers.
 * @defgroup Flash Flash
 * @ingroup Block
 */
#pragma once
#include "block.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Initialize a flash-backed block region.
 * @ingroup Flash
 *
 * Allocates a block region from the free flash area. Requested size is
 * rounded down to the flash erase-size granularity.
 *
 * @param size_bytes Requested region size in bytes.
 * @return A block descriptor on success, or `NULL` on failure.
 */
hw_block_t *hw_block_flash_init(size_t size_bytes);

/**
 * @brief Return the maximum currently available flash block size.
 * @ingroup Flash
 *
 * The returned value is aligned to the flash erase-size granularity.
 *
 * @return Maximum allocatable size in bytes.
 */
size_t hw_block_flash_get_capacity(void);

/** @} */