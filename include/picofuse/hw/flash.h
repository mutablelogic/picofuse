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
 *
 * @note On Pico, `hw_block_erase()`/`hw_block_write()` on the returned
 * handle currently only work when called from core 0 - calling either
 * from any other worker fails outright (returns `false`, nothing is
 * touched), since erasing/programming flash requires briefly pausing
 * whatever else the other core is doing, and only core 0 can currently
 * initiate that pause. `hw_block_read()` has no such restriction - it
 * works from any core. See `TODO.md`'s "Transparent core-1 -> core-0
 * flash write/erase marshaling" for the planned fix that will make
 * erase/write core-independent too.
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