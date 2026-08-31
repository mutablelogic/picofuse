#pragma once
#include <picofuse/sys/arena.h>

/** @brief Returns the arena appended before `arena`, or `NULL` if `arena` is
 * the head of its chain. Optionally populates `stats` for `arena` itself,
 * same as sys_mem_arena_next(). Not part of the public API: only meant for
 * walking a chain tail-to-head, which sys_mem.c needs to prefer more
 * recently grown (and so more likely to have room) arenas first. */
sys_mem_arena_t *_sys_mem_arena_prev(sys_mem_arena_t *arena,
                                     sys_mem_arena_stats_t *stats);

/** @brief Returns the current payload size of the allocation at `ptr` if
 * `arena` owns it, or `0` if it doesn't (including if `ptr` is `NULL`). Not
 * part of the public API: only meant for sys_mem.c to determine which arena
 * in a chain owns a given pointer, for sys_realloc()/sys_free(). */
size_t _sys_mem_arena_alloc_size(sys_mem_arena_t *arena, void *ptr);
