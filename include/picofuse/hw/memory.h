/**
 * @file memory.h
 * @brief Memory usage monitoring.
 * @defgroup Memory Memory
 * @ingroup Hardware
 *
 * @note Only implemented on Pico. On every other platform,
 * hw_memory_get_usage() still succeeds (returns `true`) but reports every
 * field as zero - there's no real RAM/stack/flash accounting backend for
 * a host OS, which already manages all of that itself.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Snapshot of active memory use.
 * @ingroup Memory
 * @headerfile memory.h picofuse/hw.h
 *
 * Unsupported regions are reported as zero. On Pico, RAM fields describe
 * primary SRAM, where globals, static data, and the heap reside; stack fields
 * describe the current core's separate scratch-SRAM stack.
 */
typedef struct hw_memory_usage_t {
  size_t ram_total_bytes;   ///< Total primary SRAM capacity.
  size_t ram_free_bytes;    ///< Primary SRAM available for heap growth.
  size_t heap_used_bytes;   ///< C allocator high-water usage in primary SRAM.
  size_t psram_used_bytes;  ///< Active PSRAM allocation bytes.
  size_t stack_total_bytes; ///< Current core's reserved stack capacity.
  size_t stack_used_bytes;  ///< Current core's active stack bytes.
  size_t stack_free_bytes;  ///< Current core's remaining stack capacity.
  size_t flash_total_bytes; ///< Physical flash capacity.
  size_t flash_used_bytes;  ///< Exact linked program image size in flash.
  size_t flash_reserved_bytes; ///< Flash reserved by active block devices.
  size_t
      flash_free_bytes; ///< Sector-usable flash after image and reservations.
} hw_memory_usage_t;

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Obtain a memory usage snapshot for the current platform.
 * @ingroup Memory
 *
 * On Pico, RAM capacity and free space come from linker and C allocator state.
 * Heap usage is a high-water mark and freeing memory does not reduce it.
 * Flash free space excludes the image's final partially occupied erase sector
 * and any flash block region reserved by hw_block_flash_init().
 *
 * Calculate percentages only when the corresponding total is nonzero:
 * @code{.c}
 * double ram_free_percent = 100.0 * usage.ram_free_bytes /
 *                           usage.ram_total_bytes;
 * double stack_free_percent = 100.0 * usage.stack_free_bytes /
 *                             usage.stack_total_bytes;
 * double flash_free_percent = 100.0 * usage.flash_free_bytes /
 *                             usage.flash_total_bytes;
 * @endcode
 * `ram_free_percent` describes primary SRAM available for heap growth. Stack
 * capacity is separate scratch SRAM and has its own percentage.
 *
 * @param usage Output snapshot. Must not be NULL.
 * @return true on success, false when @p usage is NULL.
 */
bool hw_memory_get_usage(hw_memory_usage_t *usage);

#ifdef __cplusplus
}
#endif