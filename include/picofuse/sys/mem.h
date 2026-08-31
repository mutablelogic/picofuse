/**
 * @file sys/mem.h
 * @brief Defines the default heap allocator wrappers.
 * @defgroup SystemHeap Default Allocator
 * @ingroup SystemMemory
 * @details
 * sys_malloc()/sys_calloc()/sys_realloc()/sys_free() are drop-in wrappers
 * around the process's allocator. Until a default arena has been configured,
 * they route straight through to the underlying system allocator (`malloc()`
 * and friends). Once one is configured, they allocate from it instead,
 * falling back to the system allocator only for a pointer the default arena
 * doesn't own (for example, one allocated before the arena was configured).
 *
 * Thread safety:
 * - Safe to call concurrently from multiple threads or cores, same as the
 *   underlying system allocator and sys_mem_arena_t.
 */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @name Default Allocator
 * @{ */

/**
 * @brief Allocate memory from the default allocator.
 * @ingroup SystemHeap
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocated block, or `NULL` on failure.
 */
void *sys_malloc(size_t size);

/**
 * @brief Allocate zero-initialized memory from the default allocator.
 * @ingroup SystemHeap
 * @param count Number of elements to allocate.
 * @param size Size of each element in bytes.
 * @return Pointer to the allocated block, or `NULL` on failure (including
 *         `count * size` overflowing).
 */
void *sys_calloc(size_t count, size_t size);

/**
 * @brief Resize an allocation owned by the default allocator.
 * @ingroup SystemHeap
 * @param ptr Existing allocation, or `NULL` (behaves as sys_malloc(size)).
 * @param size New size in bytes (`0` behaves as sys_free(ptr), returning
 *             `NULL`).
 * @return Pointer to the resized allocation, or `NULL` on failure - in
 *         which case `ptr` is left valid and untouched.
 */
void *sys_realloc(void *ptr, size_t size);

/**
 * @brief Release an allocation owned by the default allocator.
 * @ingroup SystemHeap
 * @param ptr Allocation to release, or `NULL` (a no-op).
 */
void sys_free(void *ptr);

/** @} */

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
