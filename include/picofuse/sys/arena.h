/**
 * @brief Memory allocation primitives: arenas and the default heap wrappers
 * built on them.
 * @defgroup SystemMemory Memory
 * @ingroup System
 */

/**
 * @file sys/arena.h
 * @brief Defines arena allocator types and operations.
 * @defgroup SystemArenas Arenas
 * @ingroup SystemMemory
 * @brief Region-oriented allocation primitives used by the default memory
 * wrappers and by callers that need deterministic allocation behavior.
 * @details
 * The arena API provides region-oriented allocation primitives used by the
 * default memory wrappers and by callers that need deterministic allocation
 * behavior.
 *
 * Arenas can be chained to grow capacity incrementally while preserving
 * locality and ownership boundaries. Allocation/reallocation/free operations
 * in this header operate on the supplied arena only; they do not traverse
 * successor arenas unless explicitly done by higher-level logic.
 *
 * Thread safety:
 * - All operations on a given arena (including chain-growing calls to
 *   sys_mem_arena_init() that pass it as `prev`) are safe to call
 *   concurrently from multiple threads or cores.
 *
 * Alignment:
 * - sys_mem_arena_alloc() and sys_mem_arena_realloc() return memory aligned
 *   suitably for any object type (the same guarantee `malloc()` makes),
 *   regardless of the requested size.
 *
 * Typical flow:
 * 1. Create an arena with `sys_mem_arena_init(...)`.
 * 2. Allocate/reallocate/free within that arena.
 * 3. Optionally walk the chain with `sys_mem_arena_next(...)`.
 * 4. Delete arenas with `sys_mem_arena_delete(...)`, tail first.
 */

#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Arena allocator handle.
 * @ingroup SystemArenas
 * @headerfile arena.h picofuse/sys.h
 */
typedef struct sys_mem_arena_t sys_mem_arena_t;

/**
 * @brief Snapshot of arena usage statistics.
 * @ingroup SystemArenas
 * @headerfile arena.h picofuse/sys.h
 */
typedef struct sys_mem_stats_t {
  size_t size_bytes;  ///< Total payload capacity of the arena in bytes.
  size_t used_bytes;  ///< Number of payload bytes currently allocated.
  size_t allocations; ///< Number of active allocations in the arena.
} sys_mem_stats_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Arena Lifecycle
 * @{ */

/**
 * @brief Initialize a new arena.
 * @ingroup SystemArenas
 * @param size Arena size in bytes.
 * @param prev Pointer to the previous (tail) arena, or `NULL` to start a new
 *             chain.
 * @param malloc_fn Underlying allocation function. Must be non-NULL when
 *                  `prev` is `NULL`, and must be `NULL` when `prev` is
 *                  non-NULL.
 * @param free_fn Underlying deallocation function. Must be non-NULL when
 *                `prev` is `NULL`, and must be `NULL` when `prev` is
 *                non-NULL.
 * @return Pointer to the newly initialized arena, or `NULL` on failure.
 *
 * When creating the first arena in a chain, pass `prev` as `NULL` along with
 * `malloc_fn`/`free_fn` pointing to the underlying allocator implementation
 * to use. When appending to an existing chain, pass `prev` as the chain's
 * current tail (see sys_mem_arena_next()) and pass `NULL` for both
 * `malloc_fn` and `free_fn` - the new arena inherits `prev`'s allocator.
 *
 * Returns `NULL` if `prev` is non-NULL and is not the chain's tail, if
 * `malloc_fn`/`free_fn` don't follow the NULL-pairing rule above, or if the
 * underlying allocation fails.
 */
sys_mem_arena_t *sys_mem_arena_init(size_t size, sys_mem_arena_t *prev,
                                    void *(*malloc_fn)(size_t size),
                                    void (*free_fn)(void *ptr));

/**
 * @brief Delete the tail arena of a chain.
 * @ingroup SystemArenas
 * @param arena The tail arena to delete. Must be non-NULL, and must be the
 *              last arena in its chain (sys_mem_arena_next(arena, NULL)
 *              must return `NULL`) - deleting a non-tail arena is invalid.
 *
 * Releases `arena` back to its underlying allocator. To fully tear down a
 * chain, delete arenas from the tail backward (LIFO order): the arena that
 * was passed as `prev` when `arena` was created becomes the new tail.
 */
void sys_mem_arena_delete(sys_mem_arena_t *arena);

/**
 * @brief Return the next arena in a chain.
 * @ingroup SystemArenas
 * @param arena Pointer to the current arena.
 * @param stats Optional pointer populated with stats for `arena` when non-NULL.
 * @return Pointer to the next (more recently appended) arena, or `NULL` when
 *         `arena` is the tail.
 */
sys_mem_arena_t *sys_mem_arena_next(sys_mem_arena_t *arena,
                                    sys_mem_stats_t *stats);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @name Arena Allocation
 * @{ */

/**
 * @brief Allocate memory from a single arena.
 * @ingroup SystemArenas
 * @param arena Arena that services the allocation request.
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocated block, aligned suitably for any object
 *         type, or `NULL` on failure.
 *
 * This function operates only on the supplied arena and does not traverse any
 * linked successor arenas.
 */
void *sys_mem_arena_alloc(sys_mem_arena_t *arena, size_t size);

/**
 * @brief Resize an allocation within a single arena.
 * @ingroup SystemArenas
 * @param arena Arena that owns the allocation.
 * @param ptr Existing allocation to resize, or `NULL`.
 * @param size New size in bytes.
 * @return Pointer to the resized block, aligned suitably for any object
 *         type, or `NULL` on failure.
 *
 * This function operates only on the supplied arena and does not traverse any
 * linked successor arenas.
 */
void *sys_mem_arena_realloc(sys_mem_arena_t *arena, void *ptr, size_t size);

/**
 * @brief Release an allocation owned by a single arena.
 * @ingroup SystemArenas
 * @param arena Arena that owns the allocation.
 * @param ptr Allocation to release, or `NULL`.
 *
 * This function operates only on the supplied arena and does not traverse any
 * linked successor arenas.
 */
void sys_mem_arena_free(sys_mem_arena_t *arena, void *ptr);

/** @} */

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
